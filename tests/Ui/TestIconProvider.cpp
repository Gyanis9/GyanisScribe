#include "Ui/IconProvider.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QSize>

#include <array>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using GyanisScribe::Ui::IconProvider;
using GyanisScribe::Ui::IconSize;

namespace
{
    // 需求第七节点名的图标清单：这一份是抄来的独立期望值，不从资源目录反推，
    // 否则删掉一枚 SVG 之后用例只会跟着一起改口，就再也抓不住它了
    constexpr std::array<std::string_view, 56> kRequiredIcons = {
            "App",
            "AppIcon",
            "Tray",
            "Capture",
            "CaptureFullScreen",
            "CaptureRegion",
            "CaptureWindow",
            "CaptureLastRegion",
            "Pin",
            "PinTop",
            "PinTransparent",
            "PinMouseThrough",
            "PinClose",
            "PinCopy",
            "PinSave",
            "Record",
            "RecordStart",
            "RecordPause",
            "RecordStop",
            "RecordFullScreen",
            "RecordRegion",
            "RecordWindow",
            "Microphone",
            "SystemAudio",
            "CursorHighlight",
            "Countdown",
            "Ocr",
            "OcrRegion",
            "OcrImage",
            "OcrCopy",
            "OcrExport",
            "OcrLanguage",
            "Annotation",
            "Rectangle",
            "Ellipse",
            "Arrow",
            "Pen",
            "Highlight",
            "Mosaic",
            "Text",
            "Number",
            "Undo",
            "Redo",
            "Copy",
            "Save",
            "Close",
            "Settings",
            "Hotkey",
            "History",
            "ColorPicker",
            "ThemeLight",
            "ThemeDark",
            "ThemeSystem",
            "Exit",
            "About",
            "Refresh",
    };

    /// 尺寸阶梯的四档，按从小到大排列，断言里按索引取
    constexpr std::array kLadderSizes = {IconSize::Small, IconSize::Medium, IconSize::Large, IconSize::ExtraLarge};

    /// 阶梯对应的逻辑像素数，与 IconSize 的枚举值同解，这条用例把它钉住
    constexpr std::array kLadderPixels = {16, 20, 24, 32};

    std::unique_ptr<QApplication> application;

    /**
     * @brief 统计位图里被画到的像素数
     * @details alpha 非零即算「画到了」：SVG 全是透明底上的线条，这个计数为 0 就说明渲染器什么都没画出来
     */
    int paintedPixelCount(const QPixmap &bitmap)
    {
        const auto image = bitmap.toImage().convertToFormat(QImage::Format_ARGB32);
        int        painted{};
        for (int y = 0; y < image.height(); ++y)
        {
            const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
            {
                if (qAlpha(line[x]) != 0)
                {
                    ++painted;
                }
            }
        }
        return painted;
    }

    /// 位图里是否出现过这个颜色（各通道允许 ±2 的整数舍入差）：替换 currentColor 的效果只能按颜色验
    bool containsColor(const QPixmap &bitmap, const QColor &expected, int tolerance = 2)
    {
        const auto image = bitmap.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y)
        {
            const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
            {
                if (qAlpha(line[x]) == 0)
                {
                    continue;
                }
                const auto difference = std::abs(qRed(line[x]) - expected.red()) + std::abs(qGreen(line[x]) - expected.green()) + std::abs(qBlue(line[x]) - expected.blue());
                if (difference <= tolerance)
                {
                    return true;
                }
            }
        }
        return false;
    }

    /// 读一枚 SVG 的原始源码，供契约用例直接检查文本
    QString readIconSource(std::string_view name)
    {
        QFile file(QStringLiteral(":/icons/%1.svg").arg(QString::fromStdString(std::string{name})));
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }

    class IconProviderFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // QPixmap 必须有 GUI 应用对象才画得出来，QCoreApplication 不够
            int   argumentCount  = 1;
            char  argumentZero[] = "TestIconProvider";
            char *arguments[]    = {argumentZero, nullptr};
            application          = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 用例统一用非黑非蓝的绿：一旦 currentColor 没被替换，Qt 的默认描边是黑，判据就分得开
            m_provider.setTintColor(QColor(0, 200, 0));
        }

        [[nodiscard]] const IconProvider &provider() const
        {
            return m_provider;
        }

    private:
        IconProvider m_provider; ///< 被测供给器
    };

} // namespace

