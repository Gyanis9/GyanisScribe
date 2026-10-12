#include "Annotation/AnnotationEngine.h"
#include "Annotation/PenParameters.h"

#include <QApplication>
#include <QColor>
#include <QImage>

#include <gtest/gtest.h>


#include <memory>
#include <string>

using GyanisScribe::Annotation::AnnotationEngine;
using GyanisScribe::Annotation::AnnotationSelection;
using GyanisScribe::Annotation::PenParameters;
using GyanisScribe::Annotation::SelectionDragModifiers;
using GyanisScribe::Annotation::SelectionHandle;
using GyanisScribe::Annotation::ToolKind;
using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::ErrorCode;

namespace
{
    std::unique_ptr<QApplication> application;

    class EngineFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestAnnotationEngine";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            CapturedImage canvas;
            canvas.image = QImage(200, 100, QImage::Format_ARGB32_Premultiplied);
            canvas.image.fill(QColor(255, 255, 255));
            canvas.devicePixelRatio = 1.0;
            canvas.logicalSize      = QSize(200, 100);
            canvas.screenName       = QStringLiteral("FakeScreen-1");
            m_canvasImage           = canvas.image.copy();
            m_engine.loadCanvas(canvas);
        }

        /// 画一个矩形框，返回它的位置
        AnnotationSelection drawRectangle(const QPoint &from, const QPoint &to)
        {
            EXPECT_TRUE(m_engine.setActiveTool(ToolKind::Rectangle).has_value());
            m_engine.beginStroke(from);
            m_engine.moveStroke(to);
            const auto committed = m_engine.endStroke();
            EXPECT_TRUE(committed.has_value());
            return committed.value_or(AnnotationSelection{});
        }

        AnnotationEngine m_engine;
        QImage           m_canvasImage;
    };
} // namespace

/**
 * @brief 钉住没画布时的表现：导出不该产出一张随机图
 */
TEST(AnnotationEngineWithoutCanvas, ExportIsEmptyAndToolsStillWork)
{
    AnnotationEngine engine;
    EXPECT_TRUE(engine.exportImage().isNull()) << "没有画布却导出成功，界面会保存出一张空文件";
    EXPECT_TRUE(engine.canvas().image.isNull());
    EXPECT_EQ(engine.canvas().devicePixelRatio, 1.0);
}

/**
 * @brief 钉住工具种类与画笔参数的拒绝面
 */
TEST_F(EngineFixture, RejectsUnknownToolAndNonPositivePenParameters)
{
    const auto unknown = m_engine.setActiveTool(static_cast<ToolKind>(99));
    ASSERT_FALSE(unknown.has_value()) << "不认识的种类被当成切成功，当前工具会停在上一个";
    EXPECT_EQ(unknown.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(m_engine.activeTool(), ToolKind::Rectangle) << "失败的选择也不该改到当前工具";

    const auto badWidth = m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 0, .fontSize = 16, .fillStyle = 0});
    ASSERT_FALSE(badWidth.has_value()) << "线宽 0 会让图形变成看不见的线";
    EXPECT_NE(badWidth.error().message.find("正数"), std::string::npos);

    const auto badFont = m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 3, .fontSize = -2, .fillStyle = 0});
    ASSERT_FALSE(badFont.has_value()) << "负字号会被 Qt 取绝对值，用户设的与实际的不是一回事";
    EXPECT_EQ(badFont.error().code, ErrorCode::InvalidArgument);
}

/**
 * @brief 钉住「插入 → 撤销 → 重做」这条链在像素上真的可逆
 */
TEST_F(EngineFixture, UndoAndRedoOfAnInsertArePixelReversible)
{
    const auto selection = drawRectangle(QPoint(10, 10), QPoint(90, 60));
    EXPECT_TRUE(m_engine.canUndo());

    const auto drawn = m_engine.exportImage();
    EXPECT_NE(drawn.pixelColor(10, 35), QColor(255, 255, 255)) << "矩形左边该在 x=10 这一列上";

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_FALSE(m_engine.canUndo()) << "撤销之后没有别的动作可撤了";
    EXPECT_TRUE(m_engine.canRedo());
    EXPECT_EQ(m_engine.exportImage().pixelColor(10, 35), QColor(255, 255, 255)) << "撤销没把这条图形抹掉";

    ASSERT_TRUE(m_engine.redo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(10, 35), drawn.pixelColor(10, 35)) << "重做没把图形画回原位";
    EXPECT_TRUE(m_engine.selectionAt(QPoint(10, 35)).has_value()) << "重做后该还能选中它";
    EXPECT_EQ(selection.shapeIndex, 0);
}

/**
 * @brief 钉住删除可撤销，且删完立刻清掉选中
 */
TEST_F(EngineFixture, RemoveSelectionIsUndoableAndClearsSelection)
{
    drawRectangle(QPoint(20, 20), QPoint(120, 80));
    const auto hit = m_engine.selectionAt(QPoint(20, 50));
    ASSERT_TRUE(hit.has_value());
    ASSERT_TRUE(m_engine.select(*hit).has_value());

    ASSERT_TRUE(m_engine.removeSelection().has_value());
    EXPECT_TRUE(m_engine.selections().empty()) << "删掉的图形还留在选中里，下一次操作会打到邻居";
    EXPECT_FALSE(m_engine.selectionAt(QPoint(20, 50)).has_value());

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_TRUE(m_engine.selectionAt(QPoint(20, 50)).has_value()) << "撤销删除没有把图形放回来";
}

/**
 * @brief 钉住移动可撤销，反向位移量与移动量一致
 */
TEST_F(EngineFixture, MoveSelectionByIsUndoable)
{
    drawRectangle(QPoint(30, 30), QPoint(80, 70));
    const auto hit = m_engine.selectionAt(QPoint(30, 50));
    ASSERT_TRUE(hit.has_value());
    ASSERT_TRUE(m_engine.select(*hit).has_value());

    // 位移取 70：原框在 x 30..80，新框在 100..150，两段不搭界，探针才分得清「动没动」
    ASSERT_TRUE(m_engine.moveSelectionBy(QPoint(70, 0)).has_value());
    EXPECT_FALSE(m_engine.selectionAt(QPoint(30, 50)).has_value()) << "移动后原位还能点中，说明没动";
    EXPECT_TRUE(m_engine.selectionAt(QPoint(100, 50)).has_value()) << "移动到的位置点不中";

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_TRUE(m_engine.selectionAt(QPoint(30, 50)).has_value()) << "撤销移动没回到原位";
    EXPECT_FALSE(m_engine.selectionAt(QPoint(100, 50)).has_value()) << "撤销只回了一半，新位置还留着图形";
}

/**
 * @brief 钉住「拖一趟 = 一步撤销」：连续平移同一条图形要并成一步
 * @details 拖动一路会发几十次位移、方向键也能连按好几格，每一跳各占一步的话撤销一次只退一格。
 *          判据取像素与栈深两处：一次撤销就回到这一趟开始之前，再撤一步撤掉的是绘制本身。
 */
TEST_F(EngineFixture, ConsecutiveMovesOfTheSameShapeCollapseIntoOneUndoStep)
{
    drawRectangle(QPoint(10, 10), QPoint(90, 60));
    const auto hit = m_engine.selectionAt(QPoint(10, 35));
    ASSERT_TRUE(hit.has_value());
    ASSERT_TRUE(m_engine.select(*hit).has_value());

    // 三跳各 +10，合起来是 +30：左边描边该从 x=10 走到 x=40
    for (int step = 0; step < 3; ++step)
    {
        ASSERT_TRUE(m_engine.moveSelectionBy(QPoint(10, 0)).has_value());
    }
    EXPECT_NE(m_engine.exportImage().pixelColor(40, 35), QColor(255, 255, 255)) << "三跳位移没全落到图形上";

    ASSERT_TRUE(m_engine.undo().has_value());
    const auto afterOneUndo = m_engine.exportImage();
    EXPECT_NE(afterOneUndo.pixelColor(10, 35), QColor(255, 255, 255)) << "一次撤销没回到这一趟开始之前：每一跳各占了一步";
    EXPECT_EQ(afterOneUndo.pixelColor(40, 35), QColor(255, 255, 255)) << "撤销只退了一格，剩下两跳还留在图上";

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_FALSE(m_engine.canUndo()) << "撤销栈里还剩着移动步：并步只并掉了其中几跳";
}

