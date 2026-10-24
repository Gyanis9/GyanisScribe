#include "Gui/Resources/IconProvider.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QIcon>
#include <QWidget>
#include <QPushButton>
#include <QImage>
#include <QPixmap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QSize>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using GyanisScribe::Gui::IconProvider;
using GyanisScribe::Gui::IconSize;

namespace
{
    // 需求第七节点名的图标清单：这一份是抄来的独立期望值，不从资源目录反推，
    // 否则删掉一枚 SVG 之后用例只会跟着一起改口，就再也抓不住它了
    constexpr std::array<std::string_view, 54> kRequiredIcons = {
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

    /// 尺寸阶梯的八档，按从小到大排列，断言里按索引取
    constexpr std::array kLadderSizes = {
            IconSize::Small, IconSize::Medium, IconSize::Large, IconSize::ExtraLarge,
            IconSize::Huge, IconSize::Display, IconSize::History, IconSize::Maximum};

    /// 阶梯对应的逻辑像素数，与 IconSize 的枚举值同解，这条用例把它钉住
    constexpr std::array kLadderPixels = {16, 20, 24, 32, 48, 64, 96, 128};

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
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestIconProvider";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
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
        EXPECT_NE(source.indexOf(QStringLiteral(R"(stroke-width="2")")), -1) << name.toStdString() << " 线宽不是 2";

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
 * @brief 钉住光栅化结果的缓存键带齐「名字、档位、描边色、缩放比」四个分量
 * @details 加缓存的风险不是「慢」而是「把旧那一格当新的发出去」：同一个实例上换描边色是换主题时
 *          真正走的路径（refreshIcons 就是这么接的），少一个分量就会交回旧色或低缩放的图。
 *          同一组参数两次取值必须逐位相同——那是这份表唯一对外承诺的事。
 */
TEST_F(IconProviderFixture, CacheKeyCarriesTintSizeAndRatio)
{
    IconProvider icons;
    icons.setTintColor(QColor(0, 200, 0));
    const auto greenSmall   = icons.pixmap("Capture", IconSize::Small);
    const auto greenMedium  = icons.pixmap("Capture", IconSize::Medium);
    const auto greenDoubled = icons.pixmap("Capture", IconSize::Small, 2.0);
    ASSERT_FALSE(greenSmall.isNull());

    icons.setTintColor(QColor(20, 20, 20));
    const auto darkSmall = icons.pixmap("Capture", IconSize::Small);
    EXPECT_FALSE(containsColor(darkSmall, QColor(0, 200, 0))) << "描边色没进缓存键：换了主题色还交回旧色那一格";
    EXPECT_TRUE(containsColor(darkSmall, QColor(20, 20, 20)));

    // 回到第一次那一格：同一组参数两次取值要逐位相同
    icons.setTintColor(QColor(0, 200, 0));
    // QPixmap 的 operator== 在 Qt 6 里是被删掉的，逐位比对要走 QImage
    EXPECT_TRUE(icons.pixmap("Capture", IconSize::Small).toImage() == greenSmall.toImage()) << "同一组参数两次取到不同的像素";

    // 档位与缩放比也要各归各格：尺寸按 IconSize 的档位值与夹住后的缩放比手算
    EXPECT_EQ(greenSmall.size(), QSize(16, 16));
    EXPECT_EQ(greenMedium.size(), QSize(20, 20)) << "档位不在键里时两档会同尺寸";
    EXPECT_EQ(greenDoubled.size(), QSize(32, 32)) << "缩放比不在键里时高 DPI 那格会拿到低缩放的那一份";
    EXPECT_DOUBLE_EQ(greenDoubled.devicePixelRatio(), 2.0);
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

/**
 * @brief 钉住「换主题后，已经挂出去的按钮图标能按名字重取一次」
 * @details QIcon 是取色那一刻的快照：不记名字、不重取，深色档就会留着深色描边（等于看不见）。
 *          refreshIcons 只动走过 applyIcon 的按钮——色块那类自绘图标不能被顺手覆盖。
 */
TEST_F(IconProviderFixture, AppliedIconsReResolveAfterTheTintChanges)
{
    IconProvider icons;
    icons.setTintColor(QColor(0, 200, 0));

    QWidget     root;
    QPushButton applied{&root};
    QPushButton handPainted{&root};
    icons.applyIcon(applied, "Capture");
    handPainted.setIcon(icons.icon("Capture"));
    ASSERT_TRUE(containsColor(applied.icon().pixmap(16), QColor(0, 200, 0)));

    icons.setTintColor(QColor(20, 20, 20));
    icons.refreshIcons(root);

    EXPECT_TRUE(containsColor(applied.icon().pixmap(16), QColor(20, 20, 20))) << "走过 applyIcon 的按钮换主题后仍是旧描边色";
    EXPECT_FALSE(containsColor(applied.icon().pixmap(16), QColor(0, 200, 0))) << "旧描边色还留在图标里";
    EXPECT_TRUE(containsColor(handPainted.icon().pixmap(16), QColor(0, 200, 0))) << "没走 applyIcon 的按钮被 refreshIcons 改了：自绘的图标会被覆盖";
}

/**
 * @brief 钉住全部图标只用一个线宽档、且颜色只走主题注入
 * @details 设计规格把描边收在唯一的 2 档：多出来的粗细会让同一排按钮看上去不是一家人。
 *          颜色只允许 none / currentColor——写死的色值绕开了 IconProvider 的主题注入，换主题时会留在旧色上。
 *          与 BundledSourcesFollowTheSvgContract 分工：那条查「2 这个档位还在不在」，
 *          这条查「有没有第二档或写死的色混进来」。
 */
TEST_F(IconProviderFixture, BundledSourcesKeepOneStrokeWidthAndThemeColorsOnly)
{
    const QRegularExpression widthPattern{QStringLiteral(R"re(stroke-width="([^"]+)")re")};
    const QRegularExpression fillPattern{QStringLiteral(R"re(fill="([^"]+)")re")};

    for (const auto &name: provider().availableIconNames())
    {
        const auto source = readIconSource(name.toStdString());
        ASSERT_FALSE(source.isEmpty()) << name.toStdString() << " 的资源是空的";

        auto widths = widthPattern.globalMatch(source);
        ASSERT_TRUE(widths.hasNext()) << name.toStdString() << " 没有声明线宽";
        while (widths.hasNext())
        {
            EXPECT_EQ(widths.next().captured(1), QStringLiteral("2")) << name.toStdString() << " 上混进了另一档线宽";
        }

        auto fills = fillPattern.globalMatch(source);
        while (fills.hasNext())
        {
            const auto value = fills.next().captured(1);
            EXPECT_TRUE(value == QStringLiteral("none") || value == QStringLiteral("currentColor")) << name.toStdString() << " 出现了写死的填充色：" << value.toStdString();
        }

        EXPECT_EQ(source.indexOf(QLatin1Char('#')), -1) << name.toStdString() << " 里出现了十六进制颜色，绕过了主题注入";
    }
}

/**
 * @brief 钉住每枚图标在每个缩放比上的墨迹都压在安全区里
 * @details 两个关注点共用这一圈判据：
 *          其一，24 网格两侧各留两格的呼吸感是这套图标「不挤」的来源，贴边或出框的图形与别处并排就显脏；
 *          其二，不带边界的 render() 会把 viewBox 按物理像素铺满画家视口，画家再乘一次缩放比，
 *          画出来是两倍大且右下半被裁——那种错的墨迹正好全落在外圈上。
 *          判据按 24 逻辑 ×2 / ×3 渲染，外圈 3 像素对应逻辑 1.5；
 *          这一圈里出现任何墨迹，都说明坐标越过了规格的内容界 2..22（含 0.75 描边外缘），或渲染口径又错了。
 */
TEST_F(IconProviderFixture, BundledIconsStayInsideTheSafeZoneAtEveryDevicePixelRatio)
{
    constexpr int kRing = 3; ///< 外圈像素数，对应逻辑 1.5

    for (const auto ratio: {2.0, 3.0})
    {
        const auto edge = static_cast<int>(IconSize::Large) * static_cast<int>(ratio);
        for (const auto &name: provider().availableIconNames())
        {
            const auto bitmap = provider().pixmap(name.toStdString(), IconSize::Large, ratio);
            ASSERT_FALSE(bitmap.isNull()) << name.toStdString() << " 画不出位图";
            const auto image = bitmap.toImage().convertToFormat(QImage::Format_ARGB32);
            ASSERT_EQ(image.size(), QSize(edge, edge));

            auto paintedInRing = 0;
            for (int y = 0; y < edge; ++y)
            {
                const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = 0; x < edge; ++x)
                {
                    const auto inRing = x < kRing || y < kRing || x >= edge - kRing || y >= edge - kRing;
                    if (inRing && qAlpha(line[x]) != 0)
                    {
                        ++paintedInRing;
                    }
                }
            }
            EXPECT_EQ(paintedInRing, 0) << name.toStdString() << " 在 " << ratio << " 倍缩放下有墨迹落进了外圈 3 像素：内容出界、贴边或渲染口径错了";
        }
    }
}

/**
 * @brief 钉住每枚图标的墨迹坐落在光学中心附近
 * @details 偏心的图形在按钮里会有「没对齐」的观感，肉眼说不清但一排排摆过去就是歪的。
 *          判据：48 像素位图上墨迹外接框的中心与几何中心相差不超过 4 像素（逻辑 2 格为限），
 *          留这个宽度是给手指、铅笔这类天生有方向的图形留的余地——它们的小尾巴允许偏一点，但不许偏一圈。
 */
TEST_F(IconProviderFixture, BundledIconsSitNearTheOpticalCenter)
{
    constexpr int    kEdge      = 48;
    constexpr double kTolerance = 4.0;

    for (const auto &name: provider().availableIconNames())
    {
        const auto bitmap = provider().pixmap(name.toStdString(), IconSize::Large, 2.0);
        ASSERT_FALSE(bitmap.isNull()) << name.toStdString() << " 画不出位图";
        const auto image = bitmap.toImage().convertToFormat(QImage::Format_ARGB32);

        int minimumX = kEdge;
        int minimumY = kEdge;
        int maximumX = -1;
        int maximumY = -1;
        for (int y = 0; y < kEdge; ++y)
        {
            const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < kEdge; ++x)
            {
                if (qAlpha(line[x]) == 0)
                {
                    continue;
                }
                minimumX = std::min(minimumX, x);
                maximumX = std::max(maximumX, x);
                minimumY = std::min(minimumY, y);
                maximumY = std::max(maximumY, y);
            }
        }

        ASSERT_GE(maximumX, 0) << name.toStdString() << " 画出来是全透明";
        const auto centerX = (minimumX + maximumX) / 2.0;
        const auto centerY = (minimumY + maximumY) / 2.0;
        EXPECT_LE(std::abs(centerX - (kEdge - 1) / 2.0), kTolerance) << name.toStdString() << " 的墨迹横向偏心";
        EXPECT_LE(std::abs(centerY - (kEdge - 1) / 2.0), kTolerance) << name.toStdString() << " 的墨迹纵向偏心";
    }
}

/**
 * @brief 钉住高 DPI 副本里图形不被缩小
 * @details 渲染口径的另一种坏法：边界给错时图形会整只缩进一角——这种坏法既不越安全区也不偏心，
 *          前面两条判据都看不见（缩小的图形本来就在安全区里，还居着中）。
 *          判据取墨迹外接框的长边：48 像素图上至少要占 22 像素（逻辑 11 格）——
 *          全套图标的长边都远在这之上，缩水到一半当场变红。
 */
TEST_F(IconProviderFixture, HighDpiVariantsKeepTheGlyphAtFullSize)
{
    constexpr int kEdge            = 48;
    constexpr int kMinimumLongSide = 22;

    for (const auto &name: provider().availableIconNames())
    {
        const auto bitmap = provider().pixmap(name.toStdString(), IconSize::Large, 2.0);
        ASSERT_FALSE(bitmap.isNull()) << name.toStdString() << " 画不出位图";
        const auto image = bitmap.toImage().convertToFormat(QImage::Format_ARGB32);

        int minimumX = kEdge;
        int minimumY = kEdge;
        int maximumX = -1;
        int maximumY = -1;
        for (int y = 0; y < kEdge; ++y)
        {
            const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < kEdge; ++x)
            {
                if (qAlpha(line[x]) == 0)
                {
                    continue;
                }
                minimumX = std::min(minimumX, x);
                maximumX = std::max(maximumX, x);
                minimumY = std::min(minimumY, y);
                maximumY = std::max(maximumY, y);
            }
        }

        ASSERT_GE(maximumX, 0) << name.toStdString() << " 画出来是全透明";
        const auto longerSide = std::max(maximumX - minimumX + 1, maximumY - minimumY + 1);
        EXPECT_GE(longerSide, kMinimumLongSide) << name.toStdString() << " 的高 DPI 副本里图形缩水了";
    }
}

/**
 * @brief 钉住同族图标共用同一份外框几何
 * @details 截图族与录屏族是同两句话（全屏 / 选区 / 上次选区 / 窗口），外框必须逐字一致：
 *          分头画是这条路最容易发生的劣化——改了一处忘了另一处，两枚图标看上去是两家出的。
 *          判据比对的就是外框那一行的字面量，改家族就得改一处。
 */
TEST_F(IconProviderFixture, CaptureAndRecordFamiliesShareFrameGeometry)
{
    const auto dashedFrame = QStringLiteral(R"(<rect x="3" y="3" width="18" height="18" rx="3" stroke-dasharray="3 3"/>)");
    for (const auto name: {"CaptureRegion", "CaptureLastRegion", "RecordRegion", "OcrRegion"})
    {
        EXPECT_NE(readIconSource(name).indexOf(dashedFrame), -1) << name << " 的虚线选区框不是家族那一份";
    }

    const auto windowFrame = QStringLiteral(R"(<rect x="3" y="4" width="18" height="16" rx="3"/>)");
    const auto titleBar    = QStringLiteral(R"(<path d="M3 8h18"/>)");
    for (const auto name: {"CaptureWindow", "RecordWindow"})
    {
        const auto source = readIconSource(name);
        EXPECT_NE(source.indexOf(windowFrame), -1) << name << " 的窗口外框不是家族那一份";
        EXPECT_NE(source.indexOf(titleBar), -1) << name << " 的标题栏线不是家族那一份";
    }

    const auto cornerBracket = QStringLiteral(R"(M3 8V5A2 2 0 0 1 5 3H8)");
    for (const auto name: {"CaptureFullScreen", "RecordFullScreen"})
    {
        EXPECT_NE(readIconSource(name).indexOf(cornerBracket), -1) << name << " 的角括号不是家族那一份";
    }
}

/**
 * @brief 钉住 125% / 150% 屏拿到的就是精确倍率位图
 * @details 只备 1×/2× 时 QIcon 会把 2× 再缩到 1.25/1.5，正是发糊的来源。
 */
TEST_F(IconProviderFixture, FractionalScreenRatiosGetExactPixmaps)
{
    const auto at125 = provider().pixmap("Capture", IconSize::Small, 1.25);
    ASSERT_FALSE(at125.isNull());
    EXPECT_EQ(at125.size(), QSize(20, 20));
    EXPECT_DOUBLE_EQ(at125.devicePixelRatio(), 1.25);

    const auto at150 = provider().pixmap("Capture", IconSize::Small, 1.5);
    ASSERT_FALSE(at150.isNull());
    EXPECT_EQ(at150.size(), QSize(24, 24));
    EXPECT_DOUBLE_EQ(at150.devicePixelRatio(), 1.5);
}

/**
 * @brief 钉住 96 档真的存在
 * @details 历史列表把类别图标按 96px 请求；没有 96 档时 QIcon 会把 32 放大到 96。
 */
TEST_F(IconProviderFixture, HistoryLadderIncludesNinetySix)
{
    const auto icon = provider().icon("Record");
    ASSERT_FALSE(icon.isNull());
    EXPECT_TRUE(icon.availableSizes().contains(QSize(96, 96)));
}

/**
 * @brief 钉住品牌图不参与主题上色、且按原生尺寸交付
 * @details 品牌图是固定三色；一旦被 setTintColor 染成单色，关于页就失去品牌色。
 */
TEST_F(IconProviderFixture, BrandPixmapKeepsNativeSizesAndIgnoresTheTint)
{
    IconProvider icons;
    icons.setTintColor(QColor(0, 200, 0));

    const auto small = icons.brandPixmap(16, 1.0);
    ASSERT_FALSE(small.isNull());
    EXPECT_EQ(small.size(), QSize(16, 16));
    EXPECT_TRUE(containsColor(small, QColor("#151515")));
    EXPECT_TRUE(containsColor(small, QColor("#F5F5F3")));
    EXPECT_TRUE(containsColor(small, QColor("#5BE37A")));
    EXPECT_FALSE(containsColor(small, QColor(0, 200, 0)));

    const auto doubled = icons.brandPixmap(64, 2.0);
    ASSERT_FALSE(doubled.isNull());
    EXPECT_EQ(doubled.size(), QSize(128, 128));
    EXPECT_DOUBLE_EQ(doubled.devicePixelRatio(), 2.0);
}

/**
 * @brief 钉住品牌 QIcon 备齐 8 个原生尺寸
 * @details 任务栏、窗口、关于页会分别要 16/32/64/128；只备一档就会拿别的尺寸缩放。
 */
TEST_F(IconProviderFixture, BrandIconExposesTheEightNativeSizes)
{
    const auto icon = IconProvider{}.brandIcon();
    ASSERT_FALSE(icon.isNull());
    for (const auto size: {16, 24, 32, 48, 64, 128, 256, 512})
    {
        EXPECT_TRUE(icon.availableSizes().contains(QSize(size, size))) << size << " 档缺席";
    }
}
