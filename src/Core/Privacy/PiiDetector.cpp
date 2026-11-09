#include "Core/Privacy/PiiDetector.h"

#include <QDate>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace GyanisScribe::Core::Privacy
{

    namespace
    {
        // 手机号允许 +86 或裸 86 前缀，以及空格/短横线；前后不得再贴数字，避免把更长的编号切一段出来
        const QRegularExpression kPhonePattern{QStringLiteral(R"((?<!\d)(?:\+?86[\s-]?)?1[3-9](?:[\s-]?\d){9}(?!\d))")};
        // 座机必须带区号前导 0；短横线可选，区号 2-3 位、号码 7-8 位
        const QRegularExpression kLandlinePattern{QStringLiteral(R"((?<!\d)0\d{2,3}-?\d{7,8}(?!\d))")};
        // 身份证 18 位，末位允许 X；前后不能贴字母数字，防止从更长串里截取
        const QRegularExpression kIdCardPattern{QStringLiteral(R"((?<![0-9A-Za-z_])\d{17}[\dXx](?![0-9A-Za-z_]))")};
        // 银行卡先切出以空格/短横线相连的整段数字分组；具体取哪 13-19 位由 bankCardRanges 在分组边界上再挑
        const QRegularExpression kBankCardRunPattern{QStringLiteral(R"((?<!\d)\d+(?:[\s-]+\d+)*)")};
        // 分组串里的单个数字组；卡号与有效期/CVV 之间恰好也只隔空格或短横线
        const QRegularExpression kDigitGroupPattern{QStringLiteral(R"(\d+)")};
        // 邮箱要求至少一个域名点与 2 位以上字母 TLD；连续点等残形交给校验函数拒绝
        const QRegularExpression kEmailPattern{
                QStringLiteral(R"((?<![A-Za-z0-9._%+\-])[A-Za-z0-9._%+\-]+@(?:[A-Za-z0-9](?:[A-Za-z0-9\-]*[A-Za-z0-9])?\.)+[A-Za-z]{2,}(?![A-Za-z0-9\-]))")};
        // IPv4 候选只负责切出四段；每段 0-255 的判定在 isValidIpv4 里
        const QRegularExpression kIpv4Pattern{QStringLiteral(R"((?<![A-Za-z0-9_.\-])\d{1,3}(?:\.\d{1,3}){3}(?![A-Za-z0-9_.\-]))")};
        // IPv6 候选至少两段冒号；前后不得贴字母、数字或冒号，避免从 IPv6:2001:db8::1 这类标签尾部切出残段
        const QRegularExpression kIpv6Pattern{QStringLiteral(R"((?<![0-9A-Za-z:])[0-9A-Fa-f]{0,4}(?::[0-9A-Fa-f]{0,4}){2,7}(?![0-9A-Fa-f:]))")};

        // GB 11643 的加权因子与校验码表；权重按身份证前 17 位的从左到右顺序
        constexpr std::array<int, 17> kIdCardWeights{7, 9, 10, 5, 8, 4, 2, 1, 6, 3, 7, 9, 10, 5, 8, 4, 2};
        constexpr std::string_view    kIdCardCheckCodes{"10X98765432"};

        /**
         * @brief 一行文字与逐字符的词归属
         */
        struct LineText
        {
            QString            text;                 ///< 词之间补一个空格拼成的整行文本
            QVector<qsizetype> wordIndexByCharacter; ///< 每个字符归属的词下标；分隔符为 -1
        };

        /**
         * @brief 去重前的命中候选
         */
        struct Candidate
        {
            PiiKind kind = PiiKind::Phone; ///< 命中类型
            QRect   box;                   ///< 已夹回图像范围的框
            QString maskedSample;          ///< 脱敏样例
        };

        /**
         * @brief 数字分组串里的一段银行卡候选
         */
        struct BankCardRange
        {
            qsizetype start  = 0; ///< 相对分组串起点的字符偏移
            qsizetype length = 0; ///< 字符长度，含组间分隔符
            int       digits = 0; ///< 这一段里的数字位数
        };

        /**
         * @brief 把行内词拼成文本，并记住每个字符属于哪个词
         * @param line 一行识别结果
         * @return 整行文本与逐字符归属
         */
        LineText buildLineText(const OcrLine &line)
        {
            LineText  lineText;
            qsizetype totalLength = 0;
            for (const OcrWord &word: line.words)
            {
                totalLength += word.text.size();
            }
            totalLength += std::max<qsizetype>(0, line.words.size() - 1);
            lineText.text.reserve(totalLength);
            lineText.wordIndexByCharacter.reserve(totalLength);

            for (qsizetype wordIndex = 0; wordIndex < line.words.size(); ++wordIndex)
            {
                if (wordIndex > 0)
                {
                    // 词之间补空格与 OcrLine::text() 的拼法一致，正则跨词命中时才能落到词框上
                    lineText.text.append(QLatin1Char(' '));
                    lineText.wordIndexByCharacter.append(-1);
                }
                const QString &wordText = line.words.at(wordIndex).text;
                lineText.text.append(wordText);
                for (qsizetype characterIndex = 0; characterIndex < wordText.size(); ++characterIndex)
                {
                    lineText.wordIndexByCharacter.append(wordIndex);
                }
            }
            return lineText;
        }

        /**
         * @brief 生成只替换 OCR 常见混淆字符的数字型副本
         * @param text 原文
         * @return 与原文等长的副本；O/o 换成 0，l/I 换成 1，S 换成 5
         * @note 副本只参与数字型匹配，脱敏样例仍从原文取
         */
        QString normalizeDigitLike(const QString &text)
        {
            QString normalized = text;
            for (qsizetype index = 0; index < normalized.size(); ++index)
            {
                const QChar character = normalized.at(index);
                if (character == QLatin1Char('O') || character == QLatin1Char('o'))
                {
                    normalized[index] = QLatin1Char('0');
                } else if (character == QLatin1Char('l') || character == QLatin1Char('I'))
                {
                    normalized[index] = QLatin1Char('1');
                } else if (character == QLatin1Char('S'))
                {
                    normalized[index] = QLatin1Char('5');
                }
            }
            return normalized;
        }

        /**
         * @brief 取出文本里的数字字符
         * @param text 任意文本
         * @return 只含数字的新串
         */
        QString digitsOnly(const QString &text)
        {
            QString digits;
            digits.reserve(text.size());
            for (const QChar character: text)
            {
                if (character.isDigit())
                {
                    digits.append(character);
                }
            }
            return digits;
        }

        /**
         * @brief 取出文本里的字母与数字，丢掉 +、空格、短横线等分隔符
         * @param text 任意文本
         * @return 只含字母数字的新串
         */
        QString significantCharacters(const QString &text)
        {
            QString characters;
            characters.reserve(text.size());
            for (const QChar character: text)
            {
                if (character.isLetterOrNumber())
                {
                    characters.append(character);
                }
            }
            return characters;
        }

        /**
         * @brief 生成手机号脱敏样例
         * @param original 命中范围对应的原文
         * @return 138****8000 形状；长度异常时全遮
         */
        QString maskPhone(const QString &original)
        {
            QString characters = significantCharacters(original);
            if (characters.size() == 13 && characters.startsWith(QStringLiteral("86")))
            {
                characters.remove(0, 2);
            }
            if (characters.size() != 11)
            {
                return QStringLiteral("***");
            }
            return characters.left(3) + QStringLiteral("****") + characters.right(4);
        }

        /**
         * @brief 生成座机脱敏样例
         * @param original 命中范围对应的原文
         * @return 区号保留、号码留末四位；无短横线时保留前导 0
         */
        QString maskLandline(const QString &original)
        {
            const qsizetype dashIndex = original.indexOf(QLatin1Char('-'));
            if (dashIndex > 0)
            {
                const QString areaCode   = significantCharacters(original.left(dashIndex));
                const QString subscriber = significantCharacters(original.mid(dashIndex + 1));
                if (!areaCode.isEmpty() && subscriber.size() >= 4)
                {
                    return areaCode + QStringLiteral("-****") + subscriber.right(4);
                }
            }
            const QString characters = significantCharacters(original);
            if (characters.size() < 8)
            {
                return QStringLiteral("***");
            }
            return characters.left(1) + QStringLiteral("*").repeated(characters.size() - 5) + characters.right(4);
        }

        /**
         * @brief 生成身份证脱敏样例
         * @param original 命中范围对应的原文
         * @return 保留前 6 位与末 4 位，中间 8 位遮掉
         */
        QString maskIdCard(const QString &original)
        {
            const QString characters = significantCharacters(original);
            if (characters.size() != 18)
            {
                return QStringLiteral("***");
            }
            return characters.left(6) + QStringLiteral("********") + characters.right(4);
        }

        /**
         * @brief 生成银行卡脱敏样例
         * @param original 命中范围对应的原文
         * @return 保留前 4 位与末 4 位，中间按长度遮掉
         */
        QString maskBankCard(const QString &original)
        {
            const QString characters = significantCharacters(original);
            if (characters.size() < 8)
            {
                return QStringLiteral("***");
            }
            return characters.left(4) + QStringLiteral("*").repeated(characters.size() - 8) + characters.right(4);
        }

        /**
         * @brief 生成邮箱脱敏样例
         * @param original 命中范围对应的原文
         * @return 本地部分只留首字符，域名原样保留
         */
        QString maskEmail(const QString &original)
        {
            const qsizetype atIndex = original.indexOf(QLatin1Char('@'));
            if (atIndex <= 0 || atIndex + 1 >= original.size())
            {
                return QStringLiteral("***");
            }
            const QString localPart       = original.left(atIndex);
            const QString domain          = original.mid(atIndex + 1);
            const QString maskedLocalPart = localPart.size() > 1 ? localPart.left(1) + QStringLiteral("***") : QStringLiteral("*");
            return maskedLocalPart + QLatin1Char('@') + domain;
        }

        /**
         * @brief 生成 IPv4 脱敏样例
         * @param original 命中范围对应的原文
         * @return 保留前两段，后两段遮掉
         */
        QString maskIpv4(const QString &original)
        {
            const QStringList octets = original.split(QLatin1Char('.'));
            if (octets.size() != 4)
            {
                return QStringLiteral("***");
            }
            return octets.at(0) + QLatin1Char('.') + octets.at(1) + QStringLiteral(".***.***");
        }

        /**
         * @brief 生成 IPv6 脱敏样例
         * @param original 命中范围对应的原文
         * @return 保留前两个非空段，其余遮掉
         */
        QString maskIpv6(const QString &original)
        {
            const QStringList groups = original.split(QLatin1Char(':'));
            QStringList       keptGroups;
            for (const QString &group: groups)
            {
                if (group.isEmpty())
                {
                    continue;
                }
                keptGroups.append(group);
                if (keptGroups.size() == 2)
                {
                    break;
                }
            }
            if (keptGroups.isEmpty())
            {
                return QStringLiteral("IPv6:****");
            }
            return keptGroups.join(QLatin1Char(':')) + QStringLiteral(":****");
        }

        /**
         * @brief 判断 13 位数字是否为 +86 手机号去掉加号后的形状
         * @param digits 13 位数字候选
         * @return true 表示 86 之后是 1[3-9] 起头的 11 位号码
         */
        bool isCountryCodeMobileDigits(const QString &digits)
        {
            return digits.size() == 13 && digits.startsWith(QStringLiteral("86")) && digits.at(2) == QLatin1Char('1') && digits.at(3) >= QLatin1Char('3') &&
                   digits.at(3) <= QLatin1Char('9');
        }

        /**
         * @brief 校验手机号归一化后的数字形状
         * @param value 归一化副本里的命中原文
         * @return true 表示 11 位且以 1 开头，或 +86 后的 13 位形态
         */
        bool isValidPhone(const QString &value)
        {
            const QString digits = digitsOnly(value);
            if (digits.size() == 11)
            {
                return digits.startsWith(QLatin1Char('1'));
            }
            return isCountryCodeMobileDigits(digits);
        }

        /**
         * @brief 校验座机归一化后的数字形状
         * @param value 归一化副本里的命中原文
         * @return true 表示 10-12 位且以 0 开头
         */
        bool isValidLandline(const QString &value)
        {
            const QString digits = digitsOnly(value);
            return digits.size() >= 10 && digits.size() <= 12 && digits.startsWith(QLatin1Char('0'));
        }

        /**
         * @brief 校验身份证的出生日期与 GB 11643 校验位
         * @param value 归一化副本里的 18 位候选
         * @return true 表示日期真实存在且校验位匹配
         */
        bool isValidIdCard(const QString &value)
        {
            if (value.size() != 18)
            {
                return false;
            }
            for (qsizetype index = 0; index < 17; ++index)
            {
                if (!value.at(index).isDigit())
                {
                    return false;
                }
            }
            const QChar checkCharacter = value.at(17).toUpper();
            if (!checkCharacter.isDigit() && checkCharacter != QLatin1Char('X'))
            {
                return false;
            }

            const int   year  = value.mid(6, 4).toInt();
            const int   month = value.mid(10, 2).toInt();
            const int   day   = value.mid(12, 2).toInt();
            const QDate birthDate(year, month, day);
            // 日期必须真实存在且不在未来：未来日期多半来自误认出来的数字串
            if (!birthDate.isValid() || birthDate > QDate::currentDate())
            {
                return false;
            }

            int weightedSum = 0;
            for (std::size_t index = 0; index < kIdCardWeights.size(); ++index)
            {
                weightedSum += (value.at(static_cast<qsizetype>(index)).unicode() - u'0') * kIdCardWeights.at(index);
            }
            const char expectedCheckCharacter = kIdCardCheckCodes.at(static_cast<std::size_t>(weightedSum % 11));
            return checkCharacter == QLatin1Char(expectedCheckCharacter);
        }

        /**
         * @brief 按 Luhn 算法校验一串数字
         * @param digits 只含数字的文本
         * @return true 表示校验和能被 10 整除且不全为零
         */
        bool passesLuhn(const QString &digits)
        {
            int  sum         = 0;
            bool doubleDigit = false;
            for (qsizetype index = digits.size(); index > 0; --index)
            {
                const int digit = digits.at(index - 1).digitValue();
                if (digit < 0)
                {
                    return false;
                }
                int contribution = digit;
                if (doubleDigit)
                {
                    contribution *= 2;
                    if (contribution > 9)
                    {
                        contribution -= 9;
                    }
                }
                sum += contribution;
                doubleDigit = !doubleDigit;
            }
            return sum > 0 && sum % 10 == 0;
        }

        /**
         * @brief 校验银行卡候选
         * @param value 正则命中的候选，数字之间可能夹着空格或短横线
         * @return true 表示归一化后长度 13-19 且通过 Luhn
         * @note 以 86 开头的 13 位号码正是 +86 手机号去掉加号后的形状；86 是中国区号而不是已发行的卡 IIN，
         *       这类候选即使 Luhn 通过也不按银行卡收，避免同一段文字被银行卡抢走
         */
        bool isValidBankCard(const QString &value)
        {
            const QString digits = digitsOnly(value);
            if (isCountryCodeMobileDigits(digits))
            {
                return false;
            }
            return digits.size() >= 13 && digits.size() <= 19 && passesLuhn(digits);
        }

        /**
         * @brief 在一段以空格/短横线相连的数字分组里挑出银行卡子段
         * @param value 连续数字分组串，例如 `4111 1111 1111 1111 12`
         * @return 相对串首的字符区间，按从左到右排列；没有合法子段时为空
         * @details 只在分组边界上取 13-19 位，卡号后面紧跟的有效期/CVV 或前面的年份
         *          不会被硬拼进候选后拖垮整段校验；单组连续数字仍整体判定，不从长编号中间截卡号。
         *          多个互不重叠的合法子段都保留，一行两张卡时不会少报；重叠时先取位数多的。
         */
        std::vector<BankCardRange> bankCardRanges(const QString &value)
        {
            std::vector<BankCardRange>      groups;
            QRegularExpressionMatchIterator groupIterator = kDigitGroupPattern.globalMatch(value);
            while (groupIterator.hasNext())
            {
                const QRegularExpressionMatch group = groupIterator.next();
                groups.push_back(BankCardRange{group.capturedStart(), group.capturedLength(), static_cast<int>(group.capturedLength())});
            }
            if (groups.empty())
            {
                return {};
            }

            // 一整串连续数字只按原样判：20 位以上的长编号即使后 16 位碰巧过了 Luhn，也不从中间切一段出来
            if (groups.size() == 1)
            {
                if (groups.front().digits >= 13 && groups.front().digits <= 19 && isValidBankCard(value))
                {
                    return {BankCardRange{0, value.size(), groups.front().digits}};
                }
                return {};
            }

            std::vector<BankCardRange> validRanges;
            for (std::size_t first = 0; first < groups.size(); ++first)
            {
                int digits = 0;
                for (std::size_t last = first; last < groups.size(); ++last)
                {
                    digits += groups.at(last).digits;
                    if (digits > 19)
                    {
                        break;
                    }
                    if (digits < 13)
                    {
                        continue;
                    }
                    const qsizetype start = groups.at(first).start;
                    const qsizetype end   = groups.at(last).start + groups.at(last).length;
                    if (isValidBankCard(value.mid(start, end - start)))
                    {
                        validRanges.push_back(BankCardRange{start, end - start, digits});
                    }
                }
            }

            // 位数多的优先，位数相同取靠左的；再排除与已选区间重叠的候选
            std::stable_sort(validRanges.begin(), validRanges.end(),
                             [](const BankCardRange &left, const BankCardRange &right)
                             {
                                 if (left.digits != right.digits)
                                 {
                                     return left.digits > right.digits;
                                 }
                                 return left.start < right.start;
                             });
            std::vector<BankCardRange> selected;
            for (const BankCardRange &range: validRanges)
            {
                const bool overlaps = std::any_of(selected.begin(), selected.end(), [&range](const BankCardRange &kept)
                                                  { return range.start < kept.start + kept.length && kept.start < range.start + range.length; });
                if (!overlaps)
                {
                    selected.push_back(range);
                }
            }
            std::sort(selected.begin(), selected.end(), [](const BankCardRange &left, const BankCardRange &right) { return left.start < right.start; });
            return selected;
        }

        /**
         * @brief 拒绝邮箱形状里的残形
         * @param value 正则命中的邮箱候选
         * @return true 表示只有一个 @、没有连续点、首尾不是点
         */
        bool isValidEmail(const QString &value)
        {
            const qsizetype atIndex = value.indexOf(QLatin1Char('@'));
            if (atIndex <= 0 || atIndex != value.lastIndexOf(QLatin1Char('@')))
            {
                return false;
            }
            if (value.contains(QStringLiteral("..")) || value.startsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char('.')))
            {
                return false;
            }
            return !value.left(atIndex).endsWith(QLatin1Char('.'));
        }

        /**
         * @brief 逐段校验 IPv4 的 0-255 范围
         * @param value 四段数字候选
         * @return true 表示每段都是 0-255
         */
        bool isValidIpv4(const QString &value)
        {
            const QStringList octets = value.split(QLatin1Char('.'));
            if (octets.size() != 4)
            {
                return false;
            }
            for (const QString &octet: octets)
            {
                if (octet.isEmpty() || octet.size() > 3)
                {
                    return false;
                }
                bool      isNumber = false;
                const int number   = octet.toInt(&isNumber);
                if (!isNumber || number < 0 || number > 255)
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief 判断一段是否为一个 1-4 位十六进制组
         * @param group 冒号之间的一段
         * @return true 表示非空、不超 4 位且全是十六进制字符
         */
        bool isHexadecimalGroup(const QString &group)
        {
            if (group.isEmpty() || group.size() > 4)
            {
                return false;
            }
            for (const QChar character: group)
            {
                const char16_t code       = character.unicode();
                const bool     isDigit    = code >= u'0' && code <= u'9';
                const bool     isLowerHex = code >= u'a' && code <= u'f';
                const bool     isUpperHex = code >= u'A' && code <= u'F';
                if (!isDigit && !isLowerHex && !isUpperHex)
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief 校验压缩或完整的 IPv6 基本格式
         * @param value 冒号分段候选
         * @return true 表示无压缩时正好 8 组、有 :: 时不足 8 组且只有一个 ::
         * @note 要求候选里至少含一位数字：代码里的 std:: 这类作用域符号常被误切成年限近似形状。
         *       代价是 face:beef:cafe:dead:babe:feed:face:beef 这类全字母的合法地址不识别，属于已记录的漏检范围。
         */
        bool isValidIpv6(const QString &value)
        {
            const bool hasDigit = std::any_of(value.begin(), value.end(), [](QChar character) { return character >= QLatin1Char('0') && character <= QLatin1Char('9'); });
            if (!hasDigit)
            {
                return false;
            }

            const QStringList halves = value.split(QStringLiteral("::"));
            if (halves.size() > 2)
            {
                return false;
            }
            int groupCount = 0;
            for (const QString &half: halves)
            {
                if (half.isEmpty())
                {
                    continue;
                }
                const QStringList groups = half.split(QLatin1Char(':'));
                for (const QString &group: groups)
                {
                    if (!isHexadecimalGroup(group))
                    {
                        return false;
                    }
                    ++groupCount;
                }
            }
            if (halves.size() == 2)
            {
                return groupCount < 8;
            }
            return groupCount == 8;
        }

        /**
         * @brief 把框夹回图像范围
         * @param box 原框
         * @param imageSize 图像尺寸
         * @return 交集；完全越界或输入非法时为空框
         */
        QRect clipToImage(const QRect &box, QSize imageSize)
        {
            if (!box.isValid() || imageSize.width() <= 0 || imageSize.height() <= 0)
            {
                return {};
            }
            return box.intersected(QRect(QPoint(0, 0), imageSize));
        }

        /**
         * @brief 把命中的字符区间映射到词框并集
         * @param line 命中所在行
         * @param wordIndexByCharacter 逐字符的词归属
         * @param start 命中起点
         * @param length 命中长度
         * @return 覆盖命中的词框并集；任一被命中的词框不可用时退回整行框
         */
        QRect boxForMatch(const OcrLine &line, const QVector<qsizetype> &wordIndexByCharacter, qsizetype start, qsizetype length)
        {
            QRect           unionBox;
            bool            hasWordBox        = false;
            bool            hasInvalidWordBox = false;
            const qsizetype end               = std::min(start + length, wordIndexByCharacter.size());
            for (qsizetype characterIndex = std::max<qsizetype>(start, 0); characterIndex < end; ++characterIndex)
            {
                const qsizetype wordIndex = wordIndexByCharacter.at(characterIndex);
                if (wordIndex < 0 || wordIndex >= line.words.size())
                {
                    continue;
                }
                const QRect &wordBox = line.words.at(wordIndex).boundingBox;
                if (!wordBox.isValid())
                {
                    hasInvalidWordBox = true;
                    continue;
                }
                unionBox   = hasWordBox ? unionBox.united(wordBox) : wordBox;
                hasWordBox = true;
            }
            // 命中只要沾到一个没有可用词框的词，并集就会缺一段，必须整体退回整行框；
            // 一个词框都不可用时同样退回整行框
            return hasInvalidWordBox || !hasWordBox ? line.boundingBox : unionBox;
        }

        /**
         * @brief 用一条正则收集通过校验的命中
         * @tparam Validator 校验命中文本的可调用对象
         * @tparam MaskBuilder 生成脱敏样例的可调用对象
         * @param searchText 参与匹配的文本；数字型规则传归一化副本
         * @param originalText 原文，脱敏样例从这里取
         * @param pattern 匹配规则
         * @param kind 命中类型
         * @param isValid 校验函数
         * @param buildMaskedSample 脱敏函数
         * @param line 命中所在行
         * @param wordIndexByCharacter 逐字符的词归属
         * @param imageSize 图像尺寸
         * @param candidates 输出候选
         */
        template<typename Validator, typename MaskBuilder>
        void collectMatches(const QString &searchText, const QString &originalText, const QRegularExpression &pattern, PiiKind kind, Validator isValid,
                            MaskBuilder buildMaskedSample, const OcrLine &line, const QVector<qsizetype> &wordIndexByCharacter, QSize imageSize, std::vector<Candidate> &candidates)
        {
            QRegularExpressionMatchIterator iterator = pattern.globalMatch(searchText);
            while (iterator.hasNext())
            {
                const QRegularExpressionMatch match = iterator.next();
                if (!match.hasMatch() || match.capturedLength() <= 0)
                {
                    continue;
                }
                const QString value = match.captured(0);
                if (!isValid(value))
                {
                    continue;
                }
                const QRect box = clipToImage(boxForMatch(line, wordIndexByCharacter, match.capturedStart(), match.capturedLength()), imageSize);
                if (box.isEmpty())
                {
                    continue;
                }
                // 归一化副本与原文逐字符等长，捕获区间可以直接切回原文
                const QString originalValue = originalText.mid(match.capturedStart(), match.capturedLength());
                candidates.push_back(Candidate{kind, box, buildMaskedSample(originalValue)});
            }
        }

        /**
         * @brief 收集一行里能按银行卡收下的那一部分
         * @param searchText 数字型归一化副本
         * @param originalText 原文，脱敏样例从这里取
         * @param line 命中所在行
         * @param wordIndexByCharacter 逐字符的词归属
         * @param imageSize 图像尺寸
         * @param candidates 输出候选
         */
        void collectBankCardMatches(const QString &searchText, const QString &originalText, const OcrLine &line, const QVector<qsizetype> &wordIndexByCharacter, QSize imageSize,
                                    std::vector<Candidate> &candidates)
        {
            QRegularExpressionMatchIterator iterator = kBankCardRunPattern.globalMatch(searchText);
            while (iterator.hasNext())
            {
                const QRegularExpressionMatch run = iterator.next();
                if (!run.hasMatch() || run.capturedLength() <= 0)
                {
                    continue;
                }
                for (const BankCardRange &range: bankCardRanges(run.captured(0)))
                {
                    const qsizetype start = run.capturedStart() + range.start;
                    const QRect     box   = clipToImage(boxForMatch(line, wordIndexByCharacter, start, range.length), imageSize);
                    if (box.isEmpty())
                    {
                        continue;
                    }
                    candidates.push_back(Candidate{PiiKind::BankCard, box, maskBankCard(originalText.mid(start, range.length))});
                }
            }
        }

        /**
         * @brief 收集一行里的全部命中的类型
         * @param line 一行识别结果
         * @param imageSize 图像尺寸
         * @param candidates 输出候选
         */
        void collectLineCandidates(const OcrLine &line, QSize imageSize, std::vector<Candidate> &candidates)
        {
            const LineText lineText = buildLineText(line);
            if (lineText.text.isEmpty())
            {
                return;
            }
            const QString digitText = normalizeDigitLike(lineText.text);

            collectMatches(digitText, lineText.text, kPhonePattern, PiiKind::Phone, isValidPhone, maskPhone, line, lineText.wordIndexByCharacter, imageSize, candidates);
            collectMatches(digitText, lineText.text, kLandlinePattern, PiiKind::Landline, isValidLandline, maskLandline, line, lineText.wordIndexByCharacter, imageSize,
                           candidates);
            collectMatches(digitText, lineText.text, kIdCardPattern, PiiKind::IdCard, isValidIdCard, maskIdCard, line, lineText.wordIndexByCharacter, imageSize, candidates);
            collectBankCardMatches(digitText, lineText.text, line, lineText.wordIndexByCharacter, imageSize, candidates);
            // 邮箱用原文匹配：数字归一化会把 .com 里的 o 换成 0、把 local 部分的 l 换成 1，副本上再也匹配不到
            collectMatches(lineText.text, lineText.text, kEmailPattern, PiiKind::Email, isValidEmail, maskEmail, line, lineText.wordIndexByCharacter, imageSize, candidates);
            collectMatches(digitText, lineText.text, kIpv4Pattern, PiiKind::Ip, isValidIpv4, maskIpv4, line, lineText.wordIndexByCharacter, imageSize, candidates);
            collectMatches(digitText, lineText.text, kIpv6Pattern, PiiKind::Ip, isValidIpv6, maskIpv6, line, lineText.wordIndexByCharacter, imageSize, candidates);
        }

        /**
         * @brief 同面积时决定先保留谁：具体证件优先于可嵌在其中的号码形态
         * @param kind 命中类型
         * @return 数值越小越优先
         */
        int priorityOf(PiiKind kind)
        {
            switch (kind)
            {
                case PiiKind::IdCard:
                    return 0;
                case PiiKind::BankCard:
                    return 1;
                case PiiKind::Phone:
                    return 2;
                case PiiKind::Landline:
                    return 3;
                case PiiKind::Email:
                    return 4;
                case PiiKind::Ip:
                    return 5;
                case PiiKind::Barcode:
                    return 6;
            }
            return 7;
        }
    } // namespace

    std::vector<PiiItem> detectPii(const Core::OcrResult &ocr, const std::vector<QRect> &barcodeBoxes, QSize imageSize)
    {
        if (imageSize.width() <= 0 || imageSize.height() <= 0)
        {
            return {};
        }

        std::vector<Candidate> candidates;
        for (const OcrLine &line: ocr.lines)
        {
            collectLineCandidates(line, imageSize, candidates);
        }
        for (const QRect &barcodeBox: barcodeBoxes)
        {
            const QRect box = clipToImage(barcodeBox, imageSize);
            if (!box.isEmpty())
            {
                candidates.push_back(Candidate{PiiKind::Barcode, box, QStringLiteral("二维码/条形码")});
            }
        }

        // 大框先留下：身份证内部可能同时匹配出手机号形态，子串框不应再占一条
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const Candidate &left, const Candidate &right)
                         {
                             const qint64 leftArea  = static_cast<qint64>(left.box.width()) * left.box.height();
                             const qint64 rightArea = static_cast<qint64>(right.box.width()) * right.box.height();
                             if (leftArea != rightArea)
                             {
                                 return leftArea > rightArea;
                             }
                             return priorityOf(left.kind) < priorityOf(right.kind);
                         });

        std::vector<Candidate> kept;
        kept.reserve(candidates.size());
        for (const Candidate &candidate: candidates)
        {
            const bool isContained = std::any_of(kept.begin(), kept.end(), [&candidate](const Candidate &existing) { return existing.box.contains(candidate.box); });
            if (!isContained)
            {
                kept.push_back(candidate);
            }
        }

        std::vector<PiiItem> items;
        items.reserve(kept.size());
        for (const Candidate &candidate: kept)
        {
            items.push_back(PiiItem{candidate.kind, candidate.box, candidate.maskedSample});
        }

        // 审核清单按阅读顺序排：先上后下，同一行先左后右
        std::stable_sort(items.begin(), items.end(),
                         [](const PiiItem &left, const PiiItem &right)
                         {
                             if (left.physicalBox.top() != right.physicalBox.top())
                             {
                                 return left.physicalBox.top() < right.physicalBox.top();
                             }
                             if (left.physicalBox.left() != right.physicalBox.left())
                             {
                                 return left.physicalBox.left() < right.physicalBox.left();
                             }
                             return priorityOf(left.kind) < priorityOf(right.kind);
                         });
        return items;
    }

} // namespace GyanisScribe::Core::Privacy
