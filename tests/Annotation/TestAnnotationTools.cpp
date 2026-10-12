#include "Annotation/AnnotationEngine.h"
#include "Annotation/PenParameters.h"

#include "Annotation/HighlightTool.h"
#include "Annotation/ImageBlur.h"
#include "Annotation/MosaicTool.h"
#include "Annotation/NumberTool.h"
#include "Annotation/PenTool.h"
#include "Annotation/TextTool.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QString>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using GyanisScribe::Annotation::AnnotationEngine;
using GyanisScribe::Annotation::AnnotationSelection;
using GyanisScribe::Annotation::NumberTool;
using GyanisScribe::Annotation::PenParameters;
using GyanisScribe::Annotation::PenTool;
using GyanisScribe::Annotation::ToolKind;
using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::ErrorCode;

namespace
{
    // 字体量度与图形绘制都要求应用对象就位，这里用 QApplication：只链 Qt6::Widgets 不够，
    // 只建 QCoreApplication 时 QFontMetrics 在部分平台上给不出真实行高
    std::unique_ptr<QApplication> application;

    class AnnotationFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestAnnotationTools";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 400×200 逻辑、dpr 1 的画布：物理像素里上半涂白、下半涂黑，方便按颜色判位置
            CapturedImage canvas;
            canvas.image = QImage(400, 200, QImage::Format_ARGB32_Premultiplied);
            canvas.image.fill(QColor(255, 255, 255));
            for (int y = 100; y < 200; ++y)
            {
                for (int x = 0; x < 400; ++x)
                {
                    canvas.image.setPixelColor(x, y, QColor(0, 0, 0));
                }
            }
            canvas.devicePixelRatio = 1.0;
            canvas.logicalSize      = QSize(400, 200);
            canvas.screenName       = QStringLiteral("FakeScreen-1");

