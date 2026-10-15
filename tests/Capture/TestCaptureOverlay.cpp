// 遮罩的「悬停认出整扇窗口」：候选按这块屏的原点换成局部坐标、命中取压在最上面的那一个、
// 原地放开（没拖出选区）时采纳此刻亮着的那一块；另钉放大镜的读数换算与翻边摆放。
// 事件用 sendEvent 直接投给控件，判据不依赖「鼠标恰好在这个时刻移动到位」。
#include "Capture/CaptureOverlay.h"

#include "Pick/ColorFormat.h"
#include "Pick/PickGeometry.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Gui/Controls/AppShortcuts.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QThread>
#include <QRect>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <vector>

using GyanisScribe::Capture::CaptureOverlay;
using GyanisScribe::Capture::SizeUnit;
using GyanisScribe::Pick::ColorFormat;

namespace
{
    std::unique_ptr<QApplication> application;

    // 这块「屏」故意不从 (0,0) 开始：局部换算少减一次原点，在主屏上看不出来，在副屏上就是错位
    const QRect kScreenGeometry{1920, 0, 800, 600};

    class CaptureOverlayFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestCaptureOverlay";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 一条用例一份 INI：遮罩的裸键读数从这里来，并行跑时两个进程写同一份会互相盖掉
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            m_iniPath        = std::filesystem::temp_directory_path() / std::format("GyanisScribe-CaptureOverlay-{}-{}.ini", info->test_suite_name(), info->name());
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
            m_settings = std::make_unique<GyanisScribe::Settings::SettingsStore>(m_iniPath);
        }

        void TearDown() override
        {
            // 先放 store（它会冲刷配置），再删文件
            m_settings.reset();
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
        }

        /// 换键那几条判据要往配置里写一颗新键，读端就是这一份
        [[nodiscard]] GyanisScribe::Settings::ISettingsStore &settings() const
        {
            return *m_settings;
        }

        [[nodiscard]] std::unique_ptr<CaptureOverlay> makeOverlay() const
        {
            QImage background(kScreenGeometry.size(), QImage::Format_ARGB32_Premultiplied);
            background.fill(Qt::white);
            return std::make_unique<CaptureOverlay>(background, kScreenGeometry, 1.0, *m_settings);
        }

        /// 用给定的冻帧与缩放因子造遮罩：放大镜的两笔换算都要在这条路上验
        [[nodiscard]] std::unique_ptr<CaptureOverlay> makeOverlay(const QImage &background, qreal devicePixelRatio) const
        {
            return std::make_unique<CaptureOverlay>(background, kScreenGeometry, devicePixelRatio, *m_settings);
        }

        std::filesystem::path                                  m_iniPath;  ///< 本次用例专用的设置文件
        std::unique_ptr<GyanisScribe::Settings::SettingsStore> m_settings; ///< 裸键清册的读端
    };

    /// 事件的局部坐标给控件自己读（position()），全局那一份按这块屏的原点推出去
    [[nodiscard]] QPointF globalOf(const QPoint &local)
    {
        return QPointF{kScreenGeometry.topLeft() + local};
    }

    void moveOver(CaptureOverlay &overlay, const QPoint &local)
    {
        QMouseEvent event{QEvent::MouseMove, QPointF{local}, globalOf(local), Qt::NoButton, Qt::NoButton, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(&overlay, &event);
    }

    void buttonAt(CaptureOverlay &overlay, const QEvent::Type type, const QPoint &local)
    {
        const auto  buttons = type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton;
        QMouseEvent event{type, QPointF{local}, globalOf(local), Qt::LeftButton, buttons, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(&overlay, &event);
    }

    /// 造一份认得出自己位置的冻帧：底色全黑，被点名的格子各涂一种
    [[nodiscard]] QImage makeFrame(const QSize &size)
    {
        QImage frame{size, QImage::Format_ARGB32};
        frame.fill(Qt::black);
        return frame;
    }

    /// 两扇叠在一起的窗口：第一份是压在最上面的那个小对话框，第二份是被它挡住的大窗口
    const std::vector<QRect> kCandidates{QRect{2100, 150, 100, 80}, QRect{2020, 100, 300, 200}};
} // namespace

/**
 * @brief 钉住悬停命中取最上面那一个，并且交回的是这块屏的局部坐标
 * @details 全局原点 (1920,0) 的屏上，全局 (2100,150) 的窗口在局部是 (180,150)。
 *          少减一次原点，遮罩就会把框画到偏左 1920 格的地方——用户看得见，代码里却不出错。
 */
TEST_F(CaptureOverlayFixture, HoverAdoptsTopmostCandidateInLocalCoordinates)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);

    // (200,160) 同时落在两扇上：取压在上面那一份
    moveOver(*overlay, QPoint{200, 160});
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(180, 150, 100, 80));

    // 只落在下面那一扇的位置：认出的就是它，局部坐标同样减过屏原点
    moveOver(*overlay, QPoint{250, 250});
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(100, 100, 300, 200));

    // 哪一扇都不在：不许亮一个凭空的框
    moveOver(*overlay, QPoint{700, 500});
    EXPECT_FALSE(overlay->hoveredRegion().has_value()) << "光标在没有候选的地方也亮了一个框";
}

/**
 * @brief 钉住「按下又原地放开」采纳悬停的那一扇，而原地放开过去什么都不产出
 * @details 这是框选里最常用的一下：点一下就要这一扇窗口。采纳之后选区与窗口边界要逐位相同，
 *          差一格就会把邻窗的一列像素带进成品。
 */
TEST_F(CaptureOverlayFixture, ClickWithoutDragAdoptsTheHoveredWindow)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);

    moveOver(*overlay, QPoint{200, 160});
    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{200, 160});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{200, 160});

    EXPECT_EQ(overlay->selectedRegion(), QRect(180, 150, 100, 80)) << "点一下没有交出压在光标下那扇窗";
    EXPECT_FALSE(overlay->hoveredRegion().has_value()) << "已经落成的选区不该再留一个预览框";
}

/**
 * @brief 钉住手动框选没被这条新路抢走：拖出来的仍然是拖的那一块
 */
TEST_F(CaptureOverlayFixture, DraggedRegionStillWinsOverHover)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);

    moveOver(*overlay, QPoint{110, 160});
    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{400, 300});
    moveOver(*overlay, QPoint{460, 360});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{460, 360});

    EXPECT_EQ(overlay->selectedRegion(), QRect(400, 300, 61, 61)) << "拖出来的选区被悬停顶掉了";
    // 选区已成，之后移动不再亮预览：两块同时亮着会让人分不清交出去的是哪一个
    moveOver(*overlay, QPoint{110, 160});
    EXPECT_FALSE(overlay->hoveredRegion().has_value());
}

/**
 * @brief 拒绝面：平台没给候选时，遮罩退回纯框选，点一下什么也不产出
 * @details 这条钉住「空候选表」那一档：少了它，一次平台降级就会变成点一下就截一屏的怪行为。
 */
TEST_F(CaptureOverlayFixture, WithoutCandidatesClickProducesNothing)
{
    auto overlay = makeOverlay();

    moveOver(*overlay, QPoint{110, 160});
    EXPECT_FALSE(overlay->hoveredRegion().has_value());

    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{110, 160});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{110, 160});
    EXPECT_TRUE(overlay->selectedRegion().isNull()) << "没有候选时点一下也该产出选区的话，框选就多了条隐式规则";
}