/**
 * @brief 钉住并步的范围：换了图形就不许把两趟位移并成一步
 * @details 并步按「同一工具 + 同一序号」判，认错了对象就会把两条图形的位移合成一次撤销，
 *          撤第二条的移动时把第一条也拖回去。
 */
TEST_F(EngineFixture, MovesOfDifferentShapesAreNotMerged)
{
    drawRectangle(QPoint(10, 10), QPoint(60, 60));
    drawRectangle(QPoint(100, 10), QPoint(150, 60));

    const auto first = m_engine.selectionAt(QPoint(10, 35));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(m_engine.select(*first).has_value());
    ASSERT_TRUE(m_engine.moveSelectionBy(QPoint(20, 0)).has_value());

    // 两趟位移之间没有别的命令插队，栈顶正是上一条移动：并步若不认图形就会把它们合成一步
    const auto second = m_engine.selectionAt(QPoint(100, 35));
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(m_engine.select(*second).has_value());
    ASSERT_TRUE(m_engine.moveSelectionBy(QPoint(20, 0)).has_value());

    // 撤的应当只是第二趟：第二条回到 x=100，第一条仍留在 x=30
    ASSERT_TRUE(m_engine.undo().has_value());
    const auto afterOneUndo = m_engine.exportImage();
    EXPECT_NE(afterOneUndo.pixelColor(100, 35), QColor(255, 255, 255)) << "第二次撤销没把第二条图形放回原位";
    EXPECT_EQ(afterOneUndo.pixelColor(120, 35), QColor(255, 255, 255)) << "第二条还停在位移后的位置，撤销没打到它";
    EXPECT_NE(afterOneUndo.pixelColor(30, 35), QColor(255, 255, 255)) << "第一条被一起拖回去了：两趟位移被并成了一步";
}

/**
 * @brief 钉住换画布清空图形与历史
 */
TEST_F(EngineFixture, LoadCanvasClearsShapesAndHistory)
{
    drawRectangle(QPoint(5, 5), QPoint(60, 40));
    ASSERT_TRUE(m_engine.canUndo());

    CapturedImage fresh;
    fresh.image = QImage(200, 100, QImage::Format_ARGB32_Premultiplied);
    fresh.image.fill(QColor(0, 255, 0));
    fresh.logicalSize      = QSize(200, 100);
    fresh.devicePixelRatio = 1.0;
    m_engine.loadCanvas(fresh);

    EXPECT_FALSE(m_engine.canUndo()) << "旧画布上的动作还能撤到新画布上，撤销会凭空冒出图形";
    EXPECT_FALSE(m_engine.canRedo());
    EXPECT_FALSE(m_engine.selectionAt(QPoint(5, 30)).has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(100, 50), QColor(0, 255, 0));
}

/**
 * @brief 钉住跨工具的命中顺序按层叠（后画的压在上面）
 * @details 层叠号在落定那一刻由引擎发：这条用例先画矩形再落序号牌，牌该压住矩形被点中。
 */
TEST_F(EngineFixture, SelectionPrefersTopmostToolAcrossTools)
{
    drawRectangle(QPoint(40, 40), QPoint(160, 90));

    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Number).has_value());
    m_engine.beginStroke(QPoint(60, 60));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    const auto hit = m_engine.selectionAt(QPoint(60, 60));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->tool, ToolKind::Number) << "重叠处交回了下面那层矩形，用户点的明明是牌";
}

/**
 * @brief 钉住撤销会让失效的选中一并失效
 */
TEST_F(EngineFixture, UndoInvalidatesStaleSelection)
{
    const auto first  = drawRectangle(QPoint(10, 10), QPoint(50, 50));
    const auto second = drawRectangle(QPoint(60, 10), QPoint(120, 60));
    ASSERT_TRUE(m_engine.select(second).has_value());

    ASSERT_TRUE(m_engine.undo().has_value()); // 撤掉第二条
    EXPECT_TRUE(m_engine.selections().empty()) << "被撤掉的图形还留在选中里，下一次移动会打到别处";

    const auto moveFailed = m_engine.moveSelectionBy(QPoint(5, 5));
    ASSERT_FALSE(moveFailed.has_value()) << "没有选中却移动成功";
    EXPECT_NE(moveFailed.error().message.find("没有选中"), std::string::npos);

    EXPECT_TRUE(m_engine.selectionAt(QPoint(10, 30)).has_value()) << "第一条该还在";
    EXPECT_EQ(first.shapeIndex, 0);
}

/**
 * @brief 钉住撤销/重做没得走时给出原因而不是崩
 */
TEST_F(EngineFixture, UndoAndRedoWithoutHistoryFailWithReason)
{
    ASSERT_FALSE(m_engine.canUndo());
    const auto nothingToUndo = m_engine.undo();
    ASSERT_FALSE(nothingToUndo.has_value()) << "空栈撤销返回成功，界面按钮的置灰判据就失效了";
    EXPECT_NE(nothingToUndo.error().message.find("没有可撤销"), std::string::npos);

    const auto nothingToRedo = m_engine.redo();
    ASSERT_FALSE(nothingToRedo.has_value());
    EXPECT_NE(nothingToRedo.error().message.find("没有可重做"), std::string::npos);
}

/**
 * @brief 钉住「选中时改样式」落到那条图形上并且可撤销
 * @details 样式记在每条图形自己身上，工具上的值只是后续图形的默认值，所以这一步必须改图形本身。
 *          像素按手算核：矩形左边在 x=10，描边以该线为中心向两侧各扩半个线宽——
 *          线宽 3 覆盖 [8.5,11.5]，线宽 8 覆盖 [6,14]；于是 x=10 两档都是实心，
 *          x=13 只在宽的那一档有墨。两条断言一实一虚，撤销是否真回退得出来。
 */
TEST_F(EngineFixture, StyleChangeRestylesTheSelectedShapeAndUndoesBack)
{
    const auto rectangle = drawRectangle(QPoint(10, 10), QPoint(90, 60));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());

    const QImage drawn = m_engine.exportImage();
    ASSERT_EQ(drawn.pixelColor(10, 35), QColor(255, 0, 0)) << "默认的红没落在左边界上，后面的对照就没基准";
    ASSERT_EQ(drawn.pixelColor(13, 35), QColor(255, 255, 255)) << "线宽 3 的描边不该够到 x=13";

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 8, .fontSize = 16, .fillStyle = 0}).has_value());
    const QImage restyled = m_engine.exportImage();
    EXPECT_EQ(restyled.pixelColor(10, 35), QColor(0, 0, 255)) << "改样式只改了默认值，没落到选中的那条图形上";
    EXPECT_EQ(restyled.pixelColor(13, 35), QColor(0, 0, 255)) << "线宽没跟着改";

    ASSERT_TRUE(m_engine.undo().has_value());
    const QImage undone = m_engine.exportImage();
    EXPECT_EQ(undone.pixelColor(10, 35), QColor(255, 0, 0)) << "撤销没把颜色退回改样式之前";
    EXPECT_EQ(undone.pixelColor(13, 35), QColor(255, 255, 255)) << "撤销没把线宽退回改样式之前";
    EXPECT_TRUE(m_engine.canRedo()) << "改样式没进栈，重做无从谈起";
}

/**
 * @brief 钉住同一条图形上连续的改样式并成一步撤销
 * @details 线宽滑块拖一趟会连发好几次值：撤一次要回到抬手之前，而不是让用户按三下 Ctrl+Z。
 *          栈里该只有「插入 + 改样式」两步，所以第二次撤销才是把图形本身抹掉。
 */
TEST_F(EngineFixture, ConsecutiveStyleChangesOnOneShapeUndoAsASingleStep)
{
    const auto rectangle = drawRectangle(QPoint(10, 10), QPoint(90, 60));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 4, .fontSize = 16, .fillStyle = 0}).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 255, 0), .lineWidth = 6, .fontSize = 16, .fillStyle = 0}).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 8, .fontSize = 16, .fillStyle = 0}).has_value());
    ASSERT_EQ(m_engine.exportImage().pixelColor(13, 35), QColor(0, 0, 255)) << "最后一次改动没落到图形上";

    ASSERT_TRUE(m_engine.undo().has_value());
    // 没并成一步的话，这一撤只会退到「绿、宽 6」那一档：x=10 是绿的、x=13 仍是白的
    EXPECT_EQ(m_engine.exportImage().pixelColor(10, 35), QColor(255, 0, 0)) << "撤一步没回到这一轮手势开始前的样式，三次改动没并成一步";

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(10, 35), QColor(255, 255, 255)) << "改样式多占了步骤，第二次撤销还没碰到那条图形";
    EXPECT_FALSE(m_engine.canUndo()) << "栈里多出了不该有的条目";
}