            m_original = canvas.image.copy();
            m_engine.loadCanvas(canvas);
        }

        AnnotationEngine m_engine;
        QImage           m_original; ///< 未经标注的底图，用于数「改了哪些像素」
    };

    /**
     * @brief 钉住高斯模糊把跨边界的对比糊掉，且只动框住的那一块
     * @details 靶子必须是**底图自带**的分界（这个装置上半白、下半黑，分界在 y=100）：
     *          马赛克与模糊都取底图像素、会先把底下的标注盖掉，拿标注当靶子的话
     *          「不糊」也能让对比度变化，那条用例就白写了（此条第一版正是这么假绿的）。
     *          判据取跨分界线的相邻像素亮度差：锐利时是 255，糊过之后掉到一半以下。
     */
    TEST_F(AnnotationFixture, BlurHalvesTheEdgeContrastInsideItsRegionOnly)
    {
        const auto before               = m_engine.exportImage();
        const auto maximumVerticalDelta = [](const QImage &image, const QRect &region)
        {
            int maximum = 0;
            for (int x = region.left(); x <= region.right(); ++x)
            {
                for (int y = region.top(); y < region.bottom(); ++y)
                {
                    maximum = std::max(maximum, std::abs(qGray(image.pixel(x, y)) - qGray(image.pixel(x, y + 1))));
                }
            }
            return maximum;
        };
        const QRect blurredRegion{100, 70, 100, 60}; // 压住底图 y=100 那条黑白分界
        ASSERT_GT(maximumVerticalDelta(before, blurredRegion), 200) << "靶子不对：底图分界上本该是黑白相邻";

        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Blur).has_value());
        m_engine.beginStroke(QPoint(100, 70));
        m_engine.moveStroke(QPoint(200, 130));
        ASSERT_TRUE(m_engine.endStroke().has_value());

        const auto after = m_engine.exportImage();
        EXPECT_LT(maximumVerticalDelta(after, blurredRegion), maximumVerticalDelta(before, blurredRegion) / 2) << "跨分界线的对比没降下来：模糊没生效";

        // 框外逐像素对比：模糊不许越界
        for (int y = 0; y < after.height(); y += 7)
        {
            for (int x = 210; x < after.width(); x += 7)
            {
                EXPECT_EQ(after.pixelColor(x, y), before.pixelColor(x, y)) << "框外的像素被糊到了：(" << x << "," << y << ")";
            }
        }
    }


    /**
     * @brief 钉住方框模糊的两条底性：均匀图糊完还是均匀（边缘不渗黑），半径非正原样交回
     * @details 边缘不夹取采样时四边会混进黑色，看上去像多了一圈暗边——这条判据就盯那一圈。
     */
    TEST(ImageBlur, KeepsUniformImagesUniformAndIgnoresNonPositiveRadius)
    {
        QImage uniform{QSize{40, 24}, QImage::Format_ARGB32};
        uniform.fill(QColor(128, 128, 128));

        const auto blurred = GyanisScribe::Annotation::boxBlurred(uniform, 4);
        for (int y = 0; y < blurred.height(); ++y)
        {
            for (int x = 0; x < blurred.width(); ++x)
            {
                EXPECT_EQ(blurred.pixelColor(x, y), QColor(128, 128, 128)) << "(" << x << "," << y << ") 渗进了别的颜色";
            }
        }

        EXPECT_EQ(GyanisScribe::Annotation::boxBlurred(uniform, 0), uniform) << "半径非正该原样交回";
        EXPECT_TRUE(GyanisScribe::Annotation::boxBlurred(QImage{}, 4).isNull());
    }

    /**
     * @brief 钉住橡皮擦沿笔迹擦回底图，且只擦到笔迹那一块
     * @details 三条判据各打一处：擦区正中要看得到**底图自己的**像素（这一带底图是黑的，
     *          写成白色或透明都会被抓到，写「什么都不画」则留着红带也被抓到）；擦区之外的带子
     *          必须原样留着；探针红点落在擦笔迹外接框里、却在笔迹半径之外，用来看住是「按笔迹抠」
     *          还是「把整个外接框盖下去」——后者会把探针也盖掉。
     */
    TEST_F(AnnotationFixture, EraserRevealsTheBaseImageOnlyAlongItsStroke)
    {
        ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0}).has_value());

        // 红色横带压在黑色半区上（底图 y=100 以下是黑的），另在白色半区点一个探针红点
        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Pen).has_value());
        m_engine.beginStroke(QPoint(40, 150));
        m_engine.moveStroke(QPoint(360, 150));
        ASSERT_TRUE(m_engine.endStroke().has_value());
        m_engine.beginStroke(QPoint(280, 40));
        ASSERT_TRUE(m_engine.endStroke().has_value());

        const auto annotated = m_engine.exportImage();
        ASSERT_EQ(annotated.pixelColor(300, 150), QColor(255, 0, 0)) << "靶子不对：横带该是红的";
        ASSERT_EQ(annotated.pixelColor(280, 40), QColor(255, 0, 0)) << "靶子不对：探针红点没画上";

        // 竖直擦一笔穿过横带：线宽 8 的橡皮是 8×6=48 宽，覆盖 x∈[276,324]
        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Eraser).has_value());
        m_engine.beginStroke(QPoint(300, 150));
        m_engine.moveStroke(QPoint(300, 60));
        ASSERT_TRUE(m_engine.endStroke().has_value());

        const auto erased = m_engine.exportImage();
        EXPECT_EQ(erased.pixelColor(300, 150), QColor(0, 0, 0)) << "擦过的地方该露出底图那块黑，而不是被涂成别的颜色或原样留着红";
        EXPECT_EQ(erased.pixelColor(200, 150), QColor(255, 0, 0)) << "擦区之外的横带被连带擦掉了";
        EXPECT_EQ(erased.pixelColor(280, 40), QColor(255, 0, 0)) << "探针在外接框里但不在笔迹上，被整框盖下去了：抠图丢的不是笔迹形状";

        // 单点落笔也要擦出一个圆点：擦的是 (250,150) 一带，30 像素外的 (220,150) 不该动
        m_engine.beginStroke(QPoint(250, 150));
        ASSERT_TRUE(m_engine.endStroke().has_value());
        const auto dotErased = m_engine.exportImage();
        EXPECT_EQ(dotErased.pixelColor(250, 150), QColor(0, 0, 0)) << "单点擦除没有落下任何东西";
        EXPECT_EQ(dotErased.pixelColor(220, 150), QColor(255, 0, 0)) << "单点擦除的影响范围超出了自己的半径";

        // 一笔擦除就是一步撤销：撤掉刚才那一下，红点该回来，而上一笔擦除照旧
        ASSERT_TRUE(m_engine.undo().has_value());
        const auto undone = m_engine.exportImage();
        EXPECT_EQ(undone.pixelColor(250, 150), QColor(255, 0, 0)) << "撤销没把单点擦除收回来";
        EXPECT_EQ(undone.pixelColor(300, 150), QColor(0, 0, 0)) << "撤销一次却收回了不止一笔";
    }

    /**
     * @brief 钉住橡皮擦压在先画的图形之上
     * @details 层叠号在落定那一刻发，橡皮这一笔落在线之后，就压住了那条线：擦过的地方该断。
     *          反过来把橡皮画在线之前的话，线会盖在擦层上面，擦不断地判据当场变红。
     */
    TEST_F(AnnotationFixture, EraserSitsAboveEveryOtherTool)
    {
        ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0}).has_value());
        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Line).has_value());
        m_engine.beginStroke(QPoint(60, 40));
        m_engine.moveStroke(QPoint(340, 40));
        ASSERT_TRUE(m_engine.endStroke().has_value());
        ASSERT_EQ(m_engine.exportImage().pixelColor(200, 40), QColor(255, 0, 0)) << "靶子不对：直线该是红的";

        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Eraser).has_value());
        m_engine.beginStroke(QPoint(200, 20));
        m_engine.moveStroke(QPoint(200, 60));
        ASSERT_TRUE(m_engine.endStroke().has_value());

        const auto erased = m_engine.exportImage();
        EXPECT_EQ(erased.pixelColor(200, 40), QColor(255, 255, 255)) << "擦层没压住先画的直线：擦过的地方要回到白底";
        EXPECT_EQ(erased.pixelColor(100, 40), QColor(255, 0, 0)) << "擦区之外的直线被连带擦掉了";
    }


    /**
     * @brief 钉住矩形与椭圆按各自的填充档上笔刷：无（内里不动）、半透明（浅浅一层）、实心（就是描边色）
     * @details 档记在图形自己身上，所以三档能同框出现：一条空心、一条半透、一条实心。
     */
    TEST_F(AnnotationFixture, ShapesRespectTheirOwnFillStyle)
    {
        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Rectangle).has_value());

        // 无填充：内里保持底图的白
        ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0}).has_value());
        m_engine.beginStroke(QPoint(20, 20));
        m_engine.moveStroke(QPoint(120, 120));
        ASSERT_TRUE(m_engine.endStroke().has_value());
        EXPECT_EQ(m_engine.exportImage().pixelColor(70, 70), QColor(255, 255, 255)) << "无填充档的内里被动过了";

        // 半透明：内里是红压在白上的一层浅红（既不是纯白也不是纯红）
        ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 1}).has_value());
        m_engine.beginStroke(QPoint(150, 20));
        m_engine.moveStroke(QPoint(250, 120));
        ASSERT_TRUE(m_engine.endStroke().has_value());
        const auto translucent = m_engine.exportImage().pixelColor(200, 70);
        EXPECT_GT(translucent.red(), 200) << "半透明档没压上一层红";
        EXPECT_LT(translucent.green(), 255) << "半透明档的内里与底图一模一样，等于没填";
        EXPECT_GT(translucent.green(), 100) << "半透明档成了实心：底下的白被完全盖住";

        // 实心：内里就是描边色
        ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 2}).has_value());
        m_engine.beginStroke(QPoint(280, 20));
        m_engine.moveStroke(QPoint(380, 120));
        ASSERT_TRUE(m_engine.endStroke().has_value());
        EXPECT_EQ(m_engine.exportImage().pixelColor(330, 70), QColor(255, 0, 0)) << "实心档的内里不是描边色";
    }


    /**
     * @brief 钉住折线的点击落点式交互：点一下落一个折点、松手不落定、收笔才整条进图
     * @details 松手不落定是这一档与别的工具最大的不同：`endStroke` 这时该回 Busy（在途图形还在），
     *          双击收笔走的 `finishPendingStroke` 才把整条交出来。最后那段（末折点到预览点）在收笔时并入。
     */
    TEST_F(AnnotationFixture, PolylineCollectsCornersUntilItIsFinished)
    {
        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Polyline).has_value());

        // 第一下：起头，松手不该落定
        m_engine.beginStroke(QPoint(40, 40));
        const auto unfinished = m_engine.endStroke();
        ASSERT_FALSE(unfinished.has_value()) << "半途松手就把折线落定了";
        EXPECT_EQ(unfinished.error().code, ErrorCode::Busy) << "该用 Busy 把「还没收」与「真没画成」分开";

        // 第二、第三个折点；中间那一次移动只当预览
        m_engine.beginStroke(QPoint(140, 40));
        ASSERT_FALSE(m_engine.endStroke().has_value());
        m_engine.beginStroke(QPoint(140, 120));
        ASSERT_FALSE(m_engine.endStroke().has_value());
        m_engine.moveStroke(QPoint(240, 120));

        // 收笔：整条落定，最后那段并入
        const auto finished = m_engine.finishPendingStroke();
        ASSERT_TRUE(finished.has_value()) << finished.error().message;
        EXPECT_EQ(finished->shapeIndex, 0);

        const auto exported = m_engine.exportImage();
        EXPECT_NE(exported.pixelColor(90, 40), QColor(255, 255, 255)) << "第一段没画出来";
        EXPECT_NE(exported.pixelColor(140, 80), QColor(255, 255, 255)) << "第二段没画出来";
        EXPECT_NE(exported.pixelColor(190, 120), QColor(255, 255, 255)) << "收笔时末段没并入";
        EXPECT_EQ(exported.pixelColor(240, 40), QColor(255, 255, 255)) << "折线跑到了折点之外";

        // 收完笔就没了在途图形：再收一次要被拒
        const auto again = m_engine.finishPendingStroke();
        ASSERT_FALSE(again.has_value());
        EXPECT_EQ(again.error().code, ErrorCode::InvalidArgument);

        EXPECT_TRUE(m_engine.undo().has_value());
        EXPECT_EQ(m_engine.exportImage().pixelColor(90, 40), QColor(255, 255, 255)) << "撤销没把整条折线收回去";
    }

    /**
     * @brief 钉住折线的落定下限：只有一个折点不算数
     */
    TEST_F(AnnotationFixture, SingleCornerPolylineIsNotUsable)
    {
        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Polyline).has_value());
        m_engine.beginStroke(QPoint(50, 50));
        ASSERT_FALSE(m_engine.endStroke().has_value());

        const auto finished = m_engine.finishPendingStroke();
        ASSERT_FALSE(finished.has_value()) << "一个折点也落定了";
        EXPECT_EQ(finished.error().code, ErrorCode::InvalidArgument);
        EXPECT_FALSE(m_engine.canUndo()) << "没落定的折线不该进撤销栈";
    }

} // namespace