/**
 * @brief 钉住「没有按下就没有选区」：光标划过的轨迹不许编出一块框
 * @details 这条是补的旧账。遮罩开了鼠标跟踪，而 mouseMoveEvent 把光标位置直接写进选区的当前端点，
 *          于是鼠标一划就出现一块从原点量到光标的大框，按 Enter 还会把它当成用户框好的交出去。
 */
TEST_F(CaptureOverlayFixture, MovingWithoutPressingYieldsNoSelection)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);

    moveOver(*overlay, QPoint{300, 200});
    EXPECT_TRUE(overlay->selectedRegion().isNull()) << "只是划过光标就编出了一块选区";
    EXPECT_TRUE(overlay->hoveredRegion().has_value()) << "划过时悬停该亮着，它不该顺带变成选区";

    int emitted = 0;
    QObject::connect(overlay.get(), &CaptureOverlay::regionSelected, overlay.get(), [&emitted](const QRect &) { ++emitted; });
    overlay->confirmSelection();
    EXPECT_EQ(emitted, 0) << "没框过东西的一次确认也产出了成品";
}

/**
 * @brief 钉住「没有鼠标移动就没有放大镜」：默认值 (0,0) 不是光标位置
 * @details 少了这一条，遮罩一铺出来就会在左上角摆一枚放大镜，而那里通常正是要框的东西。
 */
TEST_F(CaptureOverlayFixture, MagnifierAwaitsTheFirstMouseMove)
{
    auto overlay = makeOverlay();
    EXPECT_TRUE(overlay->magnifierRect().isNull()) << "还没收到鼠标移动就摆出了放大镜";
    EXPECT_FALSE(overlay->colorUnderCursor().isValid()) << "没有光标位置却报出了一个颜色";

    moveOver(*overlay, QPoint{110, 160});
    EXPECT_FALSE(overlay->magnifierRect().isNull());
}

/**
 * @brief 钉住放大镜跟着光标常驻，不限于拖框途中
 * @details 挑起始角、拉手柄、平移选区这三处都要像素级读数；只在拖框时显示的话，
 *          最需要它的「按下之前对准那一个像素」反而没有。
 */
TEST_F(CaptureOverlayFixture, MagnifierTravelsWithTheCursorBeyondDragging)
{
    auto overlay = makeOverlay();
    EXPECT_FALSE(overlay->isMagnifierVisible()) << "还没收到鼠标移动就显示，等于把 (0,0) 当成了光标";

    moveOver(*overlay, QPoint{110, 160});
    EXPECT_TRUE(overlay->isMagnifierVisible()) << "悬停挑窗口时不显示：挑起始角那一下看不到像素";

    // 拖出一块再松开：选区落定、抓点摆出来之后它仍该跟着光标
    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{100, 150});
    moveOver(*overlay, QPoint{200, 230});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{200, 230});
    EXPECT_TRUE(overlay->isMagnifierVisible()) << "选区落定之后放大镜就没了：拉手柄对齐边线时没有读数";
}

/**
 * @brief 钉住读数取的是「这块屏的原点 + 缩放因子」折算后的那一帧像素
 * @details 遮罩记的是这块屏内的局部点，冻帧是物理像素。少减原点会在副屏上偏一整屏，
 *          少乘缩放会在高 DPI 上偏到左上角——两种都错得毫无声息，只能把颜色本身钉住。
 */
TEST_F(CaptureOverlayFixture, ReadoutFollowsTheFramePixelNotTheLocalPoint)
{
    QImage frame{QSize{kScreenGeometry.width() * 2, kScreenGeometry.height() * 2}, QImage::Format_ARGB32};
    frame.fill(Qt::black);
    const QPoint local{100, 150};
    frame.setPixelColor(QPoint{local.x() * 2, local.y() * 2}, QColor{10, 200, 20}); // 乘了缩放才是这一格
    frame.setPixelColor(local, QColor{200, 20, 20});                                // 只减不乘会读到这一格

    auto overlay = makeOverlay(frame, 2.0);
    moveOver(*overlay, local);

    const QColor expected{10, 200, 20};
    EXPECT_EQ(overlay->colorUnderCursor(), expected) << "读数没按缩放因子折到帧像素";
}

/**
 * @brief 钉住放大镜读数条里颜色那一格按配置里的字母档排字
 * @details 判据读的就是画上去的同一串（readoutColorText 是绘图与用例共用的那一个出口）：
 *          关掉大写还出 #0AC814，就是「设置里拨了、界面上没接上」那一类现场。
 *          手算：R=10→0A、G=200→C8、B=20→14。
 */
TEST_F(CaptureOverlayFixture, ReadoutColorHonoursTheHexCasingSetting)
{
    QImage frame{QSize{kScreenGeometry.width() * 2, kScreenGeometry.height() * 2}, QImage::Format_ARGB32};
    frame.fill(Qt::black);
    const QPoint local{100, 150};
    frame.setPixelColor(QPoint{local.x() * 2, local.y() * 2}, QColor{10, 200, 20});

    auto overlay = makeOverlay(frame, 2.0);
    moveOver(*overlay, local);
    EXPECT_EQ(overlay->readoutColorText(), QStringLiteral("#0AC814")) << "默认档该是本仓此前的大写";

    ASSERT_TRUE(static_cast<bool>(settings().setValue(GyanisScribe::Settings::kPickHexUppercase, false))) << "写入失败";
    EXPECT_EQ(overlay->readoutColorText(), QStringLiteral("#0ac814")) << "关掉大写后读数还在折成大写";
}

/**
 * @brief 钉住放大镜视野三档改的是那枚插图，读数条留在原位；「不显示」不收读数条
 * @details 期望值手算：显示边长固定 180，一格多大按 180/取样格数 取整——窄档 9 格 → 20×9=180，
 *          宽档 21 格 → 8×21=168（宁可少几像素也不出现半格）。两档宽度不同这一判，
 *          钉的就是「视野档真的传到了几何那一层」，而不是只有下拉框上写了字。
 *          「不显示」那一档的读数条仍按窄档摆位：关掉插图不该让那一条跳到别的位置。
 */
TEST_F(CaptureOverlayFixture, MagnifierViewTiersChangeTheIllustrationAndKeepTheReadout)
{
    auto narrow = makeOverlay();
    moveOver(*narrow, QPoint{300, 300});
    ASSERT_TRUE(narrow->isMagnifierVisible()) << "默认档该是窄（9×9）";
    ASSERT_EQ(narrow->magnifierRect().width(), 180);
    const auto narrowStrip = narrow->readoutStripRect();
    ASSERT_FALSE(narrowStrip.isNull()) << "读数条没位置";

    ASSERT_TRUE(static_cast<bool>(settings().setValue(GyanisScribe::Settings::kCaptureMagnifierView, QStringLiteral("wide")))) << "写入失败";
    auto wide = makeOverlay();
    moveOver(*wide, QPoint{300, 300});
    EXPECT_EQ(wide->magnifierRect().width(), 168) << "视野档没传到几何那一层";

    ASSERT_TRUE(static_cast<bool>(settings().setValue(GyanisScribe::Settings::kCaptureMagnifierView, QStringLiteral("off")))) << "写入失败";
    auto off = makeOverlay();
    moveOver(*off, QPoint{300, 300});
    EXPECT_FALSE(off->isMagnifierVisible()) << "选了不显示还在画插图";
    EXPECT_TRUE(off->magnifierRect().isNull()) << "magnifierRect 没跟着说没有";
    EXPECT_EQ(off->readoutStripRect(), narrowStrip) << "关掉插图把坐标与颜色一起没收、或让读数条跳了位";
}

