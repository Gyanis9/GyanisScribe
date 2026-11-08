#include "Core/Privacy/PiiDetector.h"

#include "Core/Ocr/OcrTypes.h"

#include <QRect>
#include <QSize>
#include <QString>

#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <vector>

namespace
{
    using GyanisScribe::Core::OcrLine;
    using GyanisScribe::Core::OcrResult;
    using GyanisScribe::Core::OcrWord;
    using GyanisScribe::Core::Privacy::detectPii;
    using GyanisScribe::Core::Privacy::PiiItem;
    using GyanisScribe::Core::Privacy::PiiKind;

    const QSize kFrameSize(800, 600);

    /// 用词与行框拼一行；不传行框时按有效词框取并集
    OcrLine makeLine(std::initializer_list<OcrWord> words, QRect lineBox = {})
    {
        OcrLine line;
        for (const OcrWord &word: words)
        {
            line.words.append(word);
        }
        if (lineBox.isValid())
        {
            line.boundingBox = lineBox;
        } else if (!line.words.isEmpty())
        {
            line.boundingBox = line.words.first().boundingBox;
            for (qsizetype index = 1; index < line.words.size(); ++index)
            {
                if (line.words.at(index).boundingBox.isValid())
                {
                    line.boundingBox = line.boundingBox.united(line.words.at(index).boundingBox);
                }
            }
        }
        return line;
    }

    /// 把若干行包成一次识别结果
    OcrResult makeResult(std::initializer_list<OcrLine> lines)
    {
        OcrResult result;
        for (const OcrLine &line: lines)
        {
            result.lines.append(line);
        }
        return result;
    }

    /// 按类型找第一条命中，找不到返回空指针
    const PiiItem *findItem(const std::vector<PiiItem> &items, PiiKind kind)
    {
        const auto iterator = std::find_if(items.begin(), items.end(), [kind](const PiiItem &item) { return item.kind == kind; });
        return iterator == items.end() ? nullptr : &(*iterator);
    }

    /// 数某类型命中了几条
    qsizetype countKind(const std::vector<PiiItem> &items, PiiKind kind)
    {
        return static_cast<qsizetype>(std::count_if(items.begin(), items.end(), [kind](const PiiItem &item) { return item.kind == kind; }));
    }
} // namespace

/**
 * @brief 钉住手机号的两种写法、+86 词框并集与统一脱敏样例
 */
TEST(PiiDetector, DetectsMobilePhoneWithCountryCodeAndSeparators)
{
    const OcrResult ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("13800138000"), QRect(20, 20, 160, 24)}}),
            makeLine({OcrWord{QStringLiteral("+86"), QRect(20, 60, 40, 24)}, OcrWord{QStringLiteral("138-0013-8000"), QRect(66, 60, 180, 24)}}, QRect(20, 60, 226, 24)),
    });

    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items.at(0).kind, PiiKind::Phone);
    EXPECT_EQ(items.at(0).physicalBox, QRect(20, 20, 160, 24));
    EXPECT_EQ(items.at(0).maskedSample, QStringLiteral("138****8000"));
    EXPECT_EQ(items.at(1).kind, PiiKind::Phone);
    EXPECT_EQ(items.at(1).physicalBox, QRect(20, 60, 226, 24)) << "命中跨越 +86 与号码两个词时必须取词框并集";
    EXPECT_EQ(items.at(1).maskedSample, QStringLiteral("138****8000"));
}

/**
 * @brief 钉住座机区号前导 0 的拒绝面，并保留短横线脱敏形状
 */
TEST(PiiDetector, DetectsLandlineAndRejectsAreaWithoutLeadingZero)
{
    const OcrResult ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("010-12345678"), QRect(20, 20, 180, 24)}}),
            makeLine({OcrWord{QStringLiteral("410-12345678"), QRect(20, 60, 180, 24)}}),
    });

    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items.at(0).kind, PiiKind::Landline);
    EXPECT_EQ(items.at(0).physicalBox, QRect(20, 20, 180, 24));
    EXPECT_EQ(items.at(0).maskedSample, QStringLiteral("010-****5678"));
    EXPECT_EQ(countKind(items, PiiKind::Landline), 1) << "410 开头没有区号前导 0，不得当成座机";
}

