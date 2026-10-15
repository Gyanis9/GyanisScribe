// 剪贴板转贴图的判据：图像原样交回，纯文本与颜色各自渲染成卡片。
// 渲染要字体库，因此这一组跟主窗口那几组共用一个可执行体（那里立着 QApplication），
// 不再放在没有应用对象的 TestShell 里。

#include "App/ClipboardPin.h"
#include "Pin/ColorCard.h"

#include "PinTestAssets.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QMimeData>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using GyanisScribe::App::dropContentFrom;
using GyanisScribe::Pin::kColorCardHeight;
using GyanisScribe::Pin::kColorCardStripHeight;
using GyanisScribe::Pin::kColorCardWidth;
using GyanisScribe::App::kMaximumTextCardCharacters;
using GyanisScribe::App::kTextCardMaximumWidth;
using GyanisScribe::App::kTextCardMinimumWidth;
using GyanisScribe::App::colorCardColorOf;
using GyanisScribe::App::pinContentFrom;
using GyanisScribe::Pin::renderColorCard;
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

/**
 * @brief 钉住拖来的文件：图片按盘上的像素读出、GIF 连来源路径一起带走
 * @details 来源路径不带走的话，贴上去是一张静止的首帧；带走之后与布局恢复那一路同解，
 *          窗口自己会把动画接上。两件按拖进来的顺序一一对应。
 */
TEST_F(ClipboardPinFixture, DropReadsImageFilesInOrderAndKeepsAnimationSource)
{
    const auto directory = std::filesystem::temp_directory_path();
    const auto pngPath   = directory / "GyanisScribe-DropPin-red.png";
    QImage     red{QSize{8, 8}, QImage::Format_ARGB32};
    red.fill(QColor{255, 0, 0});
    ASSERT_TRUE(red.save(QString::fromStdString(pngPath.string())));

    const auto gifPath = GyanisScribe::PinTestAssets::writeTwoFrameGif(directory / "GyanisScribe-DropPin-two-frames.gif");
    ASSERT_FALSE(gifPath.isEmpty()) << "素材没落到临时文件上";

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString(pngPath.string())), QUrl::fromLocalFile(gifPath)});

    const auto dropped = dropContentFrom(&mime, cardFont());
    ASSERT_TRUE(dropped.has_value()) << dropped.error().message;
    ASSERT_EQ(dropped->contents.size(), 2U) << "拖进来两张该渲染出两份";
    EXPECT_EQ(dropped->contents[0].image.pixelColor(4, 4), QColor(255, 0, 0)) << "PNG 没按盘上的像素读出来";
    EXPECT_TRUE(dropped->contents[0].animatedSource.isEmpty()) << "静图不该记动图来源";
    EXPECT_TRUE(dropped->contents[1].animatedSource.endsWith(QStringLiteral("two-frames.gif"))) << "GIF 没把来源路径一起带走，贴上去不会播";
    EXPECT_TRUE(dropped->rejectedNames.isEmpty());
}

/**
 * @brief 钉住链接与杂文件：链接渲染成卡片，非图片的名字记下来交给调用方提示
 * @details 一件不是图片不拦整次拖放：能贴的照贴，名字记进 rejectedNames——
 *          静默丢掉一件比整次拒绝更让人困惑。
 */
TEST_F(ClipboardPinFixture, DropRendersLinksAsCardsAndRecordsNonImages)
{
    const auto directory = std::filesystem::temp_directory_path();
    const auto txtPath   = directory / "GyanisScribe-DropPin-notes.txt";
    {
        std::ofstream out{txtPath, std::ios::binary | std::ios::trunc};
        ASSERT_TRUE(out.good());
        out << "not an image";
    }

    QMimeData mime;
    mime.setUrls({QUrl{QStringLiteral("https://example.com/page")}, QUrl::fromLocalFile(QString::fromStdString(txtPath.string()))});

    const auto dropped = dropContentFrom(&mime, cardFont());
    ASSERT_TRUE(dropped.has_value()) << dropped.error().message;
    ASSERT_EQ(dropped->contents.size(), 1U) << "链接该渲染成一张卡片";
    EXPECT_GE(dropped->contents.front().image.width(), kTextCardMinimumWidth) << "链接卡片宽度退化了";
    ASSERT_EQ(dropped->rejectedNames.size(), 1U) << "非图片的名字没记下来，用户会以为它被贴了";
    EXPECT_EQ(dropped->rejectedNames.front(), QStringLiteral("GyanisScribe-DropPin-notes.txt"));
}