/**
 * @brief 钉住两条拒绝面：没有选中项、或样式没变时都不该造出一步撤销
 * @details 改默认值本身不是画布上的动作；点一下已经在用的那个颜色也不是。
 *          放进去之后用户按 Ctrl+Z 会「什么都没发生」，历史还被顶掉一格。
 */
TEST_F(EngineFixture, StyleChangeWithoutAVisibleDifferenceAddsNoUndoStep)
{
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 8, .fontSize = 16, .fillStyle = 0}).has_value());
    EXPECT_FALSE(m_engine.canUndo()) << "画布上什么都没有，改默认值不该进撤销栈";

    const auto rectangle = drawRectangle(QPoint(10, 10), QPoint(90, 60));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());
    // 上面那次把默认值改成了蓝/8，新画的这条就是蓝/8：再下发同一组值等于什么都没改
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 8, .fontSize = 16, .fillStyle = 0}).has_value());

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(10, 35), QColor(255, 255, 255)) << "点同一档颜色也造出了一步撤销，撤的就不是那条图形了";
    EXPECT_FALSE(m_engine.canUndo());
}

/**
 * @brief 钉住改样式只碰选中的那一条
 * @details 工具上的默认值会下发给全部工具，图形样式不会：漏了这一条的话，
 *          改个颜色会把整张画布上同类图形一起刷掉。
 */
TEST_F(EngineFixture, StyleChangeTouchesOnlyTheSelectedShape)
{
    const auto first  = drawRectangle(QPoint(10, 10), QPoint(40, 60));
    const auto second = drawRectangle(QPoint(60, 10), QPoint(90, 60));
    ASSERT_TRUE(m_engine.select(second).has_value());

    const QImage drawn = m_engine.exportImage();
    ASSERT_EQ(drawn.pixelColor(10, 35), QColor(255, 0, 0));
    ASSERT_EQ(drawn.pixelColor(60, 35), QColor(255, 0, 0));

    ASSERT_TRUE(m_engine.select(first).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 3, .fontSize = 16, .fillStyle = 0}).has_value());

    const QImage restyled = m_engine.exportImage();
    EXPECT_EQ(restyled.pixelColor(10, 35), QColor(0, 0, 255)) << "选中的那条没改";
    EXPECT_EQ(restyled.pixelColor(60, 35), QColor(255, 0, 0)) << "没选中的那条被一起改了";
}

/**
 * @brief 钉住「双向箭头」也走改样式命令：选中一条已画好的单头箭头，开双向要立刻落到它身上，撤销回单头
 * @details 样式开关记在图形自己身上：新开关加进来如果忘了进 RestyleShapeCommand，选中的那条不会变，
 *          或变了撤不回去。取样点与箭头几何用例手算的同一处：线宽 4 时头长 14，
 *          起点端三角在 (30, 63) 一带最厚，离杆身与三条边都在 1.5 格以上。
 */
TEST_F(EngineFixture, DoubleHeadedRestyleLandsOnTheSelectedArrowAndUndoesBack)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Arrow).has_value());
    m_engine.beginStroke(QPoint(20, 60));
    m_engine.moveStroke(QPoint(180, 60));
    const auto arrow = m_engine.endStroke();
    ASSERT_TRUE(arrow.has_value()) << arrow.error().message;
    ASSERT_TRUE(m_engine.select(*arrow).has_value());

    ASSERT_EQ(m_engine.exportImage().pixelColor(30, 63), QColor(255, 255, 255)) << "单头箭头在起点端该是白的，后面的对照就没基准";

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0, .doubleHeaded = true}).has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(30, 63), QColor(255, 0, 0)) << "开了双向，选中的那条起点端没长出第二个头";
    EXPECT_EQ(m_engine.exportImage().pixelColor(170, 63), QColor(255, 0, 0)) << "终点端的头也没了";

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(30, 63), QColor(255, 255, 255)) << "撤销没把双向退回单头";
}

/**
 * @brief 钉住马赛克粒度与高亮透明度都走改样式命令：选中项当场改档，撤销回原档
 * @details 这两个是后加的样式旋钮里最容易被漏掉的两处：penParametersOf 与 restyleShapeAt
 *          都按字段逐个抄，漏一个就等于「选中老档的图形去调档不生效」。
 *          画布换成上半白下半黑、分界在 y=50：马赛克细档（8）从 y=35 起框，
 *          取样点 (60,53) 落在跨线块 [51,59) 里、采到黑；粗档（20）的块是 [35,55)、采到白。
 *          高亮在黑底上红绿通道的读数就是 alpha 值。
 */
TEST_F(EngineFixture, MosaicGrainAndHighlightAlphaRestyleAndUndoBack)
{
    CapturedImage canvas;
    canvas.image = QImage(200, 100, QImage::Format_ARGB32_Premultiplied);
    canvas.image.fill(QColor(255, 255, 255));
    for (int y = 50; y < 100; ++y)
    {
        for (int x = 0; x < 200; ++x)
        {
            canvas.image.setPixelColor(x, y, QColor(0, 0, 0));
        }
    }
    canvas.devicePixelRatio = 1.0;
    canvas.logicalSize      = QSize(200, 100);
    canvas.screenName       = QStringLiteral("FakeScreen-1");
    m_engine.loadCanvas(canvas);

    // 马赛克：细档跨线块采到黑
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Mosaic).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0, .mosaicBlockSize = 8}).has_value());
    m_engine.beginStroke(QPoint(20, 35));
    m_engine.moveStroke(QPoint(120, 75));
    const auto mosaic = m_engine.endStroke();
    ASSERT_TRUE(mosaic.has_value()) << mosaic.error().message;
    EXPECT_LT(m_engine.exportImage().pixelColor(60, 53).red(), 128) << "细档该采到黑，后面的对照就没基准";

    // 改成粗档：同一个取样点变白；撤一步退回细档
    ASSERT_TRUE(m_engine.select(*mosaic).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0, .mosaicBlockSize = 20}).has_value());
    EXPECT_GT(m_engine.exportImage().pixelColor(60, 53).red(), 128) << "选中项的马赛克粒度没跟着改";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_LT(m_engine.exportImage().pixelColor(60, 53).red(), 128) << "撤销没把粒度退回细档";

    // 高亮透明度：黑底上 60 档 → 170 档 → 撤销
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Highlight).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 255, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0, .mosaicBlockSize = 20, .highlightAlpha = 60})
                        .has_value());
    m_engine.beginStroke(QPoint(20, 70));
    m_engine.moveStroke(QPoint(180, 70));
    const auto highlight = m_engine.endStroke();
    ASSERT_TRUE(highlight.has_value()) << highlight.error().message;
    EXPECT_EQ(m_engine.exportImage().pixelColor(160, 70), QColor(60, 60, 0)) << "轻档的读数不是 60";

    ASSERT_TRUE(m_engine.select(*highlight).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 255, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0, .mosaicBlockSize = 20, .highlightAlpha = 170})
                        .has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(160, 70), QColor(170, 170, 0)) << "选中项的高亮透明度没跟着改";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(160, 70), QColor(60, 60, 0)) << "撤销没把透明度退回轻档";
}

/**
 * @brief 钉住高亮加宽的换档与撤销：宽度记在图形自己身上，选中的那条跟着改
 * @details 窄档（2×）带宽 16、宽档（8×）带宽 64：探点取在窄档带外、宽档带内，
 *          同一个坐标在两档下的读数互为对照。改倍数是样式动作，撤销要能整条退回去。
 */