/**
 * @brief 钉住身份证校验位与出生日期的双重拒绝面
 * @details 无效日期那串的 GB 11643 校验位是正确的，只有 13 月 32 日不合法，因此红就只能来自日期校验。
 */
TEST(PiiDetector, AcceptsValidIdCardAndRejectsBadChecksumAndDate)
{
    const OcrResult ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("11010519491231002X"), QRect(20, 20, 220, 24)}}),
            makeLine({OcrWord{QStringLiteral("11010519491231003X"), QRect(20, 60, 220, 24)}}),
            makeLine({OcrWord{QStringLiteral("110105194913320027"), QRect(20, 100, 220, 24)}}),
    });

    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items.at(0).kind, PiiKind::IdCard);
    EXPECT_EQ(items.at(0).physicalBox, QRect(20, 20, 220, 24));
    EXPECT_EQ(items.at(0).maskedSample, QStringLiteral("110105********002X"));
    EXPECT_EQ(countKind(items, PiiKind::IdCard), 1);
}

/**
 * @brief 钉住银行卡的 Luhn 正例与仅一位之差的拒绝面
 */
TEST(PiiDetector, AcceptsOnlyLuhnValidBankCard)
{
    const OcrResult ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("4111111111111111"), QRect(20, 20, 220, 24)}}),
            makeLine({OcrWord{QStringLiteral("4111111111111112"), QRect(20, 60, 220, 24)}}),
    });

    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items.at(0).kind, PiiKind::BankCard);
    EXPECT_EQ(items.at(0).physicalBox, QRect(20, 20, 220, 24));
    EXPECT_EQ(items.at(0).maskedSample, QStringLiteral("4111********1111"));
}

/**
 * @brief 钉住邮箱识别，以及缺 TLD、连续点这类残缺串的拒绝面
 */
TEST(PiiDetector, DetectsEmailAndRejectsMalformedAddresses)
{
    const OcrResult ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("alice@example.com"), QRect(20, 20, 220, 24)}}),
            makeLine({OcrWord{QStringLiteral("alice@example"), QRect(20, 60, 220, 24)}}),
            makeLine({OcrWord{QStringLiteral("alice..bob@example.com"), QRect(20, 100, 260, 24)}}),
    });

    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items.at(0).kind, PiiKind::Email);
    EXPECT_EQ(items.at(0).physicalBox, QRect(20, 20, 220, 24));
    EXPECT_EQ(items.at(0).maskedSample, QStringLiteral("a***@example.com"));
}

/**
 * @brief 钉住 IPv4 逐段范围校验与带数字的 IPv6 基本格式
 */
TEST(PiiDetector, ValidatesIpv4OctetsAndBasicIpv6)
{
    const OcrResult ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("192.168.1.10"), QRect(20, 20, 180, 24)}}),
            makeLine({OcrWord{QStringLiteral("999.1.1.1"), QRect(20, 60, 160, 24)}}),
            makeLine({OcrWord{QStringLiteral("2001:db8::1"), QRect(20, 100, 200, 24)}}),
            makeLine({OcrWord{QStringLiteral("v1.2.3.4"), QRect(20, 140, 160, 24)}}),
            makeLine({OcrWord{QStringLiteral("std::vector"), QRect(20, 180, 180, 24)}}),
    });

    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 2U);
    ASSERT_EQ(countKind(items, PiiKind::Ip), 2);
    const PiiItem *ipv4 = findItem(items, PiiKind::Ip);
    ASSERT_NE(ipv4, nullptr);
    EXPECT_EQ(ipv4->physicalBox, QRect(20, 20, 180, 24));
    EXPECT_EQ(ipv4->maskedSample, QStringLiteral("192.168.***.***"));
    EXPECT_EQ(items.at(1).maskedSample, QStringLiteral("2001:db8:****")) << "v1.2.3.4 是版本号、std::vector 是代码片段，都不能算 IP";
}

