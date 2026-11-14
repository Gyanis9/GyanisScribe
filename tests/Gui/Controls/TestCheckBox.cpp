#include "Gui/Controls/CheckBox.h"

#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QEnterEvent>
#include <QEvent>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <QStyle>
#include <QStyleOptionButton>
#include <QSvgRenderer>
#include <QWidget>

#include <cmath>

using GyanisScribe::Gui::activePalette;
using GyanisScribe::Gui::activeThemeKind;
using GyanisScribe::Gui::CheckBox;
using GyanisScribe::Gui::paletteFor;
using GyanisScribe::Gui::setTextThemeKind;
using GyanisScribe::Gui::ThemeKind;
using GyanisScribe::Gui::ThemeManager;

namespace
{
    /// 渲染用例共用的主题与样式表还原：两者都是进程级状态，留在用例后面会污染同进程的下一条
    class CheckBoxFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_theme      = activeThemeKind();
            m_styleSheet = qApp->styleSheet();
        }

        void TearDown() override
        {
            qApp->setStyleSheet(m_styleSheet);
            setTextThemeKind(m_theme);
        }

    private:
        ThemeKind m_theme = ThemeKind::Light;
        QString   m_styleSheet;
    };

    /// 指示器在宿主上的矩形：与焦点环用例同源，问样式要 SE_CheckBoxIndicator，不自己按 QSS 数值推算
    QRect indicatorOnHost(const QCheckBox &box, const QWidget &host)
    {
        QStyleOptionButton option;
        option.initFrom(&box);
        option.rect           = box.rect();
        const QRect indicator = box.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, &box);
        return {box.mapTo(&host, indicator.topLeft()), indicator.size()};
    }

    /// 两色各通道都在容差内：描边两端与底色混合出的过渡色不计
    bool isNear(const QColor &actual, const QColor &expected, const int tolerance = 12)
    {
        return std::abs(actual.red() - expected.red()) <= tolerance && std::abs(actual.green() - expected.green()) <= tolerance && std::abs(actual.blue() - expected.blue()) <= tolerance;
    }

    /// 与 onAccent 足够接近的像素才算勾线本体
    bool isOnAccent(const QColor &color)
    {
        return isNear(color, activePalette().onAccent);
    }

    /// 数一张抓图里落在指定矩形内的某色像素；坐标按整张图的绝对位置取
    int pixelsNear(const QImage &image, const QRect &area, const QColor &expected, const int tolerance = 12)
    {
        int count = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
        {
            for (int x = area.left(); x <= area.right(); ++x)
            {
                if (isNear(image.pixelColor(x, y), expected, tolerance))
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /// 收起一张抓图里落在指示器矩形内的 onAccent 像素，坐标相对指示器左上角
    QSet<QPoint> onAccentPixels(const QImage &image, const QRect &indicator)
    {
        QSet<QPoint> points;
        for (int y = 0; y < indicator.height(); ++y)
        {
            for (int x = 0; x < indicator.width(); ++x)
            {
                if (isOnAccent(image.pixelColor(indicator.x() + x, indicator.y() + y)))
                {
                    points.insert(QPoint(x, y));
                }
            }
        }
        return points;
    }

    /// 勾的绘制盒相对指示器的内缩：与 QSS 的描边宽度同档，高对比度档是 2px
    int indicatorInset(const ThemeKind kind)
    {
        return kind == ThemeKind::HighContrast ? 2 : 1;
    }

    /// 按勾形资源自己画一张参照图：内容区与自绘勾取同一个矩形，判据不依赖样式表能否装载图片
    QImage checkPathReference(const ThemeKind kind, const QSize indicatorSize)
    {
        const int inset = indicatorInset(kind);
        QImage    image{indicatorSize, QImage::Format_ARGB32_Premultiplied};
        image.fill(paletteFor(kind).accent);

        QSvgRenderer renderer{QStringLiteral(":/styles/CheckmarkOnAccent.svg")};
        if (!renderer.isValid())
        {
            return {};
        }

        QPainter painter{&image};
        renderer.render(&painter, QRectF(inset, inset, indicatorSize.width() - 2.0 * inset, indicatorSize.height() - 2.0 * inset));
        return image;
    }
} // namespace

/**
 * @brief 钉住自绘复选框的勾选动画：勾从缩小档弹回满幅
 * @details 指示器底与勾都由 Gui::CheckBox 自绘。关动画时勾选直接满幅；
 *          开动画后取消再勾选，缩放先落在 0.55，推进到弹簧稳定时长之后精确收在 1.0。
 */
TEST_F(CheckBoxFixture, AnimatesTheCheckFromTheCompressedStart)
{
    qApp->setStyleSheet(ThemeManager::styleSheetFor(ThemeKind::Light));
    setTextThemeKind(ThemeKind::Light);

    CheckBox box;
    box.setAnimationsEnabled(false);
    box.setChecked(true);
    EXPECT_DOUBLE_EQ(box.checkScale(), 1.0);

    box.setAnimationsEnabled(true);
    box.setChecked(false);
    box.setChecked(true);
    EXPECT_LT(box.checkScale(), 1.0) << "勾选没有从缩小档起步";
    box.advanceCheckAnimationForTesting(600);
    EXPECT_DOUBLE_EQ(box.checkScale(), 1.0) << "弹簧没有收在 1.0";
}

/**
 * @brief 钉住指示器按状态自绘填充与描边
 * @details 方框填色不再来自样式表：静息取 controlFill，悬停取 hover，勾选取 accent，
 *          禁用取 mutedFill。宿主铺红底，指示器中心必须是该状态对应的底；
 *          悬停用 WA_UnderMouse 构造，与样式表判悬停读的是同一个属性。
 *          左缘中点另核对边色：浅深档的悬停边是 1px borderStrong，高对比度档的静息、
 *          悬停与禁用边都是 2px（静息与禁用取 borderStrong，悬停取主色）。
 */
TEST_F(CheckBoxFixture, IndicatorFillFollowsTheState)
{
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast})
    {
        qApp->setStyleSheet(ThemeManager::styleSheetFor(kind));
        setTextThemeKind(kind);

        QWidget host;
        host.resize(240, 80);
        host.setAutoFillBackground(true);
        host.setPalette(QPalette{QColor{255, 0, 0}});
        CheckBox box{&host};
        box.setText(QStringLiteral("勾选项"));
        box.setGeometry(24, 24, 180, 24);
        host.show();
        QCoreApplication::processEvents();
        // show 之后第一个可聚焦控件（勾选框）会自动拿到焦点：先清掉，量的才是静息边
        box.clearFocus();
        box.update();
        QCoreApplication::processEvents();

        const QRect indicator = indicatorOnHost(box, host);
        ASSERT_FALSE(indicator.isEmpty()) << "取不到指示器矩形，判据没有落点";
        const auto centerOf = [&indicator](const QImage &image) { return image.pixelColor(indicator.center()); };
        // 左缘中点：描边按半个笔宽内缩，这一列像素被笔盖满，取到的就是纯边色
        const auto leftEdgeOf = [&indicator](const QImage &image) { return image.pixelColor(indicator.left(), indicator.center().y()); };
        // 高对比度档的边是 2px：第二列也必须还是边色，1px 边会在这一列露出填色
        const auto secondEdgeOf = [&indicator](const QImage &image) { return image.pixelColor(indicator.left() + 1, indicator.center().y()); };

        auto shot = host.grab().toImage();
        EXPECT_EQ(centerOf(shot), paletteFor(kind).controlFill) << static_cast<int>(kind) << " 静息指示器底不是 controlFill";
        if (kind == ThemeKind::HighContrast)
        {
            EXPECT_EQ(leftEdgeOf(shot), paletteFor(kind).borderStrong) << "高对比度静息指示器的左缘不是 borderStrong";
            EXPECT_EQ(secondEdgeOf(shot), paletteFor(kind).borderStrong) << "高对比度静息指示器的边只有 1px";
        }
        else
        {
            EXPECT_EQ(leftEdgeOf(shot), paletteFor(kind).border) << static_cast<int>(kind) << " 静息指示器没有描边，和分组卡底分不开";
        }

        box.setAttribute(Qt::WA_UnderMouse, true);
        box.update();
        QCoreApplication::processEvents();
        shot = host.grab().toImage();
        EXPECT_EQ(centerOf(shot), paletteFor(kind).hover) << static_cast<int>(kind) << " 悬停指示器底不是 hover";
        EXPECT_EQ(leftEdgeOf(shot), kind == ThemeKind::HighContrast ? paletteFor(kind).accent : paletteFor(kind).borderStrong) << static_cast<int>(kind) << " 悬停指示器的边色不对";
        if (kind == ThemeKind::HighContrast)
        {
            EXPECT_EQ(secondEdgeOf(shot), paletteFor(kind).accent) << "高对比度悬停指示器的边只有 1px";
        }
        box.setAttribute(Qt::WA_UnderMouse, false);

        box.setChecked(true);
        box.update();
        QCoreApplication::processEvents();
        shot = host.grab().toImage();
        EXPECT_EQ(centerOf(shot), paletteFor(kind).accent) << static_cast<int>(kind) << " 勾选指示器底不是主色";

        box.setEnabled(false);
        box.update();
        QCoreApplication::processEvents();
        shot = host.grab().toImage();
        EXPECT_EQ(centerOf(shot), paletteFor(kind).mutedFill) << static_cast<int>(kind) << " 禁用指示器底不是 mutedFill";
        if (kind == ThemeKind::HighContrast)
        {
            EXPECT_EQ(leftEdgeOf(shot), paletteFor(kind).borderStrong) << "禁用且勾选的高对比度指示器不该再用主色边";
            EXPECT_EQ(secondEdgeOf(shot), paletteFor(kind).borderStrong) << "禁用且勾选的高对比度指示器的边只有 1px";
        }

        host.hide();
    }
}

