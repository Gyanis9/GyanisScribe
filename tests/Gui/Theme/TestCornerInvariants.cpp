#include <gtest/gtest.h>

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    /**
     * @brief 读一份文本文件
     * @param path 目标路径
     * @return 文件全部内容；读不到时返回空串
     */
    std::string readText(const std::filesystem::path &path)
    {
        std::ifstream      stream{path, std::ios::binary};
        std::ostringstream out;
        out << stream.rdbuf();
        return out.str();
    }

    /**
     * @brief 递归收集 src 下的 C++ 源文件
     * @return 源文件路径列表
     * @details 只扫 src：tests 里的 UiShotTool 模拟图、CheckBox 的 SVG 参照与用例自身的
     *          形状数据都是正当用途，不该被这条不变量卷进来。
     */
    std::vector<std::filesystem::path> sourceFiles()
    {
        std::vector<std::filesystem::path> files;
        const auto                         root = std::filesystem::path{GYANISSCRIBE_SOURCE_DIR} / "src";
        for (const auto &entry: std::filesystem::recursive_directory_iterator{root})
        {
            if (entry.is_regular_file() && (entry.path().extension() == ".cpp" || entry.path().extension() == ".h"))
            {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    /**
     * @brief 判断一行的首个非空白字符是否落在注释里
     * @param line 源文件的一行
     * @return 以双斜线、块注释开头或以星号续行时为真
     */
    bool isCommentLine(const std::string &line)
    {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
        {
            return false;
        }
        return line.compare(first, 2, "//") == 0 || line.compare(first, 2, "/*") == 0 || line[first] == '*';
    }

    /**
     * @brief 查找一个「裸控件构造」片段，要求它后面不再接标识符字符
     * @param text 源文件原文
     * @param token 片段，如 new QListWidget
     * @return 命中位置；边界不符的命中继续往后找
     * @details new QListWidgetItem 是列表模型项，不是控件构造；按标识符边界排除它，
     *          不把合法用法当成迁移漏项。
     */
    std::optional<std::size_t> findRawConstruction(const std::string &text, const std::string_view token)
    {
        for (auto at = text.find(token); at != std::string::npos; at = text.find(token, at + 1))
        {
            const auto after = at + token.size();
            if (after >= text.size())
            {
                return at;
            }
            const auto next = static_cast<unsigned char>(text[after]);
            if (!std::isalnum(next) && next != '_')
            {
                return at;
            }
        }
        return std::nullopt;
    }

    /// 去掉样式表里的块注释：选择器与规则的切分不能把注释里的花括号或词算进去
    std::string withoutComments(const std::string &text)
    {
        std::string result;
        result.reserve(text.size());
        for (std::size_t index = 0; index < text.size();)
        {
            if (text.compare(index, 2, "/*") == 0)
            {
                const auto end = text.find("*/", index + 2);
                if (end == std::string::npos)
                {
                    break;
                }
                index = end + 2;
                continue;
            }
            result.push_back(text[index]);
            ++index;
        }
        return result;
    }

    std::string trim(const std::string &text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return {};
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    struct StyleRule
    {
        std::string selector;
        std::string body;
    };

    /**
     * @brief 把样式表拆成「选择器 + 规则体」
     * @param text 去掉注释后的样式表原文
     * @return 按出现顺序排列的规则
     */
    std::vector<StyleRule> rulesIn(const std::string &text)
    {
        std::vector<StyleRule> rules;
        std::size_t            cursor = 0;
        while (true)
        {
            const auto open = text.find('{', cursor);
            if (open == std::string::npos)
            {
                break;
            }
            const auto close = text.find('}', open);
            if (close == std::string::npos)
            {
                break;
            }
            const auto previousClose = text.rfind('}', open);
            const auto selectorStart = previousClose == std::string::npos ? 0 : previousClose + 1;
            rules.push_back({trim(text.substr(selectorStart, open - selectorStart)), text.substr(open + 1, close - open - 1)});
            cursor = close + 1;
        }
        return rules;
    }

    /**
     * @brief 取一个选择器的规则体
     * @param text 样式表原文
     * @param selector 选择器全文
     * @return 花括号内的文本；找不到选择器或花括号不配对时为空
     */
    std::string blockOf(const std::string &text, const std::string_view selector)
    {
        const auto at = text.find(selector);
        if (at == std::string::npos)
        {
            return {};
        }
        const auto open = text.find('{', at + selector.size());
        if (open == std::string::npos)
        {
            return {};
        }
        const auto close = text.find('}', open);
        if (close == std::string::npos)
        {
            return {};
        }
        return text.substr(open + 1, close - open - 1);
    }

    std::vector<std::string> styleSheetTexts()
    {
        std::vector<std::string> sheets;
        const auto               styles = std::filesystem::path{GYANISSCRIBE_SOURCE_DIR} / "src/Gui/Resources/Styles";
        for (const auto *name: {"Light.qss", "Dark.qss", "HighContrast.qss"})
        {
            sheets.push_back(readText(styles / name));
        }
        return sheets;
    }
} // namespace

TEST(CornerInvariants, StyleSheetsHaveNoBorderRadius)
{
    const auto styles = std::filesystem::path{GYANISSCRIBE_SOURCE_DIR} / "src/Gui/Resources/Styles";
    for (const auto *name: {"Light.qss", "Dark.qss", "HighContrast.qss"})
    {
        const auto text = readText(styles / name);
        ASSERT_FALSE(text.empty()) << name;
        EXPECT_EQ(text.find("border-radius"), std::string::npos) << name;
    }
}

TEST(CornerInvariants, SourceHasNoRawShapeWidgetConstruction)
{
    constexpr std::array patterns = {"new QPushButton", "new QToolButton", "new QLineEdit", "new QComboBox",  "new QFontComboBox", "new QTextEdit",
                                     "new QListWidget", "new QMenu",       "new QSlider",   "new QScrollBar", "new QCheckBox",     "new QSpinBox"};
    for (const auto &file: sourceFiles())
    {
        const auto text = readText(file);
        for (const auto *pattern: patterns)
        {
            EXPECT_FALSE(findRawConstruction(text, pattern).has_value()) << file.string() << " 仍在用 " << pattern;
        }
    }
}

TEST(CornerInvariants, SourceHasNoRoundedRectDrawingOrInlineRadii)
{
    for (const auto &file: sourceFiles())
    {
        std::istringstream lines{readText(file)};
        std::string        line;
        int                number = 0;
        while (std::getline(lines, line))
        {
            ++number;
            if (isCommentLine(line))
            {
                continue;
            }
            EXPECT_EQ(line.find("drawRoundedRect"), std::string::npos) << file.string() << ':' << number;
            EXPECT_EQ(line.find("addRoundedRect"), std::string::npos) << file.string() << ':' << number;
            EXPECT_EQ(line.find("border-radius"), std::string::npos) << file.string() << ':' << number;
        }
    }
}

/**
 * @brief 钉住工具提示的样式表规则不带任何边框声明
 * @details 提示弹层没有自绘类，面板与描边由 SquircleStyle 在 PE_PanelTipLabel 里画；
 *          样式表只要声明了边框（哪怕是透明的），样式表引擎就会自己画完这一层，
 *          代理风格收不到调用，圆角与描边一起丢。规则必须非空，否则「没写 border」
 *          只是因为整块都被人删掉了。
 */
TEST(CornerInvariants, TooltipRuleKeepsNoBorderDeclaration)
{
    for (const auto &text: styleSheetTexts())
    {
        ASSERT_FALSE(text.empty());
        const auto block = blockOf(text, "QToolTip");
        ASSERT_FALSE(block.empty()) << "样式表里找不到 QToolTip 规则";
        EXPECT_NE(block.find("color"), std::string::npos) << "QToolTip 规则丢了文字色";
        EXPECT_NE(block.find("padding"), std::string::npos) << "QToolTip 规则丢了内边距";
        EXPECT_EQ(block.find("border"), std::string::npos) << "QToolTip 规则出现边框声明，代理风格收不到 PE_PanelTipLabel";
    }
}

/**
 * @brief 钉住输入类的透明底只落在自绘控件上
 * @details 原生对话窗（取色、文本输入、文件对话框）里的 QLineEdit / QComboBox / QSpinBox
 *          也匹配类型选择器；透明底加透明边落在它们身上就是「没有底的输入框」。
 *          规则统一用 [squircle="true"] 限定到 Gui::LineEdit / Gui::ComboBox / Gui::SpinBox，
 *          微调框与可编辑下拉框内部的行编辑走同一属性的祖先选择器。
 */
TEST(CornerInvariants, InputTransparencyStaysScopedToTheSquircleClasses)
{
    for (const auto &text: styleSheetTexts())
    {
        for (const auto &rule: rulesIn(withoutComments(text)))
        {
            if (rule.body.find("background: transparent") == std::string::npos)
            {
                continue;
            }
            for (const auto *type: {"QLineEdit", "QComboBox", "QSpinBox"})
            {
                if (rule.selector.find(type) == std::string::npos)
                {
                    continue;
                }
                EXPECT_NE(rule.selector.find("squircle"), std::string::npos) << "输入类的透明底规则没限定到自绘控件：" << rule.selector;
            }
        }
    }
}