TEST_F(EngineFixture, HighlightWidthFactorRestylesAndUndoBack)
{
    CapturedImage canvas;
    canvas.image = QImage(200, 100, QImage::Format_ARGB32_Premultiplied);
    canvas.image.fill(QColor(255, 255, 255));
    for (int y = 50; y < 100; ++y)
    {
        for (int x = 0; x < 200; ++x)
        {
            canvas.image.setPixelColor(x, y, QColor(0, 0, 0));
        }
    }
    canvas.devicePixelRatio = 1.0;
    canvas.logicalSize      = QSize(200, 100);
    canvas.screenName       = QStringLiteral("FakeScreen-1");
    m_engine.loadCanvas(canvas);

    // 窄档（2×）画一条 8 线宽的高亮：带宽 16，(160,55) 落在带外
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Highlight).has_value());
    ASSERT_TRUE(
            m_engine.setPenParameters(PenParameters{.color = QColor(255, 255, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0, .highlightAlpha = 110, .highlightWidthFactor = 2})
                    .has_value());
    m_engine.beginStroke(QPoint(20, 70));
    m_engine.moveStroke(QPoint(180, 70));
    const auto highlight = m_engine.endStroke();
    ASSERT_TRUE(highlight.has_value()) << highlight.error().message;
    EXPECT_EQ(m_engine.exportImage().pixelColor(160, 55), QColor(0, 0, 0)) << "窄档在带外留了墨，后面的对照就没有基准";

    // 改成宽档（8×）：同一个探点进带内；撤一步退回窄档
    ASSERT_TRUE(m_engine.select(*highlight).has_value());
    ASSERT_TRUE(
            m_engine.setPenParameters(PenParameters{.color = QColor(255, 255, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0, .highlightAlpha = 110, .highlightWidthFactor = 8})
                    .has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(160, 55), QColor(110, 110, 0)) << "选中项的高亮加宽没跟着改";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(160, 55), QColor(0, 0, 0)) << "撤销没把加宽退回窄档，改倍数没进撤销栈";
}

/**
 * @brief 钉住圆角的开关能改在选中项上、撤销能退回去
 */
TEST_F(EngineFixture, RoundedRectRestylesAndUndoesBack)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Rectangle).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(20, 20));
    m_engine.moveStroke(QPoint(80, 80));
    const auto box = m_engine.endStroke();
    ASSERT_TRUE(box.has_value()) << box.error().message;

    // 「有墨」取深浅而不是纯白：紧贴圆角弧的探点会吃进抗锯齿的浅灰（见工具级那条的说明）
    const auto cornerInked = [this] { return m_engine.exportImage().pixelColor(21, 21).lightness() < 100; };
    ASSERT_TRUE(cornerInked()) << "直角档的角上没有墨，后面的对照没有基准";

    ASSERT_TRUE(m_engine.select(*box).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0, .roundedRect = true}).has_value());
    EXPECT_FALSE(cornerInked()) << "选中项改成圆角之后角还在：开关没落到那条图形上";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_TRUE(cornerInked()) << "撤销没把圆角退回直角，改开关没进撤销栈";
}

/**
 * @brief 钉住椭圆档的开关能改在选中项上、撤销能退回去
 * @details 选一条高亮，看端点外那一格：平头档到端点为止，切到椭圆档圆帽该顶出来。
 */
TEST_F(EngineFixture, EllipticalRestylesTheSelectionAndUndoesBack)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Highlight).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(60, 50));
    m_engine.moveStroke(QPoint(120, 50));
    const auto band = m_engine.endStroke();
    ASSERT_TRUE(band.has_value()) << band.error().message;

    const auto capInked = [this] { return m_engine.exportImage().pixelColor(131, 50) != QColor(255, 255, 255); };
    ASSERT_FALSE(capInked()) << "平头档的端点外就有墨，后面的对照没有基准";

    ASSERT_TRUE(m_engine.select(*band).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0, .elliptical = true}).has_value());
    EXPECT_TRUE(capInked()) << "选中项切成椭圆档之后端点外该有圆帽：开关没落到那条图形上";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_FALSE(capInked()) << "撤销没把椭圆档退回平头，改开关没进撤销栈";
}

/**
 * @brief 钉住线型的换档与撤销：档记在图形自己身上，选中的那条跟着改
 * @details 白底上一条 90 长的横线：实线铺满、点线按 (1,2)×线宽 断——同一段量墨点数，
 *          改档之后读数变小、撤一步又回到满。
 */
TEST_F(EngineFixture, LineStyleRestylesAndUndoesBack)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Line).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(20, 50));
    m_engine.moveStroke(QPoint(140, 50));
    const auto line = m_engine.endStroke();
    ASSERT_TRUE(line.has_value()) << line.error().message;

    const auto inkAlong = [this]
    {
        const QImage drawn = m_engine.exportImage();
        int          ink   = 0;
        for (int x = 20; x <= 140; ++x)
        {
            for (int dy = -4; dy <= 4; ++dy)
            {
                if (drawn.pixelColor(x, 50 + dy) != QColor(255, 255, 255))
                {
                    ++ink;
                    break;
                }
            }
        }
        return ink;
    };

    const int solid = inkAlong();
    ASSERT_GT(solid, 100) << "实线没铺满，后面的对照没有基准";

    ASSERT_TRUE(m_engine.select(*line).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0, .lineStyle = 2}).has_value());
    EXPECT_LT(inkAlong(), solid) << "选中项改成点线之后还是满的：线型没落到那条图形上";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(inkAlong(), solid) << "撤销没把线型退回实线，改线型没进撤销栈";
}

/**
 * @brief 钉住箭头款式与大小两档都能改在选中项上、撤销能退回去
 * @details 白底、线宽 6、尖在 (140, 50)：头长 3.5×6=21（纵深 18.2）。
 *          款式探点取轴线上离尖 12 那点：实心有芯、空心透白；
 *          大小探点取离尖 22 的纵切行数：大档（头长 30）切到芯（约 25 行），小档（头长 15）只剩杆（约 6-8 行）。
 *          两样都是样式动作、各进一次撤销栈：退一步就是原位原样。
 */
TEST_F(EngineFixture, ArrowHeadStyleAndScaleRestyleAndUndoBack)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Arrow).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 6, .fontSize = 16, .fillStyle = 0}).has_value());
    m_engine.beginStroke(QPoint(20, 50));
    m_engine.moveStroke(QPoint(140, 50));
    const auto arrow = m_engine.endStroke();
    ASSERT_TRUE(arrow.has_value()) << arrow.error().message;

    const auto innerInked = [this] { return m_engine.exportImage().pixelColor(128, 50) != QColor(255, 255, 255); };
    ASSERT_TRUE(innerInked()) << "实心档在轴线上没有芯，后面的对照没有基准";

    // 款式：实心 → 空心 → 撤回实心
    ASSERT_TRUE(m_engine.select(*arrow).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 6, .fontSize = 16, .fillStyle = 0, .arrowHeadStyle = 1}).has_value());
    EXPECT_FALSE(innerInked()) << "选中项改成空心档之后头里还填着色：款式没落到那条图形上";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_TRUE(innerInked()) << "撤销没把款式退回实心，改款式没进撤销栈";

    // 大小：小档 → 大档 → 撤回小档
    const auto inkRowsAtDepth22 = [this]
    {
        const QImage drawn = m_engine.exportImage();
        int          rows  = 0;
        for (int y = 30; y <= 70; ++y)
        {
            if (drawn.pixelColor(118, y) != QColor(255, 255, 255))
            {
                ++rows;
            }
        }
        return rows;
    };
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 6, .fontSize = 16, .fillStyle = 0, .arrowHeadScale = 2.5}).has_value());
    const int smallRows = inkRowsAtDepth22();
    ASSERT_GT(smallRows, 0) << "小档在离尖 22 处连杆都没切到，后面的对照没有基准";
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 6, .fontSize = 16, .fillStyle = 0, .arrowHeadScale = 5.0}).has_value());
    EXPECT_GT(inkRowsAtDepth22(), smallRows * 2) << "选中项改成大档之后那一列还是只有杆：大小没落到那条图形上";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(inkRowsAtDepth22(), smallRows) << "撤销没把大小退回小档，改大小没进撤销栈";
}