/**
 * @brief 钉住悬停态靠进入与离开事件自己请求重绘
 * @details 样式表撤掉 QCheckBox::indicator 的 :hover 规则后，悬停底与描边只由自绘代码按
 *          underMouse() 决定，而 Qt 的进入与离开事件不会顺带重绘控件。这里各发一次 Enter
 *          与 Leave，只转事件循环、不手动 update()：两回合都必须出现绘制事件，
 *          否则悬停色要等下一次别的重绘才出现，或因缺少刷新留在屏上。
 */
TEST_F(CheckBoxFixture, EnterAndLeaveScheduleTheirOwnRepaints)
{
    qApp->setStyleSheet(ThemeManager::styleSheetFor(ThemeKind::Light));
    setTextThemeKind(ThemeKind::Light);

    QWidget host;
    host.resize(240, 80);
    CheckBox box{&host};
    box.setText(QStringLiteral("勾选项"));
    box.setGeometry(24, 24, 180, 24);
    host.show();
    QCoreApplication::processEvents();

    struct PaintCounter final : QObject
    {
        int paints = 0;

        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Paint)
            {
                ++paints;
            }
            return false;
        }
    } counter;
    box.installEventFilter(&counter);
    QCoreApplication::processEvents();
    counter.paints = 0;

    QEnterEvent enter{QPointF{1.0, 1.0}, box.mapToGlobal(QPoint{1, 1}), QPointF{1.0, 1.0}};
    QApplication::sendEvent(&box, &enter);
    QCoreApplication::processEvents();
    EXPECT_GT(counter.paints, 0) << "进入只改了悬停态没请求重绘：悬停底与描边要等下一次别的重绘才出现";

    counter.paints = 0;
    QEvent leave{QEvent::Leave};
    QApplication::sendEvent(&box, &leave);
    QCoreApplication::processEvents();
    EXPECT_GT(counter.paints, 0) << "离开没请求重绘：悬停底与描边会留在屏上";
}