/**
 * @brief 钉住「资源里真实存在的每一枚都能画出来，且四档尺寸都齐」
 * @details 这条同时兜住三种真实故障：qrc 没登记（文件在仓库里但资源树是空的）、
 *          SVG 写法落在 Qt 渲染器子集之外（isValid 为假）、某档尺寸算错了像素。
 */
TEST_F(IconProviderFixture, EveryBundledIconPaintsAtEveryLadderSize)
{
    const auto names = provider().availableIconNames();
    ASSERT_GE(names.size(), kRequiredIcons.size()) << "资源目录里的图标比需求清单还少，说明有 SVG 或 qrc 条目丢了";

    for (const auto &name: names)
    {
        const auto stdName = name.toStdString();
        for (std::size_t index = 0; index < kLadderSizes.size(); ++index)
        {
            const auto bitmap = provider().pixmap(stdName, kLadderSizes[index]);
            ASSERT_FALSE(bitmap.isNull()) << name.toStdString() << " 在 " << kLadderPixels[index] << " 档画不出位图";
            EXPECT_EQ(bitmap.size(), QSize(kLadderPixels[index], kLadderPixels[index]))
                    << name.toStdString() << " 的 " << kLadderPixels[index] << " 档尺寸不对，逻辑像素必须正好等于档位";
            EXPECT_GT(paintedPixelCount(bitmap), 0) << name.toStdString() << " 画出来是全透明：渲染器接了源文件但一条线都没画";
        }

        // 多档齐全才谈得上「QIcon 自己按按钮尺寸挑」
        const auto icon = provider().icon(stdName);
        ASSERT_FALSE(icon.isNull()) << name.toStdString() << " 取不到图标";
        EXPECT_GE(icon.availableSizes().size(), kLadderSizes.size()) << name.toStdString() << " 只备齐了 " << icon.availableSizes().size() << " 档，阶梯是四档";
    }
}

/**
 * @brief 钉住需求清单逐名到位
 * @details 名字是调用方写的字符串，拼错或漏放只在运行时才看得见；这条把它提前到构建后的门禁里。
 */
TEST_F(IconProviderFixture, RequiredIconNamesAreAllPresent)
{
    const auto available = provider().availableIconNames();

    for (const auto required: kRequiredIcons)
    {
        EXPECT_TRUE(available.contains(QString::fromStdString(std::string{required}))) << "需求第七节点名的 " << required << " 不在资源里";
    }
}

/**
 * @brief 钉住 SVG 源码本身符合第七节的硬约束
 * @details 检查的是文本而不是渲染结果：viewBox、currentColor、线宽、原创且无外部引用，
 *          这几条渲染得再好也说明不了，必须逐份读源码。
 */