/**
 * @brief 钉住画笔的采样：一路走过的点都要留下，重复采样点丢掉
 */
TEST_F(AnnotationFixture, PenKeepsEveryDistinctSamplePoint)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Pen).has_value());
    m_engine.beginStroke(QPoint(10, 10));
    m_engine.moveStroke(QPoint(30, 10));
    m_engine.moveStroke(QPoint(30, 10)); // 与上一点重合，不该再记一次
    m_engine.moveStroke(QPoint(30, 40));

    const auto committed = m_engine.endStroke();
    ASSERT_TRUE(committed.has_value()) << "拖出一条折线应当落定";
    EXPECT_EQ(committed->shapeIndex, 0);

    const auto shape = m_engine.exportImage();
    EXPECT_FALSE(shape.isNull());
    // 笔迹本身：导出图上 (30,25) 这一段应当被画过（原来这里是白的）
    EXPECT_NE(m_engine.exportImage().pixelColor(30, 25), QColor(255, 255, 255)) << "折线没进导出图";
}

/**
 * @brief 钉住退化的一笔不落定：点一下就松手不该产生图形
 */
TEST_F(AnnotationFixture, DegenerateStrokeIsRejected)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Rectangle).has_value());
    m_engine.beginStroke(QPoint(50, 50));
    m_engine.moveStroke(QPoint(50, 50));

    const auto committed = m_engine.endStroke();
    ASSERT_FALSE(committed.has_value()) << "零尺寸矩形不该落定，否则撤销栈里会出现看不见的条目";
    EXPECT_EQ(committed.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(committed.error().message.find("没有落定"), std::string::npos) << "文案要说明这一笔为什么不算，实际：" << committed.error().message;
    EXPECT_FALSE(m_engine.canUndo()) << "退化的一笔不该进撤销栈";
}

/**
 * @brief 钉住箭头的命中判据走线距而不是外接框
 */