/**
 * @brief 钉住放大镜摆在光标右下、贴到屏角时翻到左上，并且整枚留在屏内
 * @details 摆在光标右下是为了不挡住正要看的像素；翻边判据失效时放大镜有一半跑到屏外，
 *          用户照着抄的格子根本不是光标那一片。
 */
TEST_F(CaptureOverlayFixture, MagnifierFlipsAsideAtTheScreenCorner)
{
    auto         overlay = makeOverlay();
    const QPoint nearCorner{kScreenGeometry.width() - GyanisScribe::Pick::kMagnifierViewEdge - 40, kScreenGeometry.height() - GyanisScribe::Pick::kMagnifierViewEdge - 40};
    moveOver(*overlay, nearCorner);
    const auto roomy = overlay->magnifierRect();
    EXPECT_TRUE(roomy.left() > nearCorner.x()) << "摆得下时放大镜该在光标右下";
    EXPECT_TRUE(roomy.top() > nearCorner.y());

    const QPoint corner{kScreenGeometry.width() - 2, kScreenGeometry.height() - 2};
    moveOver(*overlay, corner);
    const auto flipped = overlay->magnifierRect();
    EXPECT_TRUE(flipped.right() < corner.x()) << "贴到屏角时没翻到光标左侧";
    EXPECT_TRUE(flipped.bottom() < corner.y()) << "贴到屏底时没翻到光标上方";

    const QRect widgetRect{QPoint{0, 0}, kScreenGeometry.size()};
    EXPECT_TRUE(widgetRect.contains(flipped)) << "放大镜整枚该留在这块屏内";
}

/// 投一条键盘事件给遮罩
void pressKey(CaptureOverlay &overlay, const Qt::Key key, const Qt::KeyboardModifiers modifiers = Qt::KeyboardModifier{})
{
    QKeyEvent event{QEvent::KeyPress, key, modifiers};
    QApplication::sendEvent(&overlay, &event);
}

/// 按 V 复制尺寸并读回，读不到就重按一次，最多等两秒
/// @param expected 期望的文本
/// @return 最终从剪贴板读到的文本
/// @details 剪贴板是全桌面共享的资源：别的进程恰好把它占着的那一刻，这一次 setText 会写不进去
///          （本机实测过的偶发红签名）。重按一次就是重写一遍——读到期望值或超时才收口，
///          判据仍然打在真实路径上，不靠「通常都成功」。
[[nodiscard]] QString copySizeAndReadBack(CaptureOverlay &overlay, const QString &expected)
{
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        pressKey(overlay, Qt::Key_V);
        const auto text = QGuiApplication::clipboard()->text();
        if (text == expected)
        {
            return text;
        }
        QApplication::processEvents();
        QThread::msleep(50);
    }
    return QGuiApplication::clipboard()->text();
}

/// 拖出一块选区：按下 anchor，拖到 current 后放开
void dragOut(CaptureOverlay &overlay, const QPoint &anchor, const QPoint &current)
{
    buttonAt(overlay, QEvent::MouseButtonPress, anchor);
    moveOver(overlay, current);
    buttonAt(overlay, QEvent::MouseButtonRelease, current);
}

/**
 * @brief 钉住裸按 C 轮换读数写法，带修饰键的不认
 * @details 只影响遮罩上这一枚读数，不写回配置：临时换一种写法看一眼，不该顺手改掉用户的默认档。
 */
TEST_F(CaptureOverlayFixture, ColorKeyCyclesTheReadoutFormat)
{
    auto overlay = makeOverlay();
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hex) << "默认档应当是 Hex";

    pressKey(*overlay, Qt::Key_C);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Rgb);
    pressKey(*overlay, Qt::Key_C);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hsl);
    pressKey(*overlay, Qt::Key_C);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hsv);
    pressKey(*overlay, Qt::Key_C);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hsi);
    pressKey(*overlay, Qt::Key_C);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Decimal);
    pressKey(*overlay, Qt::Key_C);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hex) << "六档应当轮回来";

    pressKey(*overlay, Qt::Key_C, Qt::ShiftModifier);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hex) << "Shift 已经归「挪右下角」，裸按才轮换";
}

/**
 * @brief 钉住方向键一格：单独按整块平移、按住 Shift 挪右下角，两者都不许改到另一件事
 * @details 松手之后想改一像素得重框一遍，这条键就是为了不必重框。平移改了大小、缩放挪了顶边，
 *          都是「看着按对了、交出去的却是另一块」。
 */
TEST_F(CaptureOverlayFixture, ArrowKeysNudgeTheLaidOutSelection)
{
    auto overlay = makeOverlay();
    dragOut(*overlay, QPoint{100, 100}, QPoint{140, 120});
    ASSERT_EQ(overlay->selectedRegion(), QRect(100, 100, 41, 21)) << "起点就不是这块";

    pressKey(*overlay, Qt::Key_Right);
    EXPECT_EQ(overlay->selectedRegion(), QRect(101, 100, 41, 21)) << "方向键没整块平移一格";

    pressKey(*overlay, Qt::Key_Down, Qt::ShiftModifier);
    EXPECT_EQ(overlay->selectedRegion(), QRect(101, 100, 41, 22)) << "Shift+方向键没把右下角往下扩一格";

    pressKey(*overlay, Qt::Key_Left, Qt::ShiftModifier);
    EXPECT_EQ(overlay->selectedRegion(), QRect(101, 100, 40, 22)) << "往里收一格该收窄而不是挪顶边";
}

/**
 * @brief 拒绝面：还没框出选区时方向键不许编出一块框
 * @details 与「划过光标不许产出选区」同一条规矩：键盘微调只对已经落成的那块动手。
 */
TEST_F(CaptureOverlayFixture, ArrowKeysWithoutASelectionYieldNothing)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);
    moveOver(*overlay, QPoint{300, 200});

    pressKey(*overlay, Qt::Key_Right);
    pressKey(*overlay, Qt::Key_Down, Qt::ShiftModifier);
    EXPECT_TRUE(overlay->selectedRegion().isNull()) << "没有选区时方向键凭空造出了一块";
}

/**
 * @brief 钉住选区贴到屏边时方向键就地停下，而不是把它推出这块屏
 * @details 平移的夹取按整块算：贴边再按一次，选区一格都不动。越出去的那一半用户看不见，
 *          交出去的成品却少了那几条像素。
 */
