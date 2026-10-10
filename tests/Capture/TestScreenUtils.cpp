#include "Capture/ScreenUtils.h"

#include <QImage>
#include <QPoint>
#include <QRect>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

using GyanisScribe::Capture::cropToLogicalRect;
using GyanisScribe::Capture::formatSelectionHint;
using GyanisScribe::Capture::regionAtPoint;
using GyanisScribe::Capture::regionFromText;
using GyanisScribe::Capture::regionToText;
using GyanisScribe::Capture::toPhysicalRect;
using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::ErrorCode;

namespace
{
    // 一帧 200×100 的物理像素图，上半涂红、下半涂蓝：裁完之后按像素颜色就能验出偏移
    CapturedImage makeFrame(qreal devicePixelRatio)
    {
        QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(255, 0, 0));
        for (int y = image.height() / 2; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                image.setPixelColor(x, y, QColor(0, 0, 255));
            }
        }
        image.setDevicePixelRatio(1.0);

        CapturedImage frame;
        frame.image            = image;
        frame.devicePixelRatio = devicePixelRatio;
        frame.logicalSize      = QSize(static_cast<int>(200 / devicePixelRatio), static_cast<int>(100 / devicePixelRatio));
        frame.screenName       = QStringLiteral("FakeScreen-1");
        return frame;
    }
} // namespace

/**
 * @brief 钉住逻辑→物理的换算在三个缩放档上的手算结果
 */
TEST(ScreenUtils, ScalesLogicalRectToPhysicalPixels)
{
    EXPECT_EQ(*toPhysicalRect(QRect(0, 0, 100, 50), 1.0), QRect(0, 0, 100, 50));
    EXPECT_EQ(*toPhysicalRect(QRect(10, 20, 40, 20), 1.0), QRect(10, 20, 40, 20));
    // dpr 1.5：起点 15/30，宽 60 高 30（40×1.5、20×1.5）
    EXPECT_EQ(*toPhysicalRect(QRect(10, 20, 40, 20), 1.5), QRect(15, 30, 60, 30));
    // dpr 2：起点 32/32，宽 96 高 48
    EXPECT_EQ(*toPhysicalRect(QRect(16, 16, 48, 24), 2.0), QRect(32, 32, 96, 48));

    const auto rejected = toPhysicalRect(QRect(0, 0, 10, 10), 0.0);
    ASSERT_FALSE(rejected.has_value()) << "缩放因子为 0 时按 1 处理会得到一张与屏幕上不等大的图";
    EXPECT_EQ(rejected.error().code, ErrorCode::InvalidArgument);
}

/**
 * @brief 钉住裁图后「像素尺寸 ÷ 缩放因子 = 逻辑尺寸」这一对互指关系，以及像素确实落在选区里
 */
TEST(ScreenUtils, CropKeepsPixelAndLogicalSizePairing)
{
    const auto frame   = makeFrame(2.0);
    const auto cropped = cropToLogicalRect(frame, QRect(10, 10, 40, 15));
    ASSERT_TRUE(cropped.has_value()) << "选区在帧内应当裁得出，实际原因：" << (cropped.has_value() ? std::string{} : cropped.error().message);

    EXPECT_EQ(cropped->image.width(), 80) << "物理宽不是逻辑宽的两倍";
    EXPECT_EQ(cropped->image.height(), 30) << "逻辑 15 高在 dpr 2 上应是 30 物理行";
    EXPECT_EQ(cropped->logicalSize, (QSize(40, 15))) << "逻辑尺寸没跟着选区走，标注层会按错的大小排版";
    EXPECT_EQ(cropped->devicePixelRatio, 2.0);
    EXPECT_EQ(cropped->screenName, QStringLiteral("FakeScreen-1")) << "来源屏幕丢了，多显示器下无法回到那一块屏";

    // 像素判据先钉格式：非 ARGB32 时按 QColor 比较的含义不同
    ASSERT_EQ(cropped->image.format(), QImage::Format_ARGB32_Premultiplied);
    // 原帧物理 0..49 行是红、50..99 行是蓝；逻辑 (10,10,40,15) → 物理 (20,20,80,30) 全落在红区
    EXPECT_EQ(cropped->image.pixelColor(0, 0), QColor(255, 0, 0)) << "裁到的第一格不是选区左上角那一格，换算排错了点";
    EXPECT_EQ(cropped->image.pixelColor(79, 29), QColor(255, 0, 0)) << "最后一格应是原帧物理 (99,49)，仍在红区";

    // 跨界选区：逻辑 y=25 → 物理 y=50 正好是蓝区第一行，颜色判据能证明偏移量而不只是尺寸
    const auto crossing = cropToLogicalRect(frame, QRect(10, 25, 40, 10));
    ASSERT_TRUE(crossing.has_value());
    EXPECT_EQ(crossing->image.pixelColor(0, 0), QColor(0, 0, 255)) << "逻辑 y=25 在 dpr 2 上应当已经进了蓝区";
    EXPECT_EQ(crossing->image.height(), 20);
}