TEST_F(AnnotationFixture, ArrowHitTestFollowsTheShaft)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Arrow).has_value());
    m_engine.beginStroke(QPoint(0, 0));
    m_engine.moveStroke(QPoint(200, 0));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    EXPECT_TRUE(m_engine.selectionAt(QPoint(100, 0)).has_value()) << "箭杆正中该被点中";
    EXPECT_FALSE(m_engine.selectionAt(QPoint(100, 100)).has_value()) << "外接框里的空白处不该算命中";
    EXPECT_FALSE(m_engine.selectionAt(QPoint(300, 0)).has_value()) << "箭头延长线上不该点得中";
}

/**
 * @brief 钉住箭头头部是实心三角：两翼张在尖端往回约 12 格的地方，而不是贴在尖端旁的一小片
 * @details 线宽 4 时头长 14，两翼各与箭杆成 30°，三角在尖端往回 12 格处最宽（±7）。
 *          (170,63) 只可能落在三角里：离杆身 1.5 格、离三条边都在 1.6 格以上——
 *          两翼如果算错成「从尖端往回 14 的底边点再前伸 14」这个位置是白的，画出来就是一根光杆。
 */
TEST_F(AnnotationFixture, ArrowHeadIsASolidTriangle)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Arrow).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(20, 60));
    m_engine.moveStroke(QPoint(180, 60));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    const auto drawn = m_engine.exportImage();
    EXPECT_EQ(drawn.pixelColor(170, 63), QColor(255, 0, 0)) << "头部三角在这个位置没有墨：两翼贴在尖端旁，画出来还是一根光杆";
    EXPECT_EQ(drawn.pixelColor(172, 60), QColor(255, 0, 0)) << "尖端往回一点的位置该在三角里";
    EXPECT_EQ(drawn.pixelColor(30, 63), QColor(255, 255, 255)) << "单向箭头不该在起点端长出第二个头";
}

/**
 * @brief 钉住双向箭头：doubleHeaded 打开后两端各有一个实心三角，箭杆仍然连得到
 * @details 与上一条同一组坐标互为对照：只把 doubleHeaded 打开，(30,63) 就从白变红。
 *          这条同时走完「参数下发 → 新画的图形带上开关 → 两端各画一个头」整条路。
 */
TEST_F(AnnotationFixture, DoubleHeadedArrowDrawsAHeadOnBothEnds)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Arrow).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0, .doubleHeaded = true}).has_value());
    m_engine.beginStroke(QPoint(20, 60));
    m_engine.moveStroke(QPoint(180, 60));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    const auto drawn = m_engine.exportImage();
    EXPECT_EQ(drawn.pixelColor(170, 63), QColor(255, 0, 0)) << "终点端没画头";
    EXPECT_EQ(drawn.pixelColor(30, 63), QColor(255, 0, 0)) << "起点端没画第二个头：双向开关没落到新画的图形上";
    EXPECT_EQ(drawn.pixelColor(100, 60), QColor(255, 0, 0)) << "两个头之间箭杆断了";
}

/**
 * @brief 钉住直线的命中走线距而不是外接框
 * @details 与箭头同一套判据：外接框会把一条斜线的整片空白矩形算成命中，用户点 nowhere 却选中了这条线。
 */
TEST_F(AnnotationFixture, LineHitTestFollowsTheSegment)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Line).has_value());
    m_engine.beginStroke(QPoint(0, 0));
    m_engine.moveStroke(QPoint(120, 120));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    EXPECT_TRUE(m_engine.selectionAt(QPoint(60, 60)).has_value()) << "线的正中该被点中";
    EXPECT_FALSE(m_engine.selectionAt(QPoint(110, 20)).has_value()) << "外接框里的空白处不该算命中";
    EXPECT_FALSE(m_engine.selectionAt(QPoint(200, 200)).has_value()) << "延长线上不该点得中";
}

/**
 * @brief 钉住两端重合的「直线」不落定
 * @details 两点距离不到 4 格时这条线没有长度：落进撤销栈会让用户按一次撤销却看不到画面有任何变化。
 */
TEST_F(AnnotationFixture, ZeroLengthLineIsNotCommitted)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Line).has_value());
    m_engine.beginStroke(QPoint(50, 50));
    m_engine.moveStroke(QPoint(51, 51));

    const auto committed = m_engine.endStroke();
    ASSERT_FALSE(committed.has_value()) << "退化的一笔被当成了落定";
    EXPECT_EQ(committed.error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(m_engine.canUndo()) << "退化的一笔不该进撤销栈";
}

/**
 * @brief 钉住椭圆命中在环上，四角空心处点不中
 */
TEST_F(AnnotationFixture, EllipseHitTestIsARingNotABoundingBox)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Ellipse).has_value());
    m_engine.beginStroke(QPoint(100, 50));
    m_engine.moveStroke(QPoint(300, 150));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    EXPECT_TRUE(m_engine.selectionAt(QPoint(200, 50)).has_value()) << "椭圆顶边正中该点得中";
    EXPECT_FALSE(m_engine.selectionAt(QPoint(105, 55)).has_value()) << "外接框的左上角是空心区，不该点得中";
}

/**
 * @brief 钉住序号在落定那一刻取号，撤销一条后接着那个号
 */
TEST_F(AnnotationFixture, NumberTakesItsLabelAtCommitTime)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Number).has_value());
    for (int index = 0; index < 3; ++index)
    {
        m_engine.beginStroke(QPoint(20 + index * 30, 20));
        ASSERT_TRUE(m_engine.endStroke().has_value());
    }

    ASSERT_TRUE(m_engine.undo().has_value()); // 撤掉 3 号
    m_engine.beginStroke(QPoint(200, 200));
    const auto renumbered = m_engine.endStroke();
    ASSERT_TRUE(renumbered.has_value());

    // 第三条现在又该是 3：按当时的条数 +1，不是按「一共画过几次」
    const auto exported = m_engine.exportImage();
    EXPECT_FALSE(exported.isNull());
    EXPECT_TRUE(m_engine.selectionAt(QPoint(200, 200)).has_value()) << "新序号牌该在它落点那一处";
}