namespace
{
    /// 用文字工具在指定位置放一条文字，返回它的位置
    AnnotationSelection placeText(AnnotationEngine &engine, const QPoint &at, const QString &text)
    {
        EXPECT_TRUE(engine.setActiveTool(ToolKind::Text).has_value());
        engine.beginStroke(at);
        EXPECT_TRUE(engine.setPendingText(text).has_value());
        const auto committed = engine.endStroke();
        EXPECT_TRUE(committed.has_value()) << "这条文字没落定：内容或位置被当成退化了";
        return committed.value_or(AnnotationSelection{});
    }

    /**
 * @brief 钉住改号入口穿过引擎之后读数与落号同一口径
 * @details 号落在牌子上，引擎这一层能核对的是「下一个号是多少」；牌子上的数字由 NumberTool

     * * 那一条直测钉住。
 */
    TEST_F(EngineFixture, NextNumberOverrideGoesThroughTheEngine)
    {
        EXPECT_EQ(m_engine.nextNumber(), 1) << "一条都没有时下一个号该是 1";

        ASSERT_TRUE(m_engine.setNextNumber(5).has_value());
        EXPECT_EQ(m_engine.nextNumber(), 5);

        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Number).has_value());
        m_engine.beginStroke(QPoint(30, 30));
        ASSERT_TRUE(m_engine.endStroke().has_value());
        EXPECT_EQ(m_engine.nextNumber(), 6) << "落了一号之后下一个号该顺下去";

        m_engine.resetNumbering();
        EXPECT_EQ(m_engine.nextNumber(), 2) << "回到自动口径该按现有条数 +1（已经落了一条）";
    }

    /**
     * @brief 钉住计数组穿过引擎：切到第 2 组后下一个号回到 1，第 1 组的进度不受影响
     */
    TEST_F(EngineFixture, NumberGroupsGoThroughTheEngine)
    {
        ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Number).has_value());
        m_engine.beginStroke(QPoint(30, 30));
        ASSERT_TRUE(m_engine.endStroke().has_value());
        EXPECT_EQ(m_engine.nextNumber(), 2) << "第 1 组落了一条，下一个号该是 2";

        ASSERT_TRUE(m_engine.setNumberGroup(2).has_value());
        EXPECT_EQ(m_engine.numberGroup(), 2);
        EXPECT_EQ(m_engine.nextNumber(), 1) << "第 2 组该从自己的 1 开始";
        m_engine.beginStroke(QPoint(40, 40));
        ASSERT_TRUE(m_engine.endStroke().has_value());
        EXPECT_EQ(m_engine.nextNumber(), 2);

        ASSERT_TRUE(m_engine.setNumberGroup(1).has_value());
        EXPECT_EQ(m_engine.nextNumber(), 2) << "切回第 1 组该接着自己的进度走（下一条仍是 2；按总条数算会读出 3）";
    }

} // namespace

/**
 * @brief 钉住「改已放下的文字」这一步可撤销、可重做，并且真的改到渲染
 * @details 打错一个字不该要重画一遍：改内容是一步独立操作，Ctrl+Z 回到改之前那一份，
 *          而不是把整条抹掉。像素对照证明改动落到了导出图上，不是只记在数据里。
 */
TEST_F(EngineFixture, SelectedTextCanBeReEditedAndUndone)
{
    const auto text = placeText(m_engine, QPoint(20, 20), QStringLiteral("AB"));
    ASSERT_TRUE(m_engine.select(text).has_value());
    ASSERT_TRUE(m_engine.textOfSelection().has_value());
    EXPECT_EQ(*m_engine.textOfSelection(), QStringLiteral("AB"));

    const QImage before = m_engine.exportImage();
    ASSERT_TRUE(m_engine.setTextOfSelection(QStringLiteral("CD")).has_value());
    EXPECT_EQ(*m_engine.textOfSelection(), QStringLiteral("CD")) << "改完再读回来还是旧内容，改动没落到那条图形上";
    EXPECT_NE(m_engine.exportImage(), before) << "内容改了但导出图一模一样，渲染读的不是这一格";

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(*m_engine.textOfSelection(), QStringLiteral("AB")) << "撤销把整条抹掉了，而不是退回改之前那一份文字";
    EXPECT_EQ(m_engine.exportImage(), before) << "撤销后的画面没回到改之前";

    ASSERT_TRUE(m_engine.redo().has_value());
    EXPECT_EQ(*m_engine.textOfSelection(), QStringLiteral("CD")) << "重做没把改动再放回来";
}

/**
 * @brief 钉住改文字的四种拒法，每种都给出路
 * @details 「选中的不是文字」与「那条已经不在了」如果都被当成成功，用户会以为改完了；
 *          空文本要指到 Delete 那一条，否则想撤掉一条文字的人只能改成空格再删。
 */
TEST_F(EngineFixture, TextEditingRefusesWrongTargetsWithReasons)
{
    EXPECT_FALSE(m_engine.setTextOfSelection(QStringLiteral("x")).has_value()) << "没选中也能改，等于改到上一条身上";
    EXPECT_NE(m_engine.setTextOfSelection(QStringLiteral("x")).error().message.find("双击"), std::string::npos);

    const auto rectangle = drawRectangle(QPoint(10, 10), QPoint(90, 60));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());
    const auto wrongTool = m_engine.setTextOfSelection(QStringLiteral("x"));
    ASSERT_FALSE(wrongTool.has_value()) << "矩形被当成可以改文字的目标";
    EXPECT_FALSE(m_engine.textOfSelection().has_value()) << "选中的不是文字，还回了一份内容";

    const auto reText = placeText(m_engine, QPoint(120, 20), QStringLiteral("AB"));
    ASSERT_TRUE(m_engine.select(reText).has_value());
    const auto empty = m_engine.setTextOfSelection(QString());
    ASSERT_FALSE(empty.has_value()) << "空文本被收下，画布上会留一条看不见的图形";
    EXPECT_NE(empty.error().message.find("Delete"), std::string::npos) << "空文本要指到「删掉这一条」那条路";

    // 撤销会把选中项一起作废（见 UndoInvalidatesStaleSelection），这时改文字报的是「没选中，先双击」：
    // 两种说法都给出路，且都不许悄悄改到别的那一条上
    ASSERT_TRUE(m_engine.undo().has_value());
    const auto stale = m_engine.setTextOfSelection(QStringLiteral("CD"));
    ASSERT_FALSE(stale.has_value()) << "被撤销掉的图形还能改文字";
    EXPECT_NE(stale.error().message.find("双击"), std::string::npos) << stale.error().message;
}

/**
 * @brief 钉住文字的字体族/粗体/斜体三件也走改样式命令：选中那条当场变，撤销回原样
 * @details 三个字段与马赛克粒度、高亮透明度同属「后加的样式旋钮」：penParametersOf 与
 *          restyleShapeAt 都按字段逐个抄，漏一个就是「选中老样式的文字调样式不生效」。
 *          墨点计数在字体与平台之间都成立——粗体的笔画更厚，非白像素必然更多。
 */
TEST_F(EngineFixture, TextStyleRestyleLandsOnTheSelectedTextAndUndoesBack)
{
    const auto inkIn = [this](const QRect &box)
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

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0}).has_value());
    const auto text = placeText(m_engine, QPoint(20, 50), QStringLiteral("MM"));
    ASSERT_TRUE(m_engine.select(text).has_value());

    const QRect box(10, 20, 180, 45);
    const auto  regularInk = inkIn(box);
    ASSERT_GT(regularInk, 0) << "常规档一个字都没画出来，后面的对照就没有意义";

    // 开粗体：选中这条的墨点变多；撤一步回常规
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0, .textBold = true}).has_value());
    EXPECT_GT(inkIn(box), regularInk) << "选中这条文字的粗体没跟着开";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(inkIn(box), regularInk) << "撤销没把粗体退回常规档";
}

/**
 * @brief 钉住描边与底板也走改样式命令：灰底上的亮格计数当场变，撤销回原样
 * @details 两个字段与文字样式三件同属「后加的样式旋钮」：penParametersOf 与 restyleShapeAt
 *          都按字段逐个抄，漏一个就是「选中老样式的文字调效果不生效」。
 *          画布换成中灰 128：红字（深色）的描边与底板都取白——亮格（三通道 >200）在不开效果时
 *          一个都没有，开了就成片出现，两处各按这个口径比。撤销会作废选中项，第二处要重新选中。
 */