/**
 * @brief 钉住选区超出帧边时按 ScreenUnavailable 回绝，而不是裁出一张小图
 */
TEST(ScreenUtils, RejectsRegionOutsideFrame)
{
    const auto frame = makeFrame(1.0);

    const auto overflow = cropToLogicalRect(frame, QRect(150, 10, 100, 20));
    ASSERT_FALSE(overflow.has_value()) << "超出帧宽的选区被裁成了窄图，用户会以为屏幕就这么宽";
    EXPECT_EQ(overflow.error().code, ErrorCode::ScreenUnavailable);

    const auto negative = cropToLogicalRect(frame, QRect(-5, 0, 10, 10));
    ASSERT_FALSE(negative.has_value()) << "负坐标的选区不能悄悄夹回来";
    EXPECT_EQ(negative.error().code, ErrorCode::ScreenUnavailable);
}

/**
 * @brief 钉住读数的文本形状（尺寸在前、坐标在后）
 */
TEST(ScreenUtils, FormatsSelectionHint)
{
    const auto hint = formatSelectionHint(QRect(128, 96, 1024, 640));
    EXPECT_NE(hint.indexOf(QStringLiteral("1024 × 640")), -1) << "读数要报宽高，实际是：" << hint.toStdString();
    EXPECT_NE(hint.indexOf(QStringLiteral("(128, 96)")), -1) << "读数要报起点坐标，实际是：" << hint.toStdString();
}

/**
 * @brief 钉住选区存取文本的往返与拒绝面
 */
TEST(ScreenUtils, RegionTextRoundTripsAndRejects)
{
    EXPECT_EQ(regionToText(QRect(10, 20, 40, 50)), QStringLiteral("10,20,40,50"));
    EXPECT_EQ(*regionFromText("10,20,40,50"), QRect(10, 20, 40, 50));

    EXPECT_FALSE(regionFromText("10,20,40").has_value()) << "少一段不该补 0 当高度";
    EXPECT_FALSE(regionFromText("10,20,40,50,60").has_value()) << "多一段不该忽略尾巴";
    EXPECT_FALSE(regionFromText("a,20,40,50").has_value()) << "非数字要点名是哪一段";
    EXPECT_FALSE(regionFromText("10,20,12abc,50").has_value()) << "截断成 12 就是静默变形";
    EXPECT_FALSE(regionFromText("10,20,0,50").has_value()) << "宽 0 的选区存下去会让「上次区域」复用一个空框";
    EXPECT_FALSE(regionFromText("10,20,40,-3").has_value()) << "负高同理";

    const auto withSpaces = regionFromText(" 10 , 20 , 40 , 50 ");
    ASSERT_TRUE(withSpaces.has_value()) << "手改配置时的首尾空格不该判坏";
    EXPECT_EQ(*withSpaces, QRect(10, 20, 40, 50));
}

/**
 * @brief 钉住命中测试取的是压在最上面的那一个，而不是面积最小或最后一个
 * @details 候选那份顺序就是 z 序。往后找会把用户看得见的那扇换成被压在底下的一扇——
 *          这不会报错，只会截出一张「明明点的是它」的图。
 */
TEST(ScreenUtils, HitTestTakesTheTopmostCandidate)
{
    const std::vector<QRect> candidates{QRect{0, 0, 100, 100}, QRect{20, 20, 30, 30}, QRect{50, 50, 40, 40}};

    // (25,25) 同时落在前三块里：按顺序取第一块
    EXPECT_EQ(regionAtPoint(QPoint{25, 25}, candidates), std::optional<QRect>{candidates[0]});
    // (55,55) 落在第一块与第三块里：仍取压在上面的第一块
    EXPECT_EQ(regionAtPoint(QPoint{55, 55}, candidates), std::optional<QRect>{candidates[0]});
    // 一个都不含：返回空，遮罩上不该亮任何东西
    EXPECT_FALSE(regionAtPoint(QPoint{120, 120}, candidates).has_value());
    // 空名单（平台给不出候选）：同样的空结果，不越界也不崩
    EXPECT_FALSE(regionAtPoint(QPoint{10, 10}, {}).has_value());
}
