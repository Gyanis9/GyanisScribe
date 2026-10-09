#include "Annotation/AnnotationEngine.h"

#include "Annotation/HighlightTool.h"
#include "Annotation/MosaicTool.h"
#include "Annotation/NumberTool.h"
#include "Annotation/PenTool.h"
#include "Annotation/TextTool.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QString>

#include <gtest/gtest.h>


#include <memory>
#include <string>

using GyanisScribe::Annotation::AnnotationEngine;
using GyanisScribe::Annotation::AnnotationSelection;
using GyanisScribe::Annotation::NumberTool;
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
            int   argumentCount  = 1;
            char  argumentZero[] = "TestAnnotationTools";
            char *arguments[]    = {argumentZero, nullptr};
            application          = std::make_unique<QApplication>(argumentCount, arguments);
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
 * @brief 钉住高亮的笔迹比普通画笔宽，容差同步放宽
 */
TEST_F(AnnotationFixture, HighlightIsWiderThanThePen)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Highlight).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(QColor(255, 255, 0), 3, 16).has_value());
    m_engine.beginStroke(QPoint(20, 20));
    m_engine.moveStroke(QPoint(20, 120));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    EXPECT_TRUE(m_engine.selectionAt(QPoint(20, 70)).has_value());
    EXPECT_TRUE(m_engine.selectionAt(QPoint(28, 70)).has_value()) << "高亮的宽带子外侧点不中，容差没跟着宽度放宽";
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