TEST_F(EngineFixture, TextPlateAndOutlineRestyleAndUndoBack)
{
    CapturedImage canvas;
    canvas.image = QImage(200, 100, QImage::Format_ARGB32_Premultiplied);
    canvas.image.fill(QColor(128, 128, 128));
    canvas.devicePixelRatio = 1.0;
    canvas.logicalSize      = QSize(200, 100);
    canvas.screenName       = QStringLiteral("FakeScreen-1");
    m_engine.loadCanvas(canvas);

    const auto brightIn = [this](const QRect &box)
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

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0}).has_value());
    const auto text = placeText(m_engine, QPoint(20, 60), QStringLiteral("H"));
    ASSERT_TRUE(m_engine.select(text).has_value());

    const QRect box(10, 20, 180, 70);
    ASSERT_EQ(brightIn(box), 0) << "灰底红字不该有亮格，后面的对照就没基准";

    // 开描边：白轮廓进来
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0, .textOutline = true}).has_value());
    EXPECT_GT(brightIn(box), 0) << "选中这条文字的描边没跟着开";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(brightIn(box), 0) << "撤销没把描边退回去";

    // 开底板：白板叠灰出 227，也是亮格；撤销作废了选中项，先重新选中
    ASSERT_TRUE(m_engine.select(text).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0, .textBackground = true}).has_value());
    EXPECT_GT(brightIn(box), 0) << "选中这条文字的底板没跟着开";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(brightIn(box), 0) << "撤销没把底板退回去";
}

/**
 * @brief 钉住描边色档也走改样式命令：自动档的白轮廓切到黑档当场消失、撤销回自动
 * @details 与上面那条同一个灰底装置，但这里只做**一次**改样式：连续两次改样式会被撤销栈
 *          合并成一步（引擎对同一条图形的样式命令有 merge），那样「撤销一次」回的是两处
 *          改动之前的状态，判据就指错了地方。
 */
TEST_F(EngineFixture, TextStrokeColorRestylesAndUndoesBack)
{
    CapturedImage canvas;
    canvas.image = QImage(200, 100, QImage::Format_ARGB32_Premultiplied);
    canvas.image.fill(QColor(128, 128, 128));
    canvas.devicePixelRatio = 1.0;
    canvas.logicalSize      = QSize(200, 100);
    canvas.screenName       = QStringLiteral("FakeScreen-1");
    m_engine.loadCanvas(canvas);

    const auto brightIn = [this](const QRect &box)
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

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0, .textOutline = true}).has_value());
    const auto  text = placeText(m_engine, QPoint(20, 60), QStringLiteral("H"));
    const QRect box(10, 20, 180, 70);
    ASSERT_GT(brightIn(box), 0) << "自动档的白轮廓没画出来，后面的对照没有基准";

    ASSERT_TRUE(m_engine.select(text).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 24, .fillStyle = 0, .textOutline = true, .textStrokeColorStep = 2})
                        .has_value());
    EXPECT_EQ(brightIn(box), 0) << "切到黑档之后白轮廓还在：描边色档没落到那条图形上";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_GT(brightIn(box), 0) << "撤销没把描边色退回自动档";
}

/**
 * @brief 钉住层叠按创建顺序跨工具生效：后画的压住先画的
 * @details 旧口径是按工具登记顺序画（椭圆永远盖得住矩形，与画的先后无关）；
 *          层叠号改成落定发号之后，后画的必须先被点中——这条同时钉住「发号、绘制、
 *          命中」三处读同一个号。取样区：两块图形盖同一片，中心像素看得到谁在上面；
 *          命中探针取椭圆环上的点（椭圆的内里在命中判定上不算环，见椭圆那一条直测）。
 */
TEST_F(EngineFixture, LaterShapesStackOnTopAcrossTools)
{
    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Ellipse).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 3, .fontSize = 16, .fillStyle = 2}).has_value());
    m_engine.beginStroke(QPoint(20, 20));
    m_engine.moveStroke(QPoint(120, 80));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    ASSERT_TRUE(m_engine.setActiveTool(ToolKind::Rectangle).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 3, .fontSize = 16, .fillStyle = 2}).has_value());
    m_engine.beginStroke(QPoint(20, 20));
    m_engine.moveStroke(QPoint(120, 80));
    ASSERT_TRUE(m_engine.endStroke().has_value());

    EXPECT_EQ(m_engine.exportImage().pixelColor(70, 50), QColor(0, 0, 255)) << "后画的矩形没压住先画的椭圆";
    const auto hit = m_engine.selectionAt(QPoint(70, 20));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->tool, ToolKind::Rectangle) << "命中没跟着层叠走：点的是看得见的蓝";

    ASSERT_TRUE(m_engine.select(AnnotationSelection{ToolKind::Ellipse, 0}).has_value());
    ASSERT_TRUE(m_engine.bringSelectionToFront().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(70, 50), QColor(255, 0, 0)) << "置顶没把椭圆提到矩形之上";
    const auto raisedHit = m_engine.selectionAt(QPoint(70, 20));
    ASSERT_TRUE(raisedHit.has_value());
    EXPECT_EQ(raisedHit->tool, ToolKind::Ellipse) << "置顶之后命中还在下面的矩形上";

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(70, 50), QColor(0, 0, 255)) << "撤销没把层叠顺序退回去";
}

/**
 * @brief 钉住上移/下移/置顶/置底，且已经在端点的再按一次是空动作
 * @details 空动作若进了撤销栈，用户按一次「置顶」再按 Ctrl+Z，撤掉的是上一步真实操作——
 *          这条用「连按两次置顶，撤一次该回到下移后的状态」把空动作钉出来。
 *          两条实心矩形盖同一块地，中心像素就是层叠读数。
 */
TEST_F(EngineFixture, RaiseLowerFrontBackMoveAndNoOpAtTheBoundaryCarriesNoUndoStep)
{
    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 3, .fontSize = 16, .fillStyle = 2}).has_value());
    drawRectangle(QPoint(10, 10), QPoint(90, 60));

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 3, .fontSize = 16, .fillStyle = 2}).has_value());
    drawRectangle(QPoint(10, 10), QPoint(90, 60));

    const AnnotationSelection second{ToolKind::Rectangle, 1};
    const auto                centerColor = [this] { return m_engine.exportImage().pixelColor(50, 35); };
    EXPECT_EQ(centerColor(), QColor(0, 0, 255)) << "后画的该在上面，后面的对照就没基准";

    // 下移一层：蓝的沉到红的下面
    ASSERT_TRUE(m_engine.select(second).has_value());
    ASSERT_TRUE(m_engine.lowerSelection().has_value());
    EXPECT_EQ(centerColor(), QColor(255, 0, 0)) << "下移一层没生效";

    // 置顶：蓝的回到最上面
    ASSERT_TRUE(m_engine.bringSelectionToFront().has_value());
    EXPECT_EQ(centerColor(), QColor(0, 0, 255)) << "置顶没生效";

    // 已经在最上面，再置顶是空动作：撤销一次该回到「下移之后」（红），而不是原地不动
    ASSERT_TRUE(m_engine.bringSelectionToFront().has_value());
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(centerColor(), QColor(255, 0, 0)) << "空动作进了撤销栈：这一撤撤掉了上一次真操作";

    // 再撤一步：把「下移」收回去，蓝的回到上面
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(centerColor(), QColor(0, 0, 255)) << "第二次撤销没把下移收回";

    // 置底：蓝的一沉到底
    ASSERT_TRUE(m_engine.select(second).has_value());
    ASSERT_TRUE(m_engine.sendSelectionToBack().has_value());
    EXPECT_EQ(centerColor(), QColor(255, 0, 0)) << "置底没生效";

    // 上移一层：蓝的又回到上面
    ASSERT_TRUE(m_engine.raiseSelection().has_value());
    EXPECT_EQ(centerColor(), QColor(0, 0, 255)) << "上移一层没生效";
}

/**
 * @brief 钉住选中集：加选、反选与整组删除（一步撤销）
 */
