#include "Annotation/AnnotationEngine.h"

#include <QApplication>
#include <QColor>
#include <QImage>

#include <gtest/gtest.h>


#include <memory>
#include <string>

using GyanisScribe::Annotation::AnnotationEngine;
using GyanisScribe::Annotation::AnnotationSelection;
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

    const auto badWidth = m_engine.setPenParameters(QColor(0, 0, 255), 0, 16, 0);
    ASSERT_FALSE(badWidth.has_value()) << "线宽 0 会让图形变成看不见的线";
    EXPECT_NE(badWidth.error().message.find("正数"), std::string::npos);

    const auto badFont = m_engine.setPenParameters(QColor(0, 0, 255), 3, -2, 0);
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
    EXPECT_FALSE(m_engine.selection().has_value()) << "删掉的图形还留在选中里，下一次操作会打到邻居";
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
 * @brief 钉住跨工具的命中顺序按层叠（后登记的工具在上）
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
    EXPECT_FALSE(m_engine.selection().has_value()) << "被撤掉的图形还留在选中里，下一次移动会打到别处";

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

    ASSERT_TRUE(m_engine.setPenParameters(QColor(0, 0, 255), 8, 16, 0).has_value());
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

    ASSERT_TRUE(m_engine.setPenParameters(QColor(0, 0, 255), 4, 16, 0).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(QColor(0, 255, 0), 6, 16, 0).has_value());
    ASSERT_TRUE(m_engine.setPenParameters(QColor(0, 0, 255), 8, 16, 0).has_value());
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
    ASSERT_TRUE(m_engine.setPenParameters(QColor(0, 0, 255), 8, 16, 0).has_value());
    EXPECT_FALSE(m_engine.canUndo()) << "画布上什么都没有，改默认值不该进撤销栈";

    const auto rectangle = drawRectangle(QPoint(10, 10), QPoint(90, 60));
    ASSERT_TRUE(m_engine.select(rectangle).has_value());
    // 上面那次把默认值改成了蓝/8，新画的这条就是蓝/8：再下发同一组值等于什么都没改
    ASSERT_TRUE(m_engine.setPenParameters(QColor(0, 0, 255), 8, 16, 0).has_value());

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
    ASSERT_TRUE(m_engine.setPenParameters(QColor(0, 0, 255), 3, 16, 0).has_value());

    const QImage restyled = m_engine.exportImage();
    EXPECT_EQ(restyled.pixelColor(10, 35), QColor(0, 0, 255)) << "选中的那条没改";
    EXPECT_EQ(restyled.pixelColor(60, 35), QColor(255, 0, 0)) << "没选中的那条被一起改了";
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