/**
 * @brief 钉住 OCR 混淆只作用于数字型匹配，脱敏样例仍取原文
 */
TEST(PiiDetector, NormalizesOcrConfusionOnlyForDigitMatches)
{
    const OcrResult ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("l38OO138OOO"), QRect(20, 20, 180, 24)}}),
    });

    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items.at(0).kind, PiiKind::Phone);
    EXPECT_EQ(items.at(0).maskedSample, QStringLiteral("l38****8OOO")) << "样例应保留原文的字母，而不是换成归一化后的数字";
}

/**
 * @brief 钉住多词并集与词框不可用时的整行回退
 */
TEST(PiiDetector, UnionsWordBoxesAndFallsBackToLineBox)
{
    OcrLine split;
    split.words       = {OcrWord{QStringLiteral("138"), QRect(10, 10, 30, 20)}, OcrWord{QStringLiteral("0013"), QRect(50, 10, 40, 20)},
                         OcrWord{QStringLiteral("8000"), QRect(100, 10, 40, 20)}};
    split.boundingBox = QRect(10, 10, 130, 20);

    OcrLine fallback;
    fallback.words       = {OcrWord{QStringLiteral("13800138000"), QRect()}};
    fallback.boundingBox = QRect(10, 60, 200, 20);

    const OcrResult            ocr   = makeResult({split, fallback});
    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items.at(0).physicalBox, QRect(10, 10, 130, 20)) << "三个词框的并集应覆盖整段号码";
    EXPECT_EQ(items.at(1).physicalBox, QRect(10, 60, 200, 20)) << "词框无效时必须回退整行框";
}

/**
 * @brief 钉住二维码框无需 OCR 文本也能进清单，空框被忽略
 */
TEST(PiiDetector, ReturnsBarcodeBoxesAndIgnoresEmptyOnes)
{
    const std::vector<QRect> barcodeBoxes{QRect(50, 50, 120, 120), QRect()};

    const std::vector<PiiItem> items = detectPii(OcrResult{}, barcodeBoxes, kFrameSize);

    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items.at(0).kind, PiiKind::Barcode);
    EXPECT_EQ(items.at(0).physicalBox, QRect(50, 50, 120, 120));
    EXPECT_EQ(items.at(0).maskedSample, QStringLiteral("二维码/条形码"));
}

/**
 * @brief 钉住空结果与普通文本都不产生命中
 */
TEST(PiiDetector, ReturnsEmptyForEmptyInputAndPlainText)
{
    EXPECT_TRUE(detectPii(OcrResult{}, {}, kFrameSize).empty());

    const OcrResult plain = makeResult({
            makeLine({OcrWord{QStringLiteral("hello world"), QRect(10, 10, 120, 20)}}),
    });
    EXPECT_TRUE(detectPii(plain, {}, kFrameSize).empty());
}

/**
 * @brief 钉住词框与二维码框都被夹回图像尺寸
 */
TEST(PiiDetector, ClipsWordAndBarcodeBoxesToImageSize)
{
    const OcrResult          ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("13800138000"), QRect(90, 90, 40, 40)}}),
    });
    const std::vector<QRect> barcodeBoxes{QRect(-20, 80, 30, 40)};

    const std::vector<PiiItem> items = detectPii(ocr, barcodeBoxes, QSize(100, 100));

    ASSERT_EQ(items.size(), 2U);
    const PiiItem *barcode = findItem(items, PiiKind::Barcode);
    ASSERT_NE(barcode, nullptr);
    EXPECT_EQ(barcode->physicalBox, QRect(0, 80, 10, 20)) << "二维码框的左右与上下都越界，两个方向都要夹回";
    const PiiItem *phone = findItem(items, PiiKind::Phone);
    ASSERT_NE(phone, nullptr);
    EXPECT_EQ(phone->physicalBox, QRect(90, 90, 10, 10));
    EXPECT_TRUE(QRect(0, 0, 100, 100).contains(phone->physicalBox)) << "输出框不得越过图像边界";
}

/**
 * @brief 钉住身份证内嵌的手机号形态被更大框吸收，不重复列出
 */