TEST_F(CaptureOverlayFixture, ArrowKeysStopAtTheScreenEdge)
{
    auto       overlay   = makeOverlay();
    const auto rightEdge = kScreenGeometry.width() - 41;
    dragOut(*overlay, QPoint{rightEdge, 50}, QPoint{kScreenGeometry.width() - 1, 60});
    const auto flush = overlay->selectedRegion();
    ASSERT_EQ(flush.right(), kScreenGeometry.width() - 1) << "没先造出贴边的选区，后面的判据就是空的";

    pressKey(*overlay, Qt::Key_Right);
    EXPECT_EQ(overlay->selectedRegion(), flush) << "贴到屏右边界还继续往屏外走";

    auto flushed = makeOverlay();
    dragOut(*flushed, QPoint{10, kScreenGeometry.height() - 10}, QPoint{30, kScreenGeometry.height() - 1});
    const auto atBottom = flushed->selectedRegion();
    ASSERT_EQ(atBottom.bottom(), kScreenGeometry.height() - 1) << "没先造出贴底的选区，后面的判据就是空的";

    pressKey(*flushed, Qt::Key_Down, Qt::ShiftModifier);
    EXPECT_EQ(flushed->selectedRegion(), atBottom) << "右下角不许越出这块屏";
}

/**
 * @brief 钉住「拖抓点是改这一块，而不是重新框一块」
 * @details 松手之后框已经落定，此时按下右下角拖动只能走抓点这条支路；
 *
 * 把它当成新框的起点会把整块丢掉，而画面上的感觉只是「拖了一下没反应」。
 */
TEST_F(CaptureOverlayFixture, DraggingABottomRightGripReshapesTheSameSelection)
{
    auto overlay = makeOverlay();
    dragOut(*overlay, QPoint{100, 100}, QPoint{140, 120});
    const auto settled = overlay->selectedRegion();
    ASSERT_EQ(settled, QRect(100, 100, 41, 21));

    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{140, 120});
    moveOver(*overlay, QPoint{160, 130});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{160, 130});

    EXPECT_EQ(overlay->selectedRegion(), QRect(100, 100, 61, 31)) << "拖右下角没把这块改大";
}

/**
 * @brief 钉住按住选区内部是整块平移，大小一格都不许变
 */
TEST_F(CaptureOverlayFixture, DraggingInsideMovesTheSelectionWithoutResizing)
{
    auto overlay = makeOverlay();
    dragOut(*overlay, QPoint{100, 100}, QPoint{140, 120});

    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{120, 110});
    moveOver(*overlay, QPoint{140, 115});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{140, 115});

    EXPECT_EQ(overlay->selectedRegion(), QRect(120, 105, 41, 21)) << "拖内部没挪位置，或顺手改了大小";
}

/**
 * @brief 钉住离选区很远的按下仍然开一块新的
 * @details 这是原有的框选走法，抓点不能把它抢走：认不到抓点就该回到「重新框」。

 */
TEST_F(CaptureOverlayFixture, PressingFarAwayStartsAFreshSelection)
{
    auto overlay = makeOverlay();
    dragOut(*overlay, QPoint{100, 100}, QPoint{140, 120});

    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{400, 400});
    moveOver(*overlay, QPoint{500, 450});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{500, 450});

    EXPECT_EQ(overlay->selectedRegion(), QRect(400, 400, 101, 51)) << "远处按下没开出新的一块";
}

/**
 * @brief 钉住 Shift 拖框锁正方形（含贴边时的夹取）
 * @details 期望值手算：从 (100,100) 拖到 (300,200)，宽 200 高 100 → 锁成 200×200，
 *
 * 另一个端点落到 (300,300)；往上拖时朝上展开；贴到屏边夹完仍是正方形。
 */
TEST_F(CaptureOverlayFixture, ShiftDragLocksTheSelectionToASquare)
{
    auto overlay = makeOverlay();

    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{100, 100});
    QMouseEvent drag{QEvent::MouseMove, QPointF{300.0, 200.0}, globalOf(QPoint{300, 200}), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier};
    QApplication::sendEvent(overlay.get(), &drag);

    // 端点对到 (300,300)：宽高都是 201，正方形；判据取两个端点与等边，不写整串矩形
    const auto region = overlay->selectedRegion();
    EXPECT_EQ(region.topLeft(), QPoint(100, 100));
    EXPECT_EQ(region.bottomRight(), QPoint(300, 300)) << "按住 Shift 拖出来的不是正方形";
    EXPECT_EQ(region.width(), region.height()) << "宽高不等就不叫正方形";

    // 不按 Shift 的那一次要原样跟随：上锁逻辑不许影响普通拖框
    QMouseEvent plain{QEvent::MouseMove, QPointF{300.0, 200.0}, globalOf(QPoint{300, 200}), Qt::NoButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
    QApplication::sendEvent(overlay.get(), &plain);
    EXPECT_EQ(overlay->selectedRegion().bottomRight(), QPoint(300, 200)) << "松开 Shift 之后没回到自由拖框";
}

/**
 * @brief 钉住「尺寸单位」这一格接到遮罩的三处出口上
 * @details 期望值手算：这块屏是 800x600 逻辑格，1.5 倍时从 (100,50) 拖到 (599,449) 框出 500x400 逻辑格，
 *          物理那一片起点是 (150,75)，终点向上取整到 (900,675)，所以宽高是 750x600。
 *          换算本身归 TestScreenUtils 的纯函数判据，这里钉的是「这一档接没接到遮罩」：
 *          读数、`V` 复制与尺寸框三处都从 measuredRect / selectionSizeText 取数，接错一处就只错一处。
 */
TEST_F(CaptureOverlayFixture, SizeUnitSettingMovesEveryOverlayReadoutTogether)
{
    const auto frame = makeFrame(kScreenGeometry.size());

    // 没拨过这一格时按逻辑像素：与本仓此前的读数形状逐位相同，也证明这一档是每个遮罩在构造时各读一次的
    auto logical = makeOverlay(frame, 1.5);
    EXPECT_EQ(logical->sizeUnit(), SizeUnit::Logical) << "默认档不是逻辑像素，遮罩上的读数会凭空乘上一个倍率";
    dragOut(*logical, QPoint{100, 50}, QPoint{599, 449});
    ASSERT_EQ(logical->selectedRegion(), QRect(100, 50, 500, 400));
    EXPECT_EQ(logical->selectionSizeText(), QStringLiteral("500×400")) << "默认档的读数不能凭空带上倍率";
    EXPECT_EQ(copySizeAndReadBack(*logical, QStringLiteral("500×400")), QStringLiteral("500×400")) << "默认档复制的该是屏幕上看到的这一组数";

    ASSERT_TRUE(static_cast<bool>(settings().setValue(GyanisScribe::Settings::kCaptureSizeUnit, QStringLiteral("px")))) << "写入失败";
    auto physical = makeOverlay(frame, 1.5);
    EXPECT_EQ(physical->sizeUnit(), SizeUnit::Physical) << "遮罩没把这一档读进来";
    dragOut(*physical, QPoint{100, 50}, QPoint{599, 449});

    // 选区那一层还是逻辑坐标（配置里的记录、抓取与裁剪都按它走），换的只是给人看的那几处
    ASSERT_EQ(physical->selectedRegion(), QRect(100, 50, 500, 400));
    EXPECT_EQ(physical->measuredRect(physical->selectedRegion()), QRect(150, 75, 750, 600));
    EXPECT_EQ(physical->selectionSizeText(), QStringLiteral("750×600")) << "读数没跟着这一档走";
    EXPECT_EQ(copySizeAndReadBack(*physical, QStringLiteral("750×600")), QStringLiteral("750×600")) << "V 自己再算一遍倍率：屏幕上写着 750×600，剪贴板里却是别的数";
}

