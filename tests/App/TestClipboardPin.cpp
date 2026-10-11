// 剪贴板转贴图的判据：图像原样交回，纯文本与颜色各自渲染成卡片。
// 渲染要字体库，因此这一组跟主窗口那几组共用一个可执行体（那里立着 QApplication），
// 不再放在没有应用对象的 TestShell 里。

#include "App/ClipboardPin.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QMimeData>
#include <QSize>
#include <QString>
#include <QVariant>

#include <array>
#include <memory>
#include <string>

using GyanisScribe::App::kColorCardStripHeight;
using GyanisScribe::App::kMaximumTextCardCharacters;
using GyanisScribe::App::kTextCardMaximumWidth;
using GyanisScribe::App::pinContentFrom;
using GyanisScribe::App::renderColorCard;
using GyanisScribe::App::renderTextCard;
using GyanisScribe::Core::ErrorCode;

namespace
{
    std::unique_ptr<QApplication> application;

    class ClipboardPinFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestClipboardPin";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        [[nodiscard]] static QFont cardFont()
        {
            QFont font;
            font.setPointSize(12);
            return font;
        }

        /// 卡片上有没有画上东西：取整图里与底色不同的像素数
        [[nodiscard]] static int inkPixelCount(const QImage &card, const QColor &background)
        {
            int count = 0;
            for (int y = 0; y < card.height(); ++y)
            {
                for (int x = 0; x < card.width(); ++x)
                {
                    if (card.pixelColor(x, y) != background)
                    {
                        ++count;
                    }
                }
            }
            return count;
        }
    };
} // namespace

/**
 * @brief 钉住图像那一路原样交回，不经过任何卡片渲染
 */
TEST_F(ClipboardPinFixture, HandsBackTheClipboardImageUntouched)
{
    QImage source{QSize{40, 20}, QImage::Format_ARGB32};
    source.fill(QColor{10, 120, 200});

    QMimeData data;
    data.setImageData(source);

    const auto image = pinContentFrom(&data, cardFont());
    ASSERT_TRUE(image.has_value()) << image.error().message;
    EXPECT_EQ(image->size(), source.size());
    EXPECT_EQ(image->pixelColor(5, 5), source.pixelColor(5, 5)) << "交回的该是原样那张图";
}

/**
 * @brief 钉住纯文本渲染成卡片：有字、宽度封顶、文本越长卡越高
 */
TEST_F(ClipboardPinFixture, RendersPlainTextIntoACard)
{
    QMimeData data;
    data.setText(QStringLiteral("第一行\n第二行"));

    const auto card = pinContentFrom(&data, cardFont());
    ASSERT_TRUE(card.has_value()) << card.error().message;
    EXPECT_GT(card->width(), 0);
    EXPECT_GT(card->height(), 0);
    EXPECT_GT(inkPixelCount(*card, QColor(252, 252, 252)), 0) << "卡片上一个字都没画上";

    const auto oneLine  = renderTextCard(QStringLiteral("一行"), cardFont());
    const auto manyLine = renderTextCard(QStringLiteral("一行\n两行\n三行\n四行"), cardFont());
    EXPECT_GT(manyLine.height(), oneLine.height()) << "行数变多，卡片该跟着变高";

    const auto longText = renderTextCard(QString(400, QLatin1Char('x')), cardFont());
    EXPECT_LE(longText.width(), kTextCardMaximumWidth) << "长文本该折行，而不是把卡片拉宽到没边";
}

/**
 * @brief 钉住颜色文本渲染成色卡：色块取到那一种颜色，底部是写法条
 * @details 只认 # / rgb( / hsl( 开头的短单行——QColor::fromString 连 red 这种单词都认，
 *          全放开会把普通英文里的颜色词渲染成色卡。
 */
TEST_F(ClipboardPinFixture, RendersColorTextIntoASwatchCard)
{
    QMimeData data;
    data.setText(QStringLiteral("#FF8000"));

    const auto card = pinContentFrom(&data, cardFont());
    ASSERT_TRUE(card.has_value()) << card.error().message;
    EXPECT_EQ(card->size(), QSize(160, 100));
    EXPECT_EQ(card->pixelColor(80, 10), QColor(255, 128, 0)) << "上半块该是那个颜色本身";
    EXPECT_NE(card->pixelColor(80, card->height() - kColorCardStripHeight / 2), QColor(255, 128, 0)) << "底部该是写法条，不是色块";

    // 显式类型的数组而不是花括号列表：QStringLiteral 展开成 lambda，列表推不出元素类型
    const std::array<QString, 3> notAColors{QStringLiteral("red"), QStringLiteral("orange juice"), QStringLiteral("这段话说的是颜色")};
    for (const auto &notAColor: notAColors)
    {
        QMimeData text;
        text.setText(notAColor);
        const auto asText = pinContentFrom(&text, cardFont());
        ASSERT_TRUE(asText.has_value()) << notAColor.toStdString();
        EXPECT_NE(asText->size(), QSize(160, 100)) << notAColor.toStdString() << " 被当成了颜色卡片";
    }
}

/**
 * @brief 钉住三条拒因：没有内容、空白文本、文本太长
 * @details 每条都要给替代做法：入口在托盘上，附近没有输入框能把话说清楚。
 */
TEST_F(ClipboardPinFixture, RejectsWithoutContentAndSaysWhatToDo)
{
    const auto nothing = pinContentFrom(nullptr, cardFont());
    ASSERT_FALSE(nothing.has_value());
    EXPECT_EQ(nothing.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(nothing.error().message.find("图像、纯文本或颜色"), std::string::npos) << "要说清这一条认哪些内容";

    QMimeData blank;
    blank.setText(QStringLiteral("   \n  "));
    const auto empty = pinContentFrom(&blank, cardFont());
    ASSERT_FALSE(empty.has_value());
    EXPECT_NE(empty.error().message.find("空"), std::string::npos);

    QMimeData huge;
    huge.setText(QString(kMaximumTextCardCharacters + 1, QLatin1Char('x')));
    const auto tooLong = pinContentFrom(&huge, cardFont());
    ASSERT_FALSE(tooLong.has_value()) << "超长文本该拒，而不是渲染一张几千像素高的卡片";
    EXPECT_NE(tooLong.error().message.find("太长"), std::string::npos);
    EXPECT_NE(tooLong.error().message.find("截短"), std::string::npos) << "要给出替代做法";

    QMimeData shell;
    shell.setImageData(QVariant{});
    shell.setText(QStringLiteral("挂在图像名目下的空壳"));
    EXPECT_FALSE(pinContentFrom(&shell, cardFont()).has_value()) << "挂了图像名目却没给像素，不许当成文本卡片兜底";
}

/**
 * @brief 钉住色卡渲染函数本身：色块与写法条各在各的位置
 */
TEST_F(ClipboardPinFixture, ColorCardKeepsTheSwatchAboveTheLabelStrip)
{
    const auto card = renderColorCard(QColor{12, 34, 56}, QStringLiteral("#0C2238"));
    ASSERT_FALSE(card.isNull());
    EXPECT_EQ(card.pixelColor(80, 10), QColor(12, 34, 56));
    EXPECT_NE(card.pixelColor(80, card.height() - 4), QColor(12, 34, 56));
}