/**
 * @brief 钉住勾的绘制：动画中段与收尾都看得见勾，收尾与勾形资源逐点同集
 * @details 抓图数的是指示器内的 onAccent 像素；勾形参照图用同一份 SVG 自建，
 *          样式表不再引用这张图，参照图只作为勾形的判据。
 *          弹簧在 100ms 时已经走到约 0.93，自绘勾与满幅勾的落点几乎重合，
 *          所以「弹起首帧仍收在缩小档」只能在缩放为 0.55 时判断：那一刻满幅勾的每个实心像素
 *          都该是主色底，而不是勾色。
 *          收尾帧的自绘勾必须与参照图逐点同集：高对比度档的指示器是 2px 边，
 *          内缩也必须是 2px，勾才会落在 18px 内容区里；少缩一像素时勾会压出这一集合。
 */
TEST_F(CheckBoxFixture, PaintsTheCheckWhileAnimatedAndMatchesTheReferencePathAtRest)
{
    for (const auto kind: {ThemeKind::Light, ThemeKind::HighContrast})
    {
        qApp->setStyleSheet(ThemeManager::styleSheetFor(kind));
        setTextThemeKind(kind);

        QWidget host;
        host.resize(240, 80);
        CheckBox box{&host};
        box.setText(QStringLiteral("勾选项"));
        box.setGeometry(24, 24, 180, 24);
        box.setAnimationsEnabled(false);
        box.setChecked(true);
        host.show();
        QCoreApplication::processEvents();

        const QRect indicator = indicatorOnHost(box, host);
        ASSERT_FALSE(indicator.isEmpty()) << "取不到指示器矩形，判据没有落点";

        const QImage reference = checkPathReference(kind, indicator.size());
        ASSERT_FALSE(reference.isNull()) << "勾形参照图画不出来";
        const QSet<QPoint> referencePixels = onAccentPixels(reference, QRect{QPoint{0, 0}, reference.size()});
        ASSERT_FALSE(referencePixels.isEmpty()) << "参照图上没有勾，判据没有可比的形状";

        box.setAnimationsEnabled(true);
        box.setChecked(false);
        box.setChecked(true);
        EXPECT_DOUBLE_EQ(box.checkScale(), 0.55) << "勾选没有从缩小档起步";

        // 弹起首帧：勾仍收在缩小档，满幅勾的像素位置此刻应还是主色底
        const QImage       startShot   = host.grab().toImage();
        const QSet<QPoint> startPixels = onAccentPixels(startShot, indicator);
        for (const QPoint &point: referencePixels)
        {
            EXPECT_FALSE(startPixels.contains(point)) << "弹起首帧里已经画着满幅勾的像素 (" << point.x() << ", " << point.y() << ")";
        }
        // 满幅勾长笔画的远端：按 18px 内容区算，落在指示器内 (14, 5) 附近
        const QPoint farEnd = indicator.topLeft() + QPoint(indicatorInset(kind) + 13, indicatorInset(kind) + 5);
        EXPECT_FALSE(isOnAccent(startShot.pixelColor(farEnd))) << "弹起首帧的勾已经伸到满幅勾的远端";

        box.advanceCheckAnimationForTesting(100);
        EXPECT_GT(box.checkScale(), 0.55);
        EXPECT_LT(box.checkScale(), 1.0);
        const QSet<QPoint> midPixels = onAccentPixels(host.grab().toImage(), indicator);
        EXPECT_FALSE(midPixels.isEmpty()) << "动画中段的指示器里没有勾色像素";

        // 中途取消勾选不播动画：勾已经消失，缩放立即回到满幅
        box.setChecked(false);
        EXPECT_DOUBLE_EQ(box.checkScale(), 1.0) << "取消勾选还在播缩放动画";

        box.setChecked(true);
        box.advanceCheckAnimationForTesting(600);
        EXPECT_DOUBLE_EQ(box.checkScale(), 1.0) << "弹簧没有收在 1.0";
        const QImage       endShot   = host.grab().toImage();
        const QSet<QPoint> endPixels = onAccentPixels(endShot, indicator);
        EXPECT_FALSE(endPixels.isEmpty()) << "收尾帧的指示器里没有勾色像素";
        EXPECT_EQ(endPixels, referencePixels) << "收尾的勾没有落在参照勾图的同一批像素上";
        EXPECT_EQ(endShot.pixelColor(indicator.center()), paletteFor(kind).accent) << "勾选指示器的底不是自绘的主色";

        host.hide();
    }
}