/**
 * @brief 钉住 Ctrl+A 选中整块屏、V 复制的是当前那一档的尺寸
 * @details 默认档是逻辑像素（与遮罩读数、尺寸框同一口径）；缩放屏上想要「会被裁出多少像素」
 *          就把设置里那一格拨成物理像素，判据按两档各钉一次。剪贴板直接读回，与用户粘出来的是同一份。
 */
TEST_F(CaptureOverlayFixture, ControlASelectsEverythingAndVCopiesThePixelSize)
{
    auto overlay = makeOverlay();

    // 先框一块 201×101 的：这块遮罩的缩放因子是 1，两档同值
    dragOut(*overlay, QPoint{100, 100}, QPoint{300, 200});
    EXPECT_EQ(copySizeAndReadBack(*overlay, QStringLiteral("201×101")), QStringLiteral("201×101")) << "V 没把尺寸复制进剪贴板";

    // 缩放屏上默认交回逻辑格：与屏幕上那行读数同解
    const auto background = makeFrame(kScreenGeometry.size());
    auto       scaled     = makeOverlay(background, 2.0);
    dragOut(*scaled, QPoint{100, 100}, QPoint{300, 200});
    EXPECT_EQ(copySizeAndReadBack(*scaled, QStringLiteral("201×101")), QStringLiteral("201×101")) << "默认档复制的是逻辑格：与遮罩读数、尺寸框同一口径";

    // 拨成物理像素后要交回「会被裁出多少像素」：两者不等时这一档才是设计工具里那把尺子
    ASSERT_TRUE(static_cast<bool>(settings().setValue(GyanisScribe::Settings::kCaptureSizeUnit, QStringLiteral("px")))) << "写入失败";
    const auto physical = makeOverlay(background, 2.0);
    dragOut(*physical, QPoint{100, 100}, QPoint{300, 200});
    EXPECT_EQ(copySizeAndReadBack(*physical, QStringLiteral("402×202")), QStringLiteral("402×202")) << "拨成物理像素后 V 交回的不是成品像素数";

    // 裸按 A 什么都不该发生（不许把 A 也当成选全屏）
    const auto before = overlay->selectedRegion();
    QKeyEvent  bareA{QEvent::KeyPress, Qt::Key_A, Qt::NoModifier};
    QApplication::sendEvent(overlay.get(), &bareA);
    EXPECT_EQ(overlay->selectedRegion(), before) << "裸按 A 改动了选区";

    // Ctrl+A：整块屏（末端取 size-1，选区宽度正好等于屏宽）
    QKeyEvent selectAll{QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier};
    QApplication::sendEvent(overlay.get(), &selectAll);
    const auto full = overlay->selectedRegion();
    EXPECT_EQ(full.width(), kScreenGeometry.width()) << "Ctrl+A 没选中整块屏";
    EXPECT_EQ(full.height(), kScreenGeometry.height());
}

/**
 * @brief 钉住 R / Shift+R 在历史选区之间循环挑
 * @details 正向从最近一条起、反向从最久一条起；到头绕回另一端（循环挑）。
 *          自己重新框过一块之后位次从头起——接着上一轮的位次会跳到一条没人指着的历史上；
 *          这一段的位次要落在「顺手接着按会给出另一条」的那个下标上（0），否则改坏了也看不出来。
 */
TEST_F(CaptureOverlayFixture, RegionHistoryKeysCycleThroughRecentSelections)
{
    const QRect first{10, 10, 40, 20};
    const QRect second{100, 50, 30, 30};
    const QRect third{200, 300, 20, 20};

    auto overlay = makeOverlay();

    // 没有历史：R 什么都不做，不凭空造出一块
    pressKey(*overlay, Qt::Key_R);
    EXPECT_TRUE(overlay->selectedRegion().isNull()) << "没有历史时 R 不该凭空造出一块";

    overlay->setRegionHistory({first, second, third});

    pressKey(*overlay, Qt::Key_R);
    EXPECT_EQ(overlay->selectedRegion(), first) << "第一下 R 该挑最近一条";
    pressKey(*overlay, Qt::Key_R);
    EXPECT_EQ(overlay->selectedRegion(), second) << "再按一下该往更久的一条走";
    pressKey(*overlay, Qt::Key_R);
    EXPECT_EQ(overlay->selectedRegion(), third) << "第三下该到最久的一条";
    pressKey(*overlay, Qt::Key_R);
    EXPECT_EQ(overlay->selectedRegion(), first) << "到头该绕回最近一条（循环挑）";

    // 反向：退一条、退到头再绕回另一端（收在最近一条上，为后面的「手框重置位次」留出可判别的下标）
    pressKey(*overlay, Qt::Key_R, Qt::ShiftModifier);
    EXPECT_EQ(overlay->selectedRegion(), third) << "Shift+R 该反向退一条";
    pressKey(*overlay, Qt::Key_R, Qt::ShiftModifier);
    EXPECT_EQ(overlay->selectedRegion(), second);
    pressKey(*overlay, Qt::Key_R, Qt::ShiftModifier);
    EXPECT_EQ(overlay->selectedRegion(), first) << "反向退到头该绕回最近一条";

    // 反向的第一下（从空的选区起）：从最久一条起
    auto fresh = makeOverlay();
    fresh->setRegionHistory({first, second, third});
    pressKey(*fresh, Qt::Key_R, Qt::ShiftModifier);
    EXPECT_EQ(fresh->selectedRegion(), third) << "反向的第一下该从最久一条起";

    // 自己重新框一块之后：位次从头起，而不是接着上一轮
    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{200, 200});
    moveOver(*overlay, QPoint{260, 250});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{260, 250});
    EXPECT_EQ(overlay->selectedRegion(), QRect(200, 200, 61, 51)) << "手框的这一块没落定，后面的判据无从谈起";
    pressKey(*overlay, Qt::Key_R);
    EXPECT_EQ(overlay->selectedRegion(), first) << "自己框过之后 R 该从最近一条重新起，而不是接着上一轮的位次";
}

// 光标处一摞界面元素，全局坐标，由深到浅：最外一层是窗口根（局部 (100,110,280,180)）；
// (200,160) 这个局部点同时落在三层与窗口候选里，用来验「悬停优先落在哪一级」
const std::vector<QRect> kElementChainGlobal{
        QRect{2060, 150, 80, 40},   // 最深一级，局部 (140,150,80,40)：按钮之类的小块
        QRect{2050, 140, 100, 60},  // 中间一层，局部 (130,140,100,60)：工具条
        QRect{2020, 110, 280, 180}, // 窗口根，局部 (100,110,280,180)：客户区
};

/**
 * @brief 钉住悬停优先落在元素链最深一级，链没了才退回窗口候选
 * @details 元素比整扇窗口更贴目标：给着链却还亮整窗，等于这一整批没生效；
 *          链为空是「本平台认不出元素」的常态出口，那时必须原样退回只认窗口。
 */
