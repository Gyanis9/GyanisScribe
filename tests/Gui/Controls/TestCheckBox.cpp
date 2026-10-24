#include "Gui/Controls/CheckBox.h"

#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
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

    /// 与 onAccent 足够接近的像素才算勾线本体；描边两端与主色混合出的过渡色不计
    bool isOnAccent(const QColor &color)
    {
        const auto expected = activePalette().onAccent;
        return std::abs(color.red() - expected.red()) <= 12 && std::abs(color.green() - expected.green()) <= 12 && std::abs(color.blue() - expected.blue()) <= 12;
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

    /// 按静态勾图资源自己画一张参照图：内容区与自绘勾取同一个矩形，判据不依赖 QSS 能否装载 SVG
    QImage staticCheckReference(const ThemeKind kind, const QSize indicatorSize)
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
 * @brief 钉住自绘复选框：QSS 静态勾图被抑制，勾选从缩小档弹回满幅
 * @details 关动画时勾选直接满幅；开动画后取消再勾选，缩放先落在 0.55，
 *          推进到弹簧稳定时长之后精确收在 1.0。
 */
TEST_F(CheckBoxFixture, SuppressesTheStaticQssCheckAndAnimatesTheCheck)
{
    qApp->setStyleSheet(ThemeManager::styleSheetFor(ThemeKind::Light));
    setTextThemeKind(ThemeKind::Light);

    CheckBox box;
    box.setAnimationsEnabled(false);
    box.setChecked(true);
    EXPECT_TRUE(box.property("animatedIndicator").toBool());
    EXPECT_DOUBLE_EQ(box.checkScale(), 1.0);

    box.setAnimationsEnabled(true);
    box.setChecked(false);
    box.setChecked(true);
    EXPECT_LT(box.checkScale(), 1.0) << "勾选没有从缩小档起步";
    box.advanceCheckAnimationForTesting(600);
    EXPECT_DOUBLE_EQ(box.checkScale(), 1.0) << "弹簧没有收在 1.0";
}

/**
 * @brief 钉住渲染结果：动画中段与收尾都看得见勾，静态勾图不会在弹起首帧叠上来
 * @details 抓图数的是指示器内的 onAccent 像素；静态勾图的判据用同一份 SVG 自建的参照图，
 *          不要求 QSS 的 image 资源在当前运行目录里真能装载。
 *          弹簧在 100ms 时已经走到约 0.93，自绘勾与静态勾的落点几乎重合，
 *          所以静态图是否被抑制只能在弹起首帧（0.55）判断：那一刻参照图上的每个实心像素
 *          都该是主色底，而不是勾色。
 *          收尾帧的自绘勾必须与参照图逐点同集：高对比度档的指示器描边是 2px，
 *          内缩也必须是 2px，勾才会落在 18px 内容区里；少缩一像素时勾会压出这一集合。
 */
TEST_F(CheckBoxFixture, PaintsTheCheckWhileAnimatedAndMatchesTheStaticPathAtRest)
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

        const QImage reference = staticCheckReference(kind, indicator.size());
        ASSERT_FALSE(reference.isNull()) << "静态勾图参照图画不出来";
        const QSet<QPoint> referencePixels = onAccentPixels(reference, QRect{QPoint{0, 0}, reference.size()});
        ASSERT_FALSE(referencePixels.isEmpty()) << "参照图上没有勾，判据没有可比的形状";

        box.setAnimationsEnabled(true);
        box.setChecked(false);
        box.setChecked(true);
        EXPECT_DOUBLE_EQ(box.checkScale(), 0.55) << "勾选没有从缩小档起步";

        // 弹起首帧：静态勾图若还在，它那些实心像素会出现在参照图的位置上
        const QImage       startShot   = host.grab().toImage();
        const QSet<QPoint> startPixels = onAccentPixels(startShot, indicator);
        for (const QPoint &point: referencePixels)
        {
            EXPECT_FALSE(startPixels.contains(point)) << "弹起首帧里还画着静态勾图的像素 (" << point.x() << ", " << point.y() << ")";
        }
        // 静态勾图长笔画的远端：按 18px 内容区算，落在指示器内 (14, 5) 附近
        const QPoint farEnd = indicator.topLeft() + QPoint(indicatorInset(kind) + 13, indicatorInset(kind) + 5);
        EXPECT_FALSE(isOnAccent(startShot.pixelColor(farEnd))) << "弹起首帧的勾已经伸到静态勾图的远端";

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
        const QSet<QPoint> endPixels = onAccentPixels(host.grab().toImage(), indicator);
        EXPECT_FALSE(endPixels.isEmpty()) << "收尾帧的指示器里没有勾色像素";
        EXPECT_EQ(endPixels, referencePixels) << "收尾的勾没有落在静态勾图的同一批像素上";

        host.hide();
    }
}
