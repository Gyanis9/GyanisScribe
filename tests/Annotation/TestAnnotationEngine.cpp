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

    const auto badWidth = m_engine.setPenParameters(QColor(0, 0, 255), 0, 16);
    ASSERT_FALSE(badWidth.has_value()) << "线宽 0 会让图形变成看不见的线";
    EXPECT_NE(badWidth.error().message.find("正数"), std::string::npos);

    const auto badFont = m_engine.setPenParameters(QColor(0, 0, 255), 3, -2);
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