TEST_F(CaptureOverlayFixture, HoverPrefersTheDeepestElementOverTheWindowCandidate)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);
    overlay->setElementChain(kElementChainGlobal);

    moveOver(*overlay, QPoint{200, 160});
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(140, 150, 80, 40)) << "悬停没落在最深一级元素上；全局到局部少减一次屏原点也会是这副样子";

    // 对照：同一块窗口候选上不给链，悬停就该退回整窗那一档
    auto withoutChain = makeOverlay();
    withoutChain->setWindowCandidates(kCandidates);
    moveOver(*withoutChain, QPoint{200, 160});
    ASSERT_TRUE(withoutChain->hoveredRegion().has_value());
    EXPECT_EQ(*withoutChain->hoveredRegion(), QRect(180, 150, 100, 80)) << "没有元素链时没退回窗口候选";
}

/**
 * @brief 钉住 `,` 从最深一级逐级升到窗口根、再上一档就是整扇窗口，到头停住
 * @details 每一档都要与手算的局部矩形逐位相等：升错方向或跳档，用户按出来的框会一次比一次离谱。
 *          Shift+`,` 不接管（`,` 已经被「升一层」用掉了裸键这一档）。
 */
TEST_F(CaptureOverlayFixture, CommaWalksUpTheElementChainToTheWholeWindow)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);
    overlay->setElementChain(kElementChainGlobal);
    moveOver(*overlay, QPoint{200, 160});
    ASSERT_TRUE(overlay->hoveredRegion().has_value());

    pressKey(*overlay, Qt::Key_Comma);
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(130, 140, 100, 60)) << "第一下该升到中间一层";

    pressKey(*overlay, Qt::Key_Comma);
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(100, 110, 280, 180)) << "第二下该升到窗口根";

    pressKey(*overlay, Qt::Key_Comma);
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(180, 150, 100, 80)) << "再往上该接上整扇窗口";

    pressKey(*overlay, Qt::Key_Comma);
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(180, 150, 100, 80)) << "到头之后还继续按，超过了整扇窗口这一档";

    pressKey(*overlay, Qt::Key_Comma, Qt::ShiftModifier);
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(180, 150, 100, 80)) << "带 Shift 的不该接管这一档";
}

/**
 * @brief 钉住 `.` 从窗口那一档往回降到底，到底停住
 */
TEST_F(CaptureOverlayFixture, PeriodWalksBackDownToTheDeepestElement)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);
    overlay->setElementChain(kElementChainGlobal);
    moveOver(*overlay, QPoint{200, 160});

    pressKey(*overlay, Qt::Key_Comma);
    pressKey(*overlay, Qt::Key_Comma);
    pressKey(*overlay, Qt::Key_Comma);
    ASSERT_EQ(overlay->hoveredRegion(), std::optional<QRect>(QRect(180, 150, 100, 80))) << "先升到整窗那一档，后面的下降才验得到跨级";

    pressKey(*overlay, Qt::Key_Period);
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(100, 110, 280, 180)) << "`. ` 该从整窗降回窗口根";
    pressKey(*overlay, Qt::Key_Period);
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(130, 140, 100, 60));
    pressKey(*overlay, Qt::Key_Period);
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(140, 150, 80, 40)) << "该降回最深一级";

    pressKey(*overlay, Qt::Key_Period);
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(140, 150, 80, 40)) << "到底之后还继续按，钻过了最深一级";
}

/**
 * @brief 钉住相邻同矩形的包装层并成一档：按一下就得换到另一档
 * @details 惰性树里一层包装层套一层同尺寸的壳（本机实测 Chromium 窗口根连着七层同矩形），
 *          不并档就会出现按几下尺寸都不动的死档。判据收在「并过之后一次按键跨到下一个尺寸」上。
 */
TEST_F(CaptureOverlayFixture, AdjacentDuplicateLayersCollapseIntoOneStep)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);
    // 最里那层连着两份同矩形，最外一层尺寸不同
    overlay->setElementChain({QRect{2060, 150, 80, 40}, QRect{2060, 150, 80, 40}, QRect{2020, 110, 280, 180}});
    moveOver(*overlay, QPoint{200, 160});
    ASSERT_EQ(overlay->hoveredRegion(), std::optional<QRect>(QRect(140, 150, 80, 40)));

    pressKey(*overlay, Qt::Key_Comma);
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(100, 110, 280, 180)) << "同矩形包装层没并档：按键停在一个尺寸不动的死档上";

    pressKey(*overlay, Qt::Key_Comma);
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(180, 150, 100, 80)) << "并档之后往上该直接接上整扇窗口";
}

/**
 * @brief 钉住升到的档位在链变化后还指着「同一相对层级」，链短了就在整窗那一档收住
 * @details 用户按到中间一层后光标挪一点，链换了但层级还在：位次该跟着走，不是跳回最深一级。
 *          换到一条只有一层的链时，位次越界，应当收在整扇窗口这一档而不是编出一块不存在的元素。
 */
TEST_F(CaptureOverlayFixture, AscendedDepthSticksWhenTheChainChanges)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);
    overlay->setElementChain(kElementChainGlobal);
    moveOver(*overlay, QPoint{200, 160});
    pressKey(*overlay, Qt::Key_Comma); // 停在中层（位次 1）
    ASSERT_EQ(overlay->hoveredRegion(), std::optional<QRect>(QRect(130, 140, 100, 60)));

    // 链换了、层级还是三层：位次该指到新链的中层，而不是跳回最深一级
    overlay->setElementChain({QRect{2055, 155, 70, 30}, QRect{2045, 145, 95, 55}, QRect{2015, 115, 275, 175}});
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(125, 145, 95, 55)) << "链换了之后位次跳档，用户刚按出来的层级被顺手改掉";

    // 链只剩一层：位次越界，收在整扇窗口那一档
    overlay->setElementChain({QRect{2060, 150, 80, 40}});
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(180, 150, 100, 80)) << "链短于位次时该收在整窗一档，而不是编出不存在的一级";
}

/**
 * @brief 钉住「按下又原地放开」采纳的是元素那一档，不是整扇窗口
 * @details 采纳走的就是悬停预览的那一块：预览已经细分到按钮上了，点一下交出去的却是整窗，
 *          用户按「点一下要这块」得到的东西比看到的多了十几倍。
 */
TEST_F(CaptureOverlayFixture, ClickWithoutDragAdoptsTheDeepestElement)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);
    overlay->setElementChain(kElementChainGlobal);

    moveOver(*overlay, QPoint{200, 160});
    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{200, 160});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{200, 160});

    EXPECT_EQ(overlay->selectedRegion(), QRect(140, 150, 80, 40)) << "点一下交出的是整扇窗口而不是按钮那一块";
    EXPECT_FALSE(overlay->hoveredRegion().has_value()) << "已经落成的选区不该再留一个预览框";
}

/**
 * @brief 钉住悬停点只在「没在框选、也还没有选区」时报给服务
 * @details 服务按这条去问平台要元素，每次都是一串跨进程调用：拖框途中或选区已落成时再报，
 *          就是白花费用。报的坐标必须是全局读数——服务要拿它去问平台，局部坐标问出来的是别的屏的元素。
 */