TEST_F(EngineFixture, SelectionSetSupportsAddToggleAndGroupDelete)
{
    const auto first  = drawRectangle(QPoint(10, 10), QPoint(50, 40));
    const auto second = drawRectangle(QPoint(60, 10), QPoint(100, 40));

    ASSERT_TRUE(m_engine.select(first).has_value());
    EXPECT_EQ(m_engine.selections().size(), 1U);

    ASSERT_TRUE(m_engine.addToSelection(second).has_value());
    EXPECT_EQ(m_engine.selections().size(), 2U) << "加选没进选中集";
    ASSERT_TRUE(m_engine.addToSelection(second).has_value());
    EXPECT_EQ(m_engine.selections().size(), 2U) << "同一条加选两次造出了重复项，之后整组移动会把它挪两倍";

    ASSERT_TRUE(m_engine.toggleSelection(first).has_value());
    ASSERT_EQ(m_engine.selections().size(), 1U) << "反选没把选中的那条摘掉";
    EXPECT_EQ(m_engine.selections().front().shapeIndex, second.shapeIndex) << "反选摘错了对象";

    ASSERT_TRUE(m_engine.toggleSelection(first).has_value());
    EXPECT_EQ(m_engine.selections().size(), 2U) << "反选没能把摘掉的那条再加回来";

    ASSERT_TRUE(m_engine.removeSelection().has_value());
    EXPECT_TRUE(m_engine.selections().empty());
    EXPECT_FALSE(m_engine.selectionAt(QPoint(10, 25)).has_value()) << "整组删除漏了第一条";
    EXPECT_FALSE(m_engine.selectionAt(QPoint(60, 25)).has_value()) << "整组删除漏了第二条";

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_TRUE(m_engine.selectionAt(QPoint(10, 25)).has_value()) << "一次撤销没把整组放回来";
    EXPECT_TRUE(m_engine.selectionAt(QPoint(60, 25)).has_value()) << "撤销只放回来了其中一条";
}

/**
 * @brief 钉住多选整组挪与整组改样式各是一步撤销
 * @details 多选若逐条压命令，拖一趟要点好几下 Ctrl+Z 才退得回去，改一次颜色同理。
 */
TEST_F(EngineFixture, MultiSelectionMovesAndRestylesAsOneUndoStep)
{
    const auto first  = drawRectangle(QPoint(10, 10), QPoint(50, 40));
    const auto second = drawRectangle(QPoint(60, 10), QPoint(100, 40));
    ASSERT_TRUE(m_engine.select(first).has_value());
    ASSERT_TRUE(m_engine.addToSelection(second).has_value());

    ASSERT_TRUE(m_engine.moveSelectionBy(QPoint(20, 0)).has_value());
    const auto moved = m_engine.exportImage();
    EXPECT_NE(moved.pixelColor(30, 25), QColor(255, 255, 255)) << "第一条没跟着整组挪";
    EXPECT_NE(moved.pixelColor(80, 25), QColor(255, 255, 255)) << "第二条没跟着整组挪";
    EXPECT_EQ(moved.pixelColor(10, 25), QColor(255, 255, 255)) << "第一条的原位还留着墨";

    ASSERT_TRUE(m_engine.undo().has_value());
    const auto back = m_engine.exportImage();
    EXPECT_NE(back.pixelColor(10, 25), QColor(255, 255, 255)) << "一次撤销没把整组挪回去";
    EXPECT_NE(back.pixelColor(60, 25), QColor(255, 255, 255)) << "一次撤销只退回了其中一条";
    EXPECT_EQ(back.pixelColor(30, 25), QColor(255, 255, 255)) << "撤销只退了一半";

    ASSERT_TRUE(m_engine.setPenParameters(PenParameters{.color = QColor(0, 0, 255), .lineWidth = 3, .fontSize = 16, .fillStyle = 0}).has_value());
    const auto restyled = m_engine.exportImage();
    EXPECT_EQ(restyled.pixelColor(10, 25), QColor(0, 0, 255)) << "整组改样式漏了第一条";
    EXPECT_EQ(restyled.pixelColor(60, 25), QColor(0, 0, 255)) << "整组改样式漏了第二条";

    ASSERT_TRUE(m_engine.undo().has_value());
    const auto reverted = m_engine.exportImage();
    EXPECT_EQ(reverted.pixelColor(10, 25), QColor(255, 0, 0)) << "一次撤销没把整组样式退回去";
    EXPECT_EQ(reverted.pixelColor(60, 25), QColor(255, 0, 0)) << "一次撤销只退回了其中一条的样式";
}

/**
 * @brief 钉住手柄框的八枚点与选中项的几何对上
 */
TEST_F(EngineFixture, SelectionFrameCarriesTheEightHandles)
{
    const auto rectangle = drawRectangle(QPoint(20, 30), QPoint(100, 70));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());

    const auto frame = m_engine.selectionFrame();
    ASSERT_TRUE(frame.has_value()) << "选中一条时该有手柄框";
    // 外接框按 QRectF 的包含口径量：QRect(20,30,81,41) 转成 QRectF 后右边在 101、下边在 71
    EXPECT_DOUBLE_EQ((*frame)[0].x(), 20.0);
    EXPECT_DOUBLE_EQ((*frame)[0].y(), 30.0);  // 左上
    EXPECT_DOUBLE_EQ((*frame)[2].x(), 101.0); // 右上
    EXPECT_DOUBLE_EQ((*frame)[2].y(), 30.0);
    EXPECT_DOUBLE_EQ((*frame)[4].x(), 101.0); // 右下
    EXPECT_DOUBLE_EQ((*frame)[4].y(), 71.0);

    // 多选不给手柄框
    const auto other = drawRectangle(QPoint(120, 30), QPoint(160, 70));
    ASSERT_TRUE(m_engine.addToSelection(other).has_value());
    EXPECT_FALSE(m_engine.selectionFrame().has_value()) << "多选还给手柄框，界面会按哪条画无法交代";
}

/**
 * @brief 钉住缩放拖动：从开场那一份重算、一趟是一步撤销
 * @details 每次 update 都拿开场时的外接框重算，所以中间的取整不会一格格攒成漂移；
 *          判据取「终点拖到哪、边就落在哪」与「撤一次回到开场之前」两处。
 */
TEST_F(EngineFixture, HandleDragResizesAndUndoesInOneStep)
{
    const auto rectangle = drawRectangle(QPoint(20, 30), QPoint(100, 70));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());

    ASSERT_TRUE(m_engine.beginSelectionDrag(SelectionHandle::Right, QPoint(100, 50)).has_value());
    ASSERT_TRUE(m_engine.updateSelectionDrag(QPoint(130, 50), {}).has_value());
    ASSERT_TRUE(m_engine.updateSelectionDrag(QPoint(160, 50), {}).has_value());
    m_engine.endSelectionDrag();

    const auto bounds = m_engine.selectionBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->right(), 160, 2.5) << "右边没跟着手柄走到终点";
    EXPECT_EQ(m_engine.exportImage().pixelColor(158, 50), QColor(255, 0, 0)) << "新右边线上该有墨";
    EXPECT_EQ(m_engine.exportImage().pixelColor(170, 50), QColor(255, 255, 255)) << "右边线之外不该有墨";

    ASSERT_TRUE(m_engine.undo().has_value());
    const auto undone = m_engine.selectionBounds();
    ASSERT_TRUE(undone.has_value());
    EXPECT_NEAR(undone->right(), 100, 1.5) << "一次撤销没回到开场之前（中间两次 update 占了两步？）";
    EXPECT_EQ(m_engine.exportImage().pixelColor(158, 50), QColor(255, 255, 255)) << "撤销后伸出去的那一段还留着";
}

/**
 * @brief 钉住拖动档从引擎一路走到几何：按住 Ctrl 拖右边＝以中心为不动点、两侧对向一起让
 * @details 原框 (20,30)-(101,71)，中心 x=60.5；右边拖到 120 时半程 59.5，左边该跟到 1。
 *          不带这一档时左边会留在 20——断言左边走了，才能证明修饰键真进了几何，而不是把
 *          「右边到了 120」这句普通缩放也满足的话当真。
 */
TEST_F(EngineFixture, HandleDragHonorsTheFromCenterSwitch)
{
    const auto rectangle = drawRectangle(QPoint(20, 30), QPoint(100, 70));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());

    ASSERT_TRUE(m_engine.beginSelectionDrag(SelectionHandle::Right, QPoint(100, 50)).has_value());
    ASSERT_TRUE(m_engine.updateSelectionDrag(QPoint(120, 50), SelectionDragModifiers{.fromCenter = true}).has_value());
    m_engine.endSelectionDrag();

    const auto bounds = m_engine.selectionBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->left(), 1.0, 2.5) << "Ctrl 档没把左边对向带出来（修饰键没走到几何）";
    EXPECT_NEAR(bounds->right(), 120.0, 2.5) << "右边该落在拖动点上";
}