/**
 * @brief 钉住文本的两条拒绝面与一条成功路
 */
TEST_F(AnnotationFixture, TextRequiresTheTextToolAndNonEmptyContent)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Rectangle).has_value());
    const auto wrongTool = m_engine.setPendingText(QStringLiteral("注意"));
    ASSERT_FALSE(wrongTool.has_value()) << "非文字工具时接受了文本，用户输入的内容会跑到矩形上";
    EXPECT_EQ(wrongTool.error().code, ErrorCode::InvalidArgument);

    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Text).has_value());
    m_engine.beginStroke(QPoint(40, 40));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("")).has_value());
    EXPECT_FALSE(m_engine.endStroke().has_value()) << "空文本不该落定成一条图形";

    m_engine.beginStroke(QPoint(40, 40));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("注意")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());
    EXPECT_NE(m_engine.exportImage().pixelColor(41, 41), QColor(255, 255, 255)) << "文字没画进导出图";
}

/**
 * @brief 钉住马赛克确实改掉了框内像素，且没越界改动别处
 */
TEST_F(AnnotationFixture, MosaicCoversOnlyItsRegion)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Mosaic).has_value());
    m_engine.beginStroke(QPoint(10, 10));
    m_engine.moveStroke(QPoint(110, 90));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    const auto exported = m_engine.exportImage();
    // 框内原来是一片纯白，打上马赛克后仍应是白——判据改看「相邻像素被混在一起」：
    // 跨过半分界线的框才会出现黑白混合，这里另用一条压分界线的框验那一路
    EXPECT_EQ(exported.pixelColor(60, 50), QColor(255, 255, 255)) << "纯白区打马赛克不该变黑";

    m_engine.beginStroke(QPoint(150, 60));
    m_engine.moveStroke(QPoint(250, 140));
    ASSERT_TRUE(m_engine.endStroke().has_value());
    const auto crossed       = m_engine.exportImage();
    int        changedPixels = 0;
    for (int y = 60; y <= 140; ++y)
    {
        for (int x = 150; x <= 250; ++x)
        {
            if (crossed.pixelColor(x, y) != m_original.pixelColor(x, y))
            {
                ++changedPixels;
            }
        }
    }
    EXPECT_GT(changedPixels, 0) << "框内一个像素都没变，说明马赛克根本没画上去";
    // 方块化的窗格尺寸来自实现里的除法（101 宽分成 8 块 ≈ 12.6），拿固定的 12 去探会跨到两块上，
    // 所以这里改用「同色来源必得同色输出」这条与块尺寸无关的判据：
    // 顶上一截源图全是白，底下的方块行就该全是白；底下一截全黑，方块行就该全黑。
    // 两处一起也顺带钉住「取的是框下那块源像素」——取错地方这两条都会挂。
    for (int y = 62; y <= 70; ++y)
    {
        for (int x = 152; x <= 248; ++x)
        {
            EXPECT_EQ(crossed.pixelColor(x, y), QColor(255, 255, 255)) << "框顶那几行的源图全是白，方块行不该出别的颜色：" << x << ',' << y;
        }
    }
    for (int y = 132; y <= 138; ++y)
    {
        for (int x = 152; x <= 248; ++x)
        {
            EXPECT_EQ(crossed.pixelColor(x, y), QColor(0, 0, 0)) << "框底那几行的源图全是黑，方块行不该出别的颜色：" << x << ',' << y;
        }
    }
    // 纯白区打马赛克：整块都该保持白（同时证明取色位置没跑偏）
    for (int y = 14; y <= 86; y += 7)
    {
        for (int x = 14; x <= 106; x += 7)
        {
            EXPECT_EQ(crossed.pixelColor(x, y), QColor(255, 255, 255)) << "第一个框全在纯白源图上，输出出了别的颜色：" << x << ',' << y;
        }
    }
    EXPECT_EQ(crossed.pixelColor(10, 190), QColor(0, 0, 0)) << "框外的像素被改到了";
}

/**
 * @brief 钉住马赛克粒度记在每条图形自己身上：细档跨分界线那一块采成黑，粗档同一高度上还是白
 * @details 画布上半白下半黑，分界线在 y=100。细档（8）从 y=65 起框：跨线那一块覆盖 [97,105)，
 *          块内无论取哪一行做采样都落在黑半区；粗档（20）从 y=65 起的块是 [85,105)，
 *          采样行还落在白半区。两个取样点各按自己的方块网格手算，粒度被写死成别的档就过不了。
 */
TEST_F(AnnotationFixture, MosaicGranularityFollowsEachShape)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Mosaic).has_value());

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0, .mosaicBlockSize = 8}).has_value());
    m_engine.beginStroke(QPoint(20, 65));
    m_engine.moveStroke(QPoint(120, 145));
    ASSERT_TRUE(m_engine.endStroke().has_value());
    EXPECT_LT(m_engine.exportImage().pixelColor(60, 99).red(), 128) << "细档跨线那一块没采到黑半区，粒度没生效";

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0, .mosaicBlockSize = 20}).has_value());
    m_engine.beginStroke(QPoint(140, 65));
    m_engine.moveStroke(QPoint(240, 145));
    ASSERT_TRUE(m_engine.endStroke().has_value());
    EXPECT_GT(m_engine.exportImage().pixelColor(180, 99).red(), 128) << "粗档同一高度那一块被采成了黑，粒度没跟上";
}