TEST_F(CaptureOverlayFixture, HoverPointChangesAreReportedOnlyWhileNothingIsSelected)
{
    auto overlay = makeOverlay();

    std::vector<QPoint> reported;
    QObject::connect(overlay.get(), &CaptureOverlay::hoverPointChanged, overlay.get(), [&reported](const QPoint &point) { reported.push_back(point); });

    moveOver(*overlay, QPoint{200, 160});
    ASSERT_EQ(reported.size(), 1U) << "悬停点没报给服务，元素链永远不会被问";
    EXPECT_EQ(reported[0], kScreenGeometry.topLeft() + QPoint(200, 160)) << "报的必须是全局坐标";

    // 拖出一块选区：按下与拖动途中都不该再报
    dragOut(*overlay, QPoint{300, 300}, QPoint{340, 320});
    EXPECT_EQ(reported.size(), 1U) << "框选途中还在报悬停点";

    moveOver(*overlay, QPoint{250, 250});
    EXPECT_EQ(reported.size(), 1U) << "选区已落成还在报悬停点";
}

/**
 * @brief 钉住选区落成后元素链不再影响悬停：链晚到一步也不许亮出预览框
 * @details 悬停点报出去、链送回来之间用户已经拖出了选区：这时再把预览框亮起来，
 *          「这就是要截的那块」那句已经确定的话被改成了疑问句。
 */
TEST_F(CaptureOverlayFixture, ElementChainIsIgnoredOnceASelectionExists)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);
    dragOut(*overlay, QPoint{300, 300}, QPoint{340, 320});

    overlay->setElementChain(kElementChainGlobal);
    EXPECT_FALSE(overlay->hoveredRegion().has_value()) << "已有选区时元素链还亮起了预览框";
}

/**
 * @brief 钉住比例锁：锁的是当前选区的比例、下一拖按档来、再按一次解开恢复自由
 * @details 手算：先自由框一块 200×100（2:1），按 A 锁上取的就是它；再拖到 (600,300)
 *          差 (200,200)，2:1 下按纵轴要的格数算：宽 400、高 200；解开之后再拖恢复自由尺寸。
 *          每次起拖都落在既有选区之外的空白处：落在抓点上会走「拖手柄」，那是另一条路。
 * @note 锁着比例时新拖出的框按档来；已经框好的块仍可自由拉手柄微调——手柄那八条每次拉的是
 *       某一条边，硬套比例会把「只想改宽一点」变成两个方向一起动，反而难用。
 */
TEST_F(CaptureOverlayFixture, AspectLockTakesTheSelectionRatioAndAppliesToTheNextDrag)
{
    auto overlay = makeOverlay();

    dragOut(*overlay, QPoint{100, 100}, QPoint{299, 199});
    ASSERT_EQ(overlay->selectedRegion(), QRect(100, 100, 200, 100));

    QSize seenRatio;
    QObject::connect(overlay.get(), &CaptureOverlay::aspectRatioLockChanged, overlay.get(), [&seenRatio](const QSize &ratio) { seenRatio = ratio; });
    pressKey(*overlay, Qt::Key_A);
    EXPECT_EQ(overlay->lockedAspectRatio(), (QSize(2, 1))) << "锁的不是当前选区的比例";
    EXPECT_EQ(seenRatio, (QSize(2, 1))) << "锁上没有报出去，宿主就落不了盘";

    dragOut(*overlay, QPoint{400, 100}, QPoint{600, 300});
    EXPECT_EQ(overlay->selectedRegion(), QRect(400, 100, 400, 200)) << "拖动没按锁住的 2:1 来";

    pressKey(*overlay, Qt::Key_A);
    EXPECT_TRUE(overlay->lockedAspectRatio().isEmpty()) << "再按一次没解开";
    EXPECT_TRUE(seenRatio.isEmpty()) << "解开也该报一声（空尺寸），持久层要跟着清";

    dragOut(*overlay, QPoint{50, 50}, QPoint{250, 150});
    EXPECT_EQ(overlay->selectedRegion(), QRect(50, 50, 201, 101)) << "解开之后拖动还按着比例";

    // Shift 锁正方形：松手那一刻也不许弹回自由形状——老的 release 分支直接写原始光标点，
    // 拖出来的方框会在松手瞬间变回长方形（纯函数有用例，遮罩这一层以前没人看着）
    QMouseEvent press{QEvent::MouseButtonPress, QPointF{QPoint{500, 50}}, globalOf(QPoint{500, 50}), Qt::LeftButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
    QApplication::sendEvent(overlay.get(), &press);
    QMouseEvent move{QEvent::MouseMove, QPointF{QPoint{560, 150}}, globalOf(QPoint{560, 150}), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier};
    QApplication::sendEvent(overlay.get(), &move);
    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{QPoint{560, 150}}, globalOf(QPoint{560, 150}), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier};
    QApplication::sendEvent(overlay.get(), &release);
    EXPECT_EQ(overlay->selectedRegion(), QRect(500, 50, 101, 101)) << "Shift 拖出的方框在松手时弹回了自由形状";
}

/**
 * @brief 钉住尺寸输入框：S 开出并预填、回车按原左上角应用（越界夹回）、Esc 与点画布取消
 */