/**
 * @brief 钉住旋转拖动：绕外接框中心转到位、可撤销；先转后缩时伸缩走图形自己的轴
 * @details 起始点在正上方（-90° 方向）、终点在正右方：增量正好 +90°。
 *          转 90° 后原来的右边线立到下方、上边线立到右侧，用像素与手柄框两处核对。
 */
TEST_F(EngineFixture, HandleDragRotatesAboutTheCenterAndUndoes)
{
    const auto rectangle = drawRectangle(QPoint(40, 40), QPoint(120, 60));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(80, 40), QColor(255, 0, 0)) << "靶子不对：上边线该在 y=40";

    ASSERT_TRUE(m_engine.beginSelectionDrag(SelectionHandle::Rotate, QPoint(80, 16)).has_value());
    ASSERT_TRUE(m_engine.updateSelectionDrag(QPoint(130, 50), {}).has_value());
    m_engine.endSelectionDrag();

    const auto frame = m_engine.selectionFrame();
    ASSERT_TRUE(frame.has_value());
    EXPECT_NEAR((*frame)[0].x(), 90.0, 1.5) << "转 90° 后左上角该转到 (90,10) 一带";
    EXPECT_NEAR((*frame)[0].y(), 10.0, 1.5);

    const auto turned = m_engine.exportImage();
    EXPECT_EQ(turned.pixelColor(80, 40), QColor(255, 255, 255)) << "转完之后原上边线那里该是空心的内里";
    EXPECT_EQ(turned.pixelColor(90, 50), QColor(255, 0, 0)) << "原来的上边线转到右侧：x=90 一带该有墨";

    // 命中走同一条旋转映射：点转过来的新位置有、点旧位置不该再有
    EXPECT_TRUE(m_engine.selectionAt(QPoint(90, 50)).has_value()) << "转过来的那条边点不中：命中没跟着旋转映射";
    EXPECT_FALSE(m_engine.selectionAt(QPoint(40, 50)).has_value()) << "旧位置还能点中：命中没跟着旋转映射";

    ASSERT_TRUE(m_engine.undo().has_value());
    const auto back = m_engine.exportImage();
    EXPECT_EQ(back.pixelColor(80, 40), QColor(255, 0, 0)) << "撤销没把旋转退回去";
    EXPECT_EQ(back.pixelColor(90, 50), QColor(255, 255, 255)) << "撤销后转出去的那条边还留着墨";
    EXPECT_TRUE(m_engine.selectionAt(QPoint(40, 50)).has_value()) << "撤销后原位该又能点中";
}

/**
 * @brief 钉住复制粘贴：副本按层叠抄、逐次偏移、整批一步撤销
 */
TEST_F(EngineFixture, CopyPasteDuplicatesWithGrowingOffsetAndOneStepUndo)
{
    ASSERT_FALSE(m_engine.pasteClipboard().has_value()) << "空剪贴板也能粘贴，画布上会凭空冒出东西";
    EXPECT_NE(m_engine.pasteClipboard().error().message.find("Ctrl+C"), std::string::npos) << "空剪贴板的提示该指出复制那一步";

    const auto rectangle = drawRectangle(QPoint(10, 10), QPoint(60, 40));
    ASSERT_FALSE(m_engine.copySelection().has_value()) << "没选中也能复制";
    ASSERT_TRUE(m_engine.select(rectangle).has_value());
    ASSERT_TRUE(m_engine.copySelection().has_value());

    ASSERT_TRUE(m_engine.pasteClipboard().has_value());
    ASSERT_EQ(m_engine.selections().size(), 1U) << "粘出来的该成为新的选中集";
    EXPECT_EQ(m_engine.selections().front().shapeIndex, 1);
    const auto firstCopy = m_engine.selectionBounds();
    ASSERT_TRUE(firstCopy.has_value());
    EXPECT_EQ(firstCopy->topLeft(), QPoint(18, 18)) << "第一份副本该带一格偏移（+8,+8）";
    EXPECT_EQ(m_engine.exportImage().pixelColor(18, 30), QColor(255, 0, 0)) << "副本的左边缘该落在 x=18";

    ASSERT_TRUE(m_engine.pasteClipboard().has_value());
    const auto secondCopy = m_engine.selectionBounds();
    ASSERT_TRUE(secondCopy.has_value());
    EXPECT_EQ(secondCopy->topLeft(), QPoint(26, 26)) << "第二次粘贴该再多偏一格，两次不会完全叠住";

    // 两次粘贴各占一步：撤一次少一份副本，再撤一次全没
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(26, 30), QColor(255, 255, 255)) << "撤销没把第二份副本收走";
    EXPECT_EQ(m_engine.exportImage().pixelColor(18, 30), QColor(255, 0, 0)) << "撤一次却把两份都收走了";
    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(18, 30), QColor(255, 255, 255)) << "撤销没把第一份副本收走";
    EXPECT_NE(m_engine.exportImage().pixelColor(10, 30), QColor(255, 255, 255)) << "原件被一起收走了";

    ASSERT_TRUE(m_engine.redo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(18, 30), QColor(255, 0, 0)) << "重做没把第一份副本放回来";

    // 多选整组复制出两份副本：一次粘贴是一步，Ctrl+Z 一下该把整批一起收走。
    // 新画两条互不重叠的矩形，让两份副本各有一条能单独探到的左边缘（复用旧图形的话
    // 新一轮粘贴的偏移又从 +8 起，副本会正好叠在既有副本上，探针分不出谁在谁不在）
    const auto third  = drawRectangle(QPoint(60, 60), QPoint(110, 90));
    const auto fourth = drawRectangle(QPoint(120, 60), QPoint(170, 90));
    ASSERT_TRUE(m_engine.select(third).has_value());
    ASSERT_TRUE(m_engine.addToSelection(fourth).has_value());
    ASSERT_TRUE(m_engine.copySelection().has_value());
    ASSERT_TRUE(m_engine.pasteClipboard().has_value());
    ASSERT_EQ(m_engine.selections().size(), 2U) << "两份副本该一起成为选中集";
    EXPECT_EQ(m_engine.exportImage().pixelColor(68, 75), QColor(255, 0, 0)) << "第一份副本没落位（左边缘该在 x=68）";
    EXPECT_EQ(m_engine.exportImage().pixelColor(128, 75), QColor(255, 0, 0)) << "第二份副本没落位（左边缘该在 x=128）";

    ASSERT_TRUE(m_engine.undo().has_value());
    EXPECT_EQ(m_engine.exportImage().pixelColor(68, 75), QColor(255, 255, 255)) << "一次撤销没收走整批：粘贴没包成一步";
    EXPECT_EQ(m_engine.exportImage().pixelColor(128, 75), QColor(255, 255, 255)) << "一次撤销只收走了其中一份副本";
}

/**
 * @brief 钉住手柄拖动的拒绝面：没抓住、没选中、多选、没开场
 */
TEST_F(EngineFixture, HandleDragRefusesWithoutASingleSelection)
{
    EXPECT_FALSE(m_engine.beginSelectionDrag(SelectionHandle::Right, QPoint(10, 10)).has_value()) << "没选中也能开拖手柄";
    EXPECT_FALSE(m_engine.updateSelectionDrag(QPoint(10, 10), {}).has_value()) << "没开场也能拖动";
    EXPECT_NE(m_engine.updateSelectionDrag(QPoint(10, 10), {}).error().message.find("手柄"), std::string::npos);

    const auto rectangle = drawRectangle(QPoint(20, 30), QPoint(100, 70));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());
    EXPECT_FALSE(m_engine.beginSelectionDrag(SelectionHandle::None, QPoint(100, 50)).has_value()) << "没抓住手柄也能开拖";

    const auto other = drawRectangle(QPoint(120, 30), QPoint(160, 70));
    ASSERT_TRUE(m_engine.addToSelection(other).has_value());
    EXPECT_FALSE(m_engine.beginSelectionDrag(SelectionHandle::Right, QPoint(100, 50)).has_value()) << "多选时还能开拖手柄";
}