/**
 * @brief 钉住没有文件时退回剪贴板那套单件内容：颜色写法仍走颜色卡
 */
TEST_F(ClipboardPinFixture, DropWithoutFilesFallsBackToTheSingleContentPath)
{
    QMimeData mime;
    mime.setText(QStringLiteral("#3C78C8"));

    const auto dropped = dropContentFrom(&mime, cardFont());
    ASSERT_TRUE(dropped.has_value()) << dropped.error().message;
    ASSERT_EQ(dropped->contents.size(), 1U);
    EXPECT_EQ(dropped->contents.front().image.size(), QSize(kColorCardWidth, kColorCardHeight)) << "颜色写法没走颜色卡那一档";
}

/**
 * @brief 钉住全是非图片时说清是哪个文件不行
 */
TEST_F(ClipboardPinFixture, DropOfOnlyNonImagesSaysWhatCameIn)
{
    const auto directory = std::filesystem::temp_directory_path();
    const auto txtPath   = directory / "GyanisScribe-DropPin-only.txt";
    {
        std::ofstream out{txtPath, std::ios::binary | std::ios::trunc};
        ASSERT_TRUE(out.good());
        out << "plain";
    }

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString(txtPath.string()))});

    const auto dropped = dropContentFrom(&mime, cardFont());
    ASSERT_FALSE(dropped.has_value()) << "全是非图片也当成贴得成，画布上会凭空冒出东西";
    EXPECT_NE(dropped.error().message.find("GyanisScribe-DropPin-only.txt"), std::string::npos) << "要说清是哪个文件不行";
}

/**
 * @brief 钉住「这一段是不是颜色写法」：三种形状各认一次，普通单词与多行不认
 * @details 这条判据同时是色卡身份的来源，两种坏法都打在这里：把 `rgb()`/`hsl()` 当成认不了
 *          （前缀白名单里写着它们，而 `QColor::fromString` 只认 `#` 与颜色名——不自己拆就是死代码，
 *          粘进去的会是一张文本卡），以及把「red」这类普通单词当颜色贴出来。
 */
TEST(ClipboardPinColorText, OnlyTheThreeColorFormsCount)
{
    const auto detection = [](const QString &text)
    {
        QMimeData data;
        data.setText(text);
        return colorCardColorOf(&data);
    };

    const auto hex = detection(QStringLiteral("#FF8000"));
    ASSERT_TRUE(hex.has_value()) << "`#` 那一形是最常用的一种，认不出就等于没有色卡";
    EXPECT_EQ(*hex, QColor(0xFF, 0x80, 0x00));

    const auto rgb = detection(QStringLiteral("rgb(255, 136, 0)"));
    ASSERT_TRUE(rgb.has_value()) << "rgb() 那一形要认：白名单里写着它，走不到就是死代码";
    EXPECT_EQ(*rgb, QColor(255, 136, 0));

    const auto hsl = detection(QStringLiteral("  hsl(30, 100%, 50%)  "));
    ASSERT_TRUE(hsl.has_value()) << "hsl() 那一形要认（首尾空白先去过）";
    EXPECT_EQ(hsl->hue(), 30) << "色相那一格换算错了，贴出来的是另一个色";

    EXPECT_FALSE(detection(QStringLiteral("rgb(300, 0, 0)")).has_value()) << "越界的通道值不该夹取：夹出来的不是用户粘的那个色";
    EXPECT_FALSE(detection(QStringLiteral("rgb(50%, 0, 0)")).has_value()) << "rgb 里带百分号不是这一套写法";
    EXPECT_FALSE(detection(QStringLiteral("hsl(30, 50, 50)")).has_value()) << "hsl 缺百分号也不认：多一种写法就多一条要守的口径";
    EXPECT_FALSE(detection(QStringLiteral("red")).has_value()) << "普通单词不该当颜色贴出来";
    const auto multiLine = QStringLiteral("第一行") + QLatin1Char('\n') + QStringLiteral("#FF8000");
    EXPECT_FALSE(detection(multiLine).has_value()) << "多行不该当颜色";
    EXPECT_FALSE(colorCardColorOf(nullptr).has_value()) << "空载荷不该崩也不该猜";

    QMimeData imageOnly;
    imageOnly.setImageData(QImage{4, 4, QImage::Format_ARGB32});
    EXPECT_FALSE(colorCardColorOf(&imageOnly).has_value()) << "只有图像时不该报出一个颜色";
}