/**
 * @brief 钉住高亮的笔迹比普通画笔宽，容差同步放宽
 */
TEST_F(AnnotationFixture, HighlightIsWiderThanThePen)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Highlight).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 255, 0), .lineWidth = 3, .fontSize = 16, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(20, 20));
    m_engine.moveStroke(QPoint(20, 120));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    EXPECT_TRUE(m_engine.selectionAt(QPoint(20, 70)).has_value());
    EXPECT_TRUE(m_engine.selectionAt(QPoint(28, 70)).has_value()) << "高亮的宽带子外侧点不中，容差没跟着宽度放宽";
}

/**
 * @brief 钉住高亮透明度记在每条图形自己身上：黑底上「颜色 × 透明度」就是直接的读数
 * @details 底图下半是黑的，黄 (255,255,0) 按 alpha 压上去，合成结果的红绿通道就是 alpha 值：
 *          60 一档留 (60,60,0)，170 一档留 (170,170,0)。两笔同框、档各不同，互不串档。
 */
TEST_F(AnnotationFixture, HighlightOpacityFollowsEachShape)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Highlight).has_value());

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 255, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0, .highlightAlpha = 60}).has_value());
    m_engine.beginStroke(QPoint(20, 130));
    m_engine.moveStroke(QPoint(180, 130));
    ASSERT_TRUE(m_engine.endStroke().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(100, 130), QColor(60, 60, 0)) << "轻档在黑底上的读数不是 60";

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 255, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0, .highlightAlpha = 170}).has_value());
    m_engine.beginStroke(QPoint(20, 160));
    m_engine.moveStroke(QPoint(180, 160));
    ASSERT_TRUE(m_engine.endStroke().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(100, 160), QColor(170, 170, 0)) << "重档在黑底上的读数不是 170";
}

/**
 * @brief 钉住粗体：同一串字按粗体画出来的墨点明显多过常规档
 * @details 墨点数这条判据在字体与平台之间都成立：粗体的笔画更厚，非底色像素必然更多。
 *          两串 MM 同字号画在上下两块互不重叠的白区里（底图 y=100 以下是黑的，取样块都留了余量），
 *          只比计数不比位置。
 */
TEST_F(AnnotationFixture, TextBoldDoublesTheInk)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Text).has_value());

    const auto inkCount = [this](const QRect &box)
    {
        const QImage drawn = m_engine.exportImage();
        int          ink   = 0;
        for (int y = box.top(); y <= box.bottom(); ++y)
        {
            for (int x = box.left(); x <= box.right(); ++x)
            {
                if (drawn.pixelColor(x, y) != QColor(255, 255, 255))
                {
                    ++ink;
                }
            }
        }
        return ink;
    };

    // 常规档
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 20, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(20, 35));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("MMMM")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());

    // 粗体档
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 20, .fillStyle = 0, .textBold = true}).has_value());
    m_engine.beginStroke(QPoint(20, 85));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("MMMM")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());

    const auto regularInk = inkCount(QRect(10, 10, 380, 45));
    const auto boldInk    = inkCount(QRect(10, 60, 380, 39));
    EXPECT_GT(regularInk, 0) << "常规档一个字都没画出来，后面的对照就没有意义";
    EXPECT_GT(boldInk, regularInk) << "粗体的墨点没有多过常规档，粗体没生效";
}

/**
 * @brief 钉住斜体：同一根竖笔画斜体档的顶端比底端更靠右，常规档上下基本对齐
 * @details 只比「最上一行墨点」与「最下一行墨点」的左端差值：字体是否真有斜体变体、
 *          平台合成斜体的倾角差几像素，都不影响这条相对比较。取大写字 H——左竖笔画的
 *          上下端各自代表整字的倾倒方向。
 */
TEST_F(AnnotationFixture, TextItalicLeansTheStem)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Text).has_value());

    // 最上与最下一行墨点的左端之差：斜体为正（上端更靠右），常规应近似 0
    const auto leanOf = [this](const QRect &box)
    {
        const QImage drawn   = m_engine.exportImage();
        int          topX    = -1;
        int          bottomX = -1;
        for (int y = box.top(); y <= box.bottom(); ++y)
        {
            for (int x = box.left(); x <= box.right(); ++x)
            {
                if (drawn.pixelColor(x, y) == QColor(255, 255, 255))
                {
                    continue;
                }
                if (topX < 0)
                {
                    topX = x;
                }
                bottomX = x;
                break;
            }
        }
        return topX < 0 || bottomX < 0 ? 0 : topX - bottomX;
    };

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(30, 40));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("H")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0, .textItalic = true}).has_value());
    m_engine.beginStroke(QPoint(30, 85));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("H")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());

    const auto regularLean = leanOf(QRect(20, 10, 120, 48));
    const auto italicLean  = leanOf(QRect(20, 55, 120, 44));
    EXPECT_LE(std::abs(regularLean), 2) << "常规档的竖笔画就歪了，先查取样框是不是切到了别的墨";
    EXPECT_GT(italicLean, regularLean + 2) << "斜体档的笔画没有向右倾倒，斜体没生效";
}

/**
 * @brief 钉住字体族：换一个家族，同一串字的渲染就该变
 * @details 家族名从本机字体库里现取两个，再把两族渲染逐像素比对——写死家族名会在别的
 *          机器上落到同一个替代字体上，判据假绿。两族量出来一样宽的少见情形按跳过处理，
 *          那是这台机器没有可判的差，不是被测代码的问题。
 */