TEST(PiiDetector, DropsPhoneCandidateContainedInIdCard)
{
    const OcrResult ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("11010519491231002X"), QRect(20, 20, 300, 24)}}),
    });

    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items.at(0).kind, PiiKind::IdCard);
    EXPECT_EQ(countKind(items, PiiKind::Phone), 0) << "身份证中间的 19491231002 是子串，不能单独列成手机号";
}

/**
 * @brief 钉住互不重叠的不同命中不会被去重误删
 */
TEST(PiiDetector, KeepsDistinctNonOverlappingMatches)
{
    const OcrResult ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("13800138000"), QRect(10, 10, 160, 24)}, OcrWord{QStringLiteral("alice@example.com"), QRect(220, 10, 220, 24)}}),
    });

    const std::vector<PiiItem> items = detectPii(ocr, {}, kFrameSize);

    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(countKind(items, PiiKind::Phone), 1);
    EXPECT_EQ(countKind(items, PiiKind::Email), 1);
}

/**
 * @brief 钉住所有类型的脱敏样例都不含完整敏感串
 */
TEST(PiiDetector, MaskedSamplesDoNotExposeFullSensitiveStrings)
{
    const OcrResult          ocr = makeResult({
            makeLine({OcrWord{QStringLiteral("13800138000"), QRect(10, 10, 160, 24)}}),
            makeLine({OcrWord{QStringLiteral("010-12345678"), QRect(10, 50, 180, 24)}}),
            makeLine({OcrWord{QStringLiteral("11010519491231002X"), QRect(10, 90, 220, 24)}}),
            makeLine({OcrWord{QStringLiteral("4111111111111111"), QRect(10, 130, 220, 24)}}),
            makeLine({OcrWord{QStringLiteral("alice@example.com"), QRect(10, 170, 220, 24)}}),
            makeLine({OcrWord{QStringLiteral("192.168.1.10"), QRect(10, 210, 180, 24)}}),
    });
    const std::vector<QRect> barcodeBoxes{QRect(10, 250, 120, 120)};

    const std::vector<PiiItem> items = detectPii(ocr, barcodeBoxes, kFrameSize);

    ASSERT_EQ(items.size(), 7U);
    const PiiItem *phone    = findItem(items, PiiKind::Phone);
    const PiiItem *landline = findItem(items, PiiKind::Landline);
    const PiiItem *idCard   = findItem(items, PiiKind::IdCard);
    const PiiItem *bankCard = findItem(items, PiiKind::BankCard);
    const PiiItem *email    = findItem(items, PiiKind::Email);
    const PiiItem *ip       = findItem(items, PiiKind::Ip);
    const PiiItem *barcode  = findItem(items, PiiKind::Barcode);
    ASSERT_NE(phone, nullptr);
    ASSERT_NE(landline, nullptr);
    ASSERT_NE(idCard, nullptr);
    ASSERT_NE(bankCard, nullptr);
    ASSERT_NE(email, nullptr);
    ASSERT_NE(ip, nullptr);
    ASSERT_NE(barcode, nullptr);

    EXPECT_FALSE(phone->maskedSample.contains(QStringLiteral("13800138000")));
    EXPECT_FALSE(landline->maskedSample.contains(QStringLiteral("12345678")));
    EXPECT_FALSE(idCard->maskedSample.contains(QStringLiteral("19491231")));
    EXPECT_FALSE(bankCard->maskedSample.contains(QStringLiteral("1111111111111111")));
    EXPECT_FALSE(email->maskedSample.contains(QStringLiteral("alice")));
    EXPECT_FALSE(ip->maskedSample.contains(QStringLiteral("1.10")));
    EXPECT_EQ(barcode->maskedSample, QStringLiteral("二维码/条形码"));

    EXPECT_TRUE(phone->maskedSample.contains(QLatin1Char('*')));
    EXPECT_TRUE(email->maskedSample.contains(QLatin1Char('*')));
    EXPECT_TRUE(ip->maskedSample.contains(QLatin1Char('*')));
}