/**
 * @brief 钉住勾选且禁用时勾仍看得见
 * @details 禁用档的指示器底由自绘代码落在 mutedFill（高对比度档是纯白），勾若还按 onAccent 画，
 *          浅色档就是浅底白勾、高对比度档是白底白勾，等于没有勾。这三档都按次要文本色画。
 */
TEST_F(CheckBoxFixture, DisabledCheckedBoxStillPaintsAVisibleCheck)
{
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast})
    {
        qApp->setStyleSheet(ThemeManager::styleSheetFor(kind));
        setTextThemeKind(kind);

        QWidget host;
        host.resize(240, 80);
        CheckBox box{&host};
        box.setText(QStringLiteral("不可用的勾选项"));
        box.setGeometry(24, 24, 200, 24);
        box.setAnimationsEnabled(false);
        box.setChecked(true);
        box.setEnabled(false);
        host.show();
        QCoreApplication::processEvents();

        const QRect indicator = indicatorOnHost(box, host);
        ASSERT_FALSE(indicator.isEmpty()) << "取不到指示器矩形，判据没有落点";
        const auto shot = host.grab().toImage();

        const auto secondary = paletteFor(kind).secondaryText;
        EXPECT_GT(pixelsNear(shot, indicator, secondary), 3) << static_cast<int>(kind) << " 禁用勾选的勾看不见";
        // 浅色档的禁用底与 onAccent 离得远，这条负向判据才有意义；深色与高对比度档的禁用底
        // 本身就接近 onAccent（近黑/纯白），按颜色数像素分不出「白勾」与「白底」
        if (kind == ThemeKind::Light)
        {
            EXPECT_EQ(pixelsNear(shot, indicator, paletteFor(kind).onAccent), 0) << "浅色档的禁用勾还在用 onAccent，浅底白勾等于没画";
        }

        host.hide();
    }
}