TEST_F(AnnotationFixture, TextFamilyChangesTheGlyphs)
{
    const auto families = QFontDatabase::families();
    ASSERT_GE(families.size(), 2) << "这台机器只有一个字体家族，换族无从验起";
    const QString first = families.first();
    QString       second;
    for (const auto &family: families)
    {
        if (family != first)
        {
            second = family;
            break;
        }
    }

    QFont probe;
    probe.setPointSize(24);
    probe.setFamily(first);
    const auto firstAdvance = QFontMetricsF(probe).horizontalAdvance(QStringLiteral("HWM"));
    probe.setFamily(second);
    if (qFuzzyCompare(firstAdvance, QFontMetricsF(probe).horizontalAdvance(QStringLiteral("HWM"))))
    {
        GTEST_SKIP() << "本机两族量『HWM』一样宽，没有可判的差";
    }

    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Text).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 20, .fillStyle = 0, .fontFamily = first}).has_value());
    m_engine.beginStroke(QPoint(20, 35));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("HWM")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 20, .fillStyle = 0, .fontFamily = second}).has_value());
    m_engine.beginStroke(QPoint(20, 85));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("HWM")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());

    // 两串字同字号同列起画、只差一个家族：逐像素比「上下相隔 50 行」的两块，有差即证明家族生效
    const QImage drawn = m_engine.exportImage();
    int          diff  = 0;
    for (int y = 10; y <= 44; ++y)
    {
        for (int x = 20; x <= 180; ++x)
        {
            if (drawn.pixelColor(x, y) != drawn.pixelColor(x, y + 50))
            {
                ++diff;
            }
        }
    }
    EXPECT_GT(diff, 0) << "两个家族画出来的字逐像素一致，字体族没生效";
}

/**
 * @brief 钉住描边：深字在杂底上围出一圈反色轮廓
 * @details 底图下半是黑的，红字（亮度过半判为深色）的描边取白——黑底上「白像素」这一格
 *          只有描边画得出来：不打描边时，红字压黑底任何混合都到不了三通道全高。
 */
TEST_F(AnnotationFixture, TextOutlineHalosTheGlyphs)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Text).has_value());

    const auto brightCount = [this](const QRect &box)
    {
        const QImage drawn  = m_engine.exportImage();
        int          bright = 0;
        for (int y = box.top(); y <= box.bottom(); ++y)
        {
            for (int x = box.left(); x <= box.right(); ++x)
            {
                const auto pixel = drawn.pixelColor(x, y);
                if (pixel.red() > 200 && pixel.green() > 200 && pixel.blue() > 200)
                {
                    ++bright;
                }
            }
        }
        return bright;
    };

    // 常规档：黑底红字，没有白
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 32, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(20, 150));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("H")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());
    EXPECT_EQ(brightCount(QRect(10, 105, 130, 70)), 0) << "不打描边时黑底上出了白像素，后面的对照就没基准";

    // 描边档：字形外围一圈白
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 32, .fillStyle = 0, .textOutline = true}).has_value());
    m_engine.beginStroke(QPoint(20, 150));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("H")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());
    EXPECT_GT(brightCount(QRect(10, 105, 130, 70)), 0) << "描边没有围出反色轮廓";
}

/**
 * @brief 钉住底板：字形后面垫一块反色的半透明圆角板
 * @details 红字（深色）的底板取白、透明度 200/255：黑底上白板叠出来是 (200,200,200)，
 *          取样点放在字形左侧的留白带里（板比字形框每边多出 8 像素），不在字上也不在板外。
 */
TEST_F(AnnotationFixture, TextBackgroundPlatesTheGlyphs)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Text).has_value());

    // 常规档：留白带是黑底本色
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 32, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(30, 150));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("H")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(26, 150), QColor(0, 0, 0)) << "没开底板时留白带该是黑底";

    // 底板档：同一个点在板内，是白 200 叠黑的结果
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 32, .fillStyle = 0, .textBackground = true}).has_value());
    m_engine.beginStroke(QPoint(30, 150));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("H")).has_value());
    ASSERT_TRUE(m_engine.endStroke().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(26, 150), QColor(200, 200, 200)) << "底板没垫到字形左侧的留白带里";
}

/**
 * @brief 钉住文字外接框的锚点与画字同解：position 是基线，框要盖住字、不许整框压在字底下
 * @details 渲染探针核过：position.y=60、字号 24 时墨迹在 [35,59]——postion 是基线。
 *          框顶须从基线往上退一个 ascent（约 0.9 字号），基线须落在框内。按左上角估的老写法
 *          框顶就在基线上，整框悬在字下面约一行，这条就是那处缺陷的回归判据。
 */
TEST_F(AnnotationFixture, TextSelectionBoxRidesTheBaseline)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Text).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(40, 60));
    ASSERT_TRUE(m_engine.setPendingText(QStringLiteral("H")).has_value());
    const auto committed = m_engine.endStroke();
    ASSERT_TRUE(committed.has_value()) << committed.error().message;

    // 墨迹行范围：判定框要把字整个盖住
    const QImage drawn       = m_engine.exportImage();
    int          firstInkRow = -1;
    int          lastInkRow  = -1;
    for (int y = 0; y < 100; ++y)
    {
        for (int x = 30; x < 160; ++x)
        {
            if (drawn.pixelColor(x, y) != QColor(255, 255, 255))
            {
                if (firstInkRow < 0)
                {
                    firstInkRow = y;
                }
                lastInkRow = y;
                break;
            }
        }
    }
    ASSERT_GE(firstInkRow, 0) << "字没画出来，判定框无从比";

    ASSERT_TRUE(m_engine.select(*committed).has_value());
    const auto bounds = m_engine.selectionBounds();
    ASSERT_TRUE(bounds.has_value()) << "选中了却拿不到外接框";
    EXPECT_LT(bounds->top(), firstInkRow) << "框顶在墨迹之下，整框压在字底下";
    EXPECT_LE(bounds->top(), 60 - static_cast<int>(24 * 0.6)) << "框顶距基线的退让不足大半个字号，锚点还是按左上角算的";
    EXPECT_GE(bounds->bottom(), lastInkRow) << "框没盖住字的底边";
    EXPECT_EQ(bounds->left(), 40) << "框左沿没落在 position.x 上";
}