TEST_F(IconProviderFixture, BundledSourcesFollowTheSvgContract)
{
    for (const auto &name: provider().availableIconNames())
    {
        const auto source = readIconSource(name.toStdString());
        ASSERT_FALSE(source.isEmpty()) << name.toStdString() << " 的资源是空的";

        EXPECT_NE(source.indexOf(QStringLiteral(R"(viewBox="0 0 24 24")")), -1) << name.toStdString() << " 的 viewBox 不是 0 0 24 24";
        EXPECT_NE(source.indexOf(QStringLiteral("currentColor")), -1) << name.toStdString() << " 没用 currentColor，换主题时不会跟着变色";
        EXPECT_NE(source.indexOf(QStringLiteral(R"(stroke-linecap="round")")), -1) << name.toStdString() << " 端点不是圆角，风格不统一";
        EXPECT_NE(source.indexOf(QStringLiteral(R"(stroke-linejoin="round")")), -1) << name.toStdString() << " 连接不是圆角，风格不统一";
        EXPECT_NE(source.indexOf(QStringLiteral(R"(stroke-width="1.5")")), -1) << name.toStdString() << " 线宽不在 1.5 / 2 这两档上";

        // 需求禁止脚本、外部图片与 base64：出现任一关键字就不是「原创 SVG 源码」了
        EXPECT_EQ(source.indexOf(QStringLiteral("<script")), -1) << name.toStdString() << " 里出现了脚本";
        EXPECT_EQ(source.indexOf(QStringLiteral("base64")), -1) << name.toStdString() << " 里出现了 base64";
        EXPECT_EQ(source.indexOf(QStringLiteral("http://www.w3.org/1999/xlink")), -1) << name.toStdString() << " 引用了外部图像";
    }
}

/**
 * @brief 钉住 currentColor 真的被换成了主题色
 * @details 这条是「同解」判据：没替换时 Qt 会画成默认黑，所以断言的是「出现主题色、且不出现黑」。
 */
TEST_F(IconProviderFixture, TintColorReplacesCurrentColor)
{
    const QColor green(0, 200, 0);
    const auto   greenBitmap = provider().pixmap("Capture", IconSize::ExtraLarge);
    ASSERT_FALSE(greenBitmap.isNull());
    EXPECT_TRUE(containsColor(greenBitmap, green)) << "换了主题色但画出来的像素里没有这个颜色，说明 currentColor 没被替换";

    IconProvider untinted;
    untinted.setTintColor(QColor(20, 20, 20));
    const auto darkBitmap = untinted.pixmap("Capture", IconSize::ExtraLarge);
    ASSERT_FALSE(darkBitmap.isNull());
    EXPECT_FALSE(containsColor(darkBitmap, green)) << "换了色之后还留着上一份的绿，说明替换或取色路径没跟着设置值走";
    EXPECT_TRUE(containsColor(darkBitmap, QColor(20, 20, 20)));
}

/**
 * @brief 钉住高 DPI 副本带着自己的缩放比
 * @details 需求要求矢量图标自动适配 DPI：200% 屏上必须交出一张 48 物理像素、逻辑仍是 24 的位图，
 *          否则系统会把 24 的图硬放大成糊的。
 */
TEST_F(IconProviderFixture, HighDpiPixmapKeepsItsLogicalSize)
{
    const auto bitmap = provider().pixmap("Record", IconSize::Large, 2.0);
    ASSERT_FALSE(bitmap.isNull());
    EXPECT_DOUBLE_EQ(bitmap.devicePixelRatio(), 2.0) << "缩放比没写回位图，Qt 会按物理像素当逻辑尺寸用";
    EXPECT_EQ(bitmap.size(), QSize(48, 48)) << "物理像素应当是 24×2；逻辑尺寸由缩放比还原，档位才不会被带跑";

    // 越界的缩放比要夹住而不是照着算：0.5 会画出一张比档位还小的图
    const auto clamped = provider().pixmap("Record", IconSize::Large, 9.0);
    EXPECT_EQ(clamped.size(), QSize(72, 72)) << "缩放比封顶在 3，超出应当夹住而不是无限乘";
}

/**
 * @brief 钉住名字拼错时的拒绝面
 * @details 空图标比「随手换一枚相近的」诚实：调用方能立刻看见没画出来，日志里也有原因。
 */
TEST_F(IconProviderFixture, UnknownNameYieldsEmptyIconWithoutSubstituting)
{
    const auto bitmap = provider().pixmap("NoSuchIconAtAll", IconSize::Large);
    EXPECT_TRUE(bitmap.isNull()) << "不存在的名字画出了东西，说明路径拼接落到了别的文件上";
    EXPECT_TRUE(provider().icon("NoSuchIconAtAll").isNull()) << "不存在的名字应当给出空图标";
}