TEST_F(CaptureOverlayFixture, SizeEditorAppliesTypedSizeAtTheSameOrigin)
{
    auto overlay = makeOverlay();

    // 输入框用 deleteLater 收场：查「对象还在不在」就查得到收没收起
    // （isVisible 在没 show 过的遮罩下恒为假，拿它判会恒绿）
    const auto editorAlive = [&overlay] { return overlay->findChild<QLineEdit *>(QStringLiteral("overlaySizeEditor")) != nullptr; };

    dragOut(*overlay, QPoint{100, 100}, QPoint{199, 149});
    ASSERT_EQ(overlay->selectedRegion(), QRect(100, 100, 100, 50));

    pressKey(*overlay, Qt::Key_S);
    auto *editor = overlay->findChild<QLineEdit *>(QStringLiteral("overlaySizeEditor"));
    ASSERT_NE(editor, nullptr) << "S 没开出尺寸输入框";
    EXPECT_EQ(editor->text(), QStringLiteral("100x50")) << "输入框没预填当前尺寸";

    // 回车应用：左上角不动、输入框收起
    editor->setText(QStringLiteral("300x200"));
    QKeyEvent commit{QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier};
    QApplication::sendEvent(editor, &commit);
    EXPECT_EQ(overlay->selectedRegion(), QRect(100, 100, 300, 200)) << "回车没把尺寸应用上";
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_FALSE(editorAlive()) << "应用之后输入框没收起";

    // 敲不成尺寸就留在框里继续改：一个字母都不该落到选区上
    pressKey(*overlay, Qt::Key_S);
    editor = overlay->findChild<QLineEdit *>(QStringLiteral("overlaySizeEditor"));
    ASSERT_NE(editor, nullptr);
    editor->setText(QStringLiteral("abc"));
    QApplication::sendEvent(editor, &commit);
    EXPECT_EQ(overlay->selectedRegion(), QRect(100, 100, 300, 200)) << "解析不出的尺寸被当成别的东西应用了";
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(editorAlive()) << "敲不成尺寸时该留在框里继续改";

    // Esc 取消：选区不动、框收起
    QKeyEvent cancel{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QApplication::sendEvent(editor, &cancel);
    EXPECT_EQ(overlay->selectedRegion(), QRect(100, 100, 300, 200)) << "Esc 之后选区被改了";
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_FALSE(editorAlive()) << "Esc 没收起输入框";

    // 输一个大数：夹回屏内（这块屏 800×600，锚点 (100,100)）
    pressKey(*overlay, Qt::Key_S);
    editor = overlay->findChild<QLineEdit *>(QStringLiteral("overlaySizeEditor"));
    ASSERT_NE(editor, nullptr);
    editor->setText(QStringLiteral("5000x5000"));
    QApplication::sendEvent(editor, &commit);
    EXPECT_EQ(overlay->selectedRegion(), QRect(100, 100, 700, 500)) << "越界的尺寸没夹回屏内";
}

/**
 * @brief 钉住裸按 `P` 翻这一轮的「成品含不含指针」，且只报一次意图
 * @details 这一档是一次性的：不写配置，也不许在滚动长截图那一轮出现（按帧接图会把一枚指针复制成一串）。

 */
TEST_F(CaptureOverlayFixture, PointerKeyTogglesCursorForThisRound)
{
    auto overlay = makeOverlay();
    overlay->setCursorIncluded(false);

    int reports = 0;
    int last    = -1;
    QObject::connect(overlay.get(), &CaptureOverlay::cursorIncludeChanged,
                     [&](const bool included)
                     {
                         ++reports;
                         last = included ? 1 : 0;
                     });

    pressKey(*overlay, Qt::Key_P);
    EXPECT_TRUE(overlay->cursorIncluded().value_or(false)) << "按一次该翻成含指针";
    EXPECT_EQ(reports, 1) << "翻一次只报一次";
    EXPECT_EQ(last, 1);

    pressKey(*overlay, Qt::Key_P);
    EXPECT_FALSE(overlay->cursorIncluded().value_or(true)) << "再按一次翻回来";
    EXPECT_EQ(reports, 2);

    pressKey(*overlay, Qt::Key_P, Qt::ControlModifier);
    EXPECT_EQ(reports, 2) << "带修饰键的不认，别抢 Ctrl 那一类组合";

    // 滚动长截图那一轮初值就是空：按 P 不该凭空造出一档来
    auto longRound = makeOverlay();
    longRound->setCursorIncluded(std::nullopt);
    pressKey(*longRound, Qt::Key_P);
    EXPECT_FALSE(longRound->cursorIncluded().has_value()) << "这一轮没有这一档时，P 该交回 Qt";
}

/**
 * @brief 钉住「在设置页换过一颗键」之后遮罩跟着换，旧那颗当场让位
 * @details 这条钉的是「键位只住在 Gui/Controls/AppShortcuts.h 那份清册一处」：锁比例换成 G 之后，
 *          按 G 锁得上、按 A 不再动它。默认那颗由别几条判据钉着落对动作，这一条钉的是分派读的是清册；
 *          两处缺一个就会出现「设置改了没反应」或「按屏幕上写的键没反应」。
 */
TEST_F(CaptureOverlayFixture, ReboundAspectRatioKeyMovesToTheNewLetter)
{
    ASSERT_TRUE(static_cast<bool>(settings().setValue(GyanisScribe::Gui::appShortcutKey("Mask/AspectRatio").toStdString(), QStringLiteral("G")))) << "写入失败";

    auto overlay = makeOverlay();
    dragOut(*overlay, QPoint{100, 100}, QPoint{299, 199});
    ASSERT_EQ(overlay->selectedRegion(), QRect(100, 100, 200, 100)) << "前提不对：先自由框一块 200×100";

    pressKey(*overlay, Qt::Key_A);
    EXPECT_TRUE(overlay->lockedAspectRatio().isEmpty()) << "换走之后 A 还管锁比例，同一件事就有了两个入口";

    pressKey(*overlay, Qt::Key_G);
    EXPECT_EQ(overlay->lockedAspectRatio(), (QSize(2, 1))) << "G 没接住锁比例这一档";
}

/**
 * @brief 钉住「挑最近框过的选区」换成别的字母后，Shift 那个反向挡也一起搬过去
 * @details 这一档是唯一带反向挡的遮罩键：反向不是另一颗键，而是同一颗键多按一个 Shift。
 *          判据按「旧字母不再动、新字母正反向都动」三件事钉，缺一条就是清册与分派分家。
 */
TEST_F(CaptureOverlayFixture, ReboundRegionCycleKeyCarriesItsReverse)
{
    ASSERT_TRUE(static_cast<bool>(settings().setValue(GyanisScribe::Gui::appShortcutKey("Mask/RegionCycle").toStdString(), QStringLiteral("J")))) << "写入失败";

    auto overlay = makeOverlay();
    overlay->setRegionHistory({QRect{10, 10, 50, 40}, QRect{200, 200, 60, 60}});

    pressKey(*overlay, Qt::Key_R);
    EXPECT_TRUE(overlay->selectedRegion().isNull()) << "旧那颗 R 还在挑历史选区";

    pressKey(*overlay, Qt::Key_J);
    EXPECT_EQ(overlay->selectedRegion(), QRect(10, 10, 50, 40)) << "J 没接住「挑最近框过的选区」";

    pressKey(*overlay, Qt::Key_J, Qt::ShiftModifier);
    EXPECT_EQ(overlay->selectedRegion(), QRect(200, 200, 60, 60)) << "反向挡没跟着搬：Shift 那一下仍归旧字母";
}

/**
 * @brief 钉住指针那一档换了字母仍然只翻「这一轮」，而且这一轮没有这一档时照样交回 Qt
 * @details 换键不能把「不写回配置」与「长截图那一轮没有这一档」这两条规矩一起改掉——
 *          那两条是这一档存在的理由，判据把它们跟新字母一起钉住。
 */
TEST_F(CaptureOverlayFixture, ReboundPointerKeyKeepsTheRoundScopedRules)
{
    ASSERT_TRUE(static_cast<bool>(settings().setValue(GyanisScribe::Gui::appShortcutKey("Mask/PointerToggle").toStdString(), QStringLiteral("B")))) << "写入失败";

    int  reports = 0;
    auto overlay = makeOverlay();
    overlay->setCursorIncluded(false);
    QObject::connect(overlay.get(), &CaptureOverlay::cursorIncludeChanged, [&](const bool) { ++reports; });

    pressKey(*overlay, Qt::Key_P);
    EXPECT_EQ(reports, 0) << "换走之后 P 还在翻指针那一档";
    pressKey(*overlay, Qt::Key_B);
    EXPECT_EQ(reports, 1) << "B 没接住这一档";
    EXPECT_TRUE(overlay->cursorIncluded().value_or(false)) << "翻出来的不是「含指针」";

    // 滚动长截图那一轮初值就是空：换了字母也不该凭空造出一档来
    auto longRound = makeOverlay();
    longRound->setCursorIncluded(std::nullopt);
    pressKey(*longRound, Qt::Key_B);
    EXPECT_FALSE(longRound->cursorIncluded().has_value()) << "这一轮没有这一档时，新那颗键也该交回 Qt";
}