/**
 * @brief 钉住序号取号的位置：按当时的条数 +1，撤销一条后接着那个号走
 */
TEST(NumberToolTest, LabelFollowsTheCurrentShapeCount)
{
    NumberTool tool;

    tool.begin(QPoint(0, 0));
    const auto first = tool.end();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->number, 1);
    EXPECT_EQ(first->text, QStringLiteral("1"));
    tool.insertShape(0, *first);

    tool.begin(QPoint(5, 5));
    const auto second = tool.end();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->number, 2);
    tool.insertShape(1, *second);

    // 撤销一条再点：号该回到 2，而不是跳到 3 留下两个 3
    static_cast<void>(tool.takeShapeAt(1));
    tool.begin(QPoint(9, 9));
    const auto third = tool.end();
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(third->number, 2) << "取号用的是「一共点过几次」而不是「当前有几条」，撤销后会串号";
    EXPECT_EQ(third->text, QStringLiteral("2"));
    EXPECT_EQ(tool.kind(), GyanisScribe::Annotation::ToolKind::Number);
}

/**
 * @brief 钉住点名下一个号：从点名那个号连排，回到自动口径后按现有条数接上
 * @details 「归零」在这一版里就是回到自动口径——号本来就长在现有条数上，清掉点名值即可。
 */
TEST(NumberToolTest, NamedNextNumberRunsFromThereUntilReset)
{
    NumberTool tool;
    tool.setNextNumber(5);

    tool.begin(QPoint(0, 0));
    const auto first = tool.end();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->number, 5) << "点名的号没落上去";
    tool.insertShape(0, *first);

    tool.begin(QPoint(5, 5));
    const auto second = tool.end();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->number, 6) << "点名之后该接着往下走，而不是每张都落同一个号";
    tool.insertShape(1, *second);

    tool.clearNextNumber();
    EXPECT_EQ(tool.nextNumber(), 3) << "回到自动口径该按现有条数 +1";
    tool.begin(QPoint(9, 9));
    const auto third = tool.end();
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(third->number, 3) << "回到自动口径后没按现有条数接上";

    // 小于 1 的号夹到 1：号牌上写 0 或负数是没有意义的
    tool.setNextNumber(0);
    EXPECT_EQ(tool.nextNumber(), 1);
}

/**
 * @brief 钉住画笔的采样：重合点不入库，走过的每一步都入库
 */
TEST(PenToolTest, DropsDuplicateSamplesAndKeepsThePath)
{
    PenTool tool;
    tool.setActive(true);

    tool.begin(QPoint(10, 10));
    tool.update(QPoint(10, 10)); // 与起笔重合
    tool.update(QPoint(12, 11)); // 间距不足 2 像素
    tool.update(QPoint(40, 10));
    tool.update(QPoint(40, 35));

    const auto committed = tool.end();
    ASSERT_TRUE(committed.has_value());
    // 起笔 (10,10) + 三个跨过最小间距的采样点；与起笔重合的那一次 update 不该入库
    EXPECT_EQ(committed->points.size(), 4) << "采样条数不对，实际 " << committed->points.size();
    EXPECT_EQ(committed->points.front(), QPoint(10, 10));
    EXPECT_EQ(committed->points.back(), QPoint(40, 35));
    for (int index = 0; index + 1 < committed->points.size(); ++index)
    {
        EXPECT_NE(committed->points[index], committed->points[index + 1]) << "第 " << index << " 与下一个采样点重合，去重没生效";
    }
}

/**
 * @brief 钉住改样式只写三个样式字段，且越界两个方向都拒
 * @details 撤销「改样式」靠的就是这一条：位置、轨迹、文本必须原样留着，
 *          否则撤一次颜色会把图形挪走。越界不许回绕到末尾——那是把「没这条图形」当成「改最后一条」。
 */
TEST(PenToolRestyleTest, RestyleWritesOnlyTheStyleFields)
{
    PenTool tool;
    tool.setActive(true);

    tool.begin(QPoint(10, 10));
    tool.update(QPoint(40, 35));
    const auto committed = tool.end();
    ASSERT_TRUE(committed.has_value());
    tool.insertShape(0, *committed);

    ASSERT_TRUE(tool.restyleShapeAt(0, PenParameters{.color = QColor(0, 0, 255), .lineWidth = 8, .fontSize = 20, .fillStyle = 0}));
    const auto restyled = tool.shapeAt(0);
    EXPECT_EQ(restyled.color, QColor(0, 0, 255));
    EXPECT_EQ(restyled.lineWidth, 8);
    EXPECT_EQ(restyled.fontSize, 20);
    EXPECT_EQ(restyled.points, committed->points) << "改样式把采样点动了";
    EXPECT_EQ(restyled.rect, committed->rect) << "改样式把位置动了";

    EXPECT_FALSE(tool.restyleShapeAt(1, PenParameters{.color = QColor(255, 0, 0), .lineWidth = 3, .fontSize = 16, .fillStyle = 0})) << "只有一条图形时索引 1 该被拒";
    EXPECT_FALSE(tool.restyleShapeAt(-1, PenParameters{.color = QColor(255, 0, 0), .lineWidth = 3, .fontSize = 16, .fillStyle = 0})) << "负索引该被拒";
    EXPECT_EQ(tool.shapeCount(), 1) << "越界的那次多插了一条";
    EXPECT_EQ(tool.shapeAt(0).color, QColor(0, 0, 255)) << "越界的调用改到了已有图形";
}
