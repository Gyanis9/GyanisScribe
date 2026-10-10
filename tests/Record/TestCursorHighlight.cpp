// 光标高亮的判据：坐标换算有两套（逻辑→物理要乘缩放因子，全局→帧内要减帧左上角），
// 少任何一套都会把圈画到别的地方，而画歪的圈比不画更误导看录屏的人。
#include "Record/CursorHighlight.h"

#include "Core/Types.h"
#include "Ui/DesignTokens.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QPoint>
#include <QSize>

#include <cstring>
#include <memory>

namespace
{
    using GyanisScribe::Core::ErrorCode;
    using GyanisScribe::Record::paintCursorHighlight;
    using GyanisScribe::Ui::kCursorHighlightRadius;

    std::unique_ptr<QGuiApplication> application;

    // argv 的缓冲要活在应用对象整个生命周期里：Qt 只留指针，放栈上会让之后的取用读到已释放的内存
    inline static int   kArgumentCount  = 1;
    inline static char  kArgumentZero[] = "TestCursorHighlight";
    inline static char *kArguments[]    = {kArgumentZero, nullptr};

    /// 底色取中性灰：主色是蓝系，只有来自高亮圈的像素才会让蓝色分量压过红色
    const QColor kBackground{32, 32, 32};

    QImage blankFrame(const QSize &physical)
    {
        QImage image{physical, QImage::Format_ARGB32_Premultiplied};
        image.fill(kBackground);
        return image;
    }

    /// 被改动的像素统计：个数与质心。质心是「圈心落在哪」的读数，环形对称，因此抗锯齿的毛边不影响判定
    struct PaintedArea
    {
        int    count        = 0;     ///< 与底色不同的像素个数
        double centroidX    = 0.0;   ///< 改动像素的横向质心，物理坐标
        double centroidY    = 0.0;   ///< 纵向质心
        bool   hasAccentHue = false; ///< 是否出现「蓝压过红」的一格，即主色那一圈
    };

    PaintedArea scanChanged(const QImage &image)
    {
        PaintedArea stats;
        double      sumX = 0.0;
        double      sumY = 0.0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QRgb pixel = image.pixel(x, y);
                if (qRed(pixel) == kBackground.red() && qGreen(pixel) == kBackground.green() && qBlue(pixel) == kBackground.blue())
                {
                    continue;
                }
                ++stats.count;
                sumX += x;
                sumY += y;
                // 灰底与蓝系主色混合只会让蓝大于红；把颜色换成红、白、黑这类硬编码值时这一格就不成立
                if (qBlue(pixel) > qRed(pixel) && qBlue(pixel) > qGreen(pixel))
                {
                    stats.hasAccentHue = true;
                }
            }
        }
        if (stats.count > 0)
        {
            stats.centroidX = sumX / stats.count;
            stats.centroidY = sumY / stats.count;
        }
        return stats;
    }

    /// 像素级「一点没改」：用整块缓冲比对，比逐格扫更省事也更不容易漏
    bool isUnchangedAgainst(const QImage &image, const QImage &reference)
    {
        return std::memcmp(image.constBits(), reference.constBits(), static_cast<std::size_t>(reference.sizeInBytes())) == 0;
    }

    class CursorHighlightFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            application = std::make_unique<QGuiApplication>(kArgumentCount, kArguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    /**
     * @brief 钉住「缩放因子乘对了」这一套换算
     * @details 同一格逻辑坐标在 1× 与 2× 两种帧上，圈心的物理读数必须是两倍关系；
     *          只测 1× 的话，把乘丢掉也照样全绿，而 200% 缩放的屏上圈会偏到左上角去。
     */
    TEST_F(CursorHighlightFixture, RingCenterScalesWithDevicePixelRatio)
    {
        const QPoint cursor{20, 25};

        auto oneX = blankFrame(QSize{80, 80});
        auto twoX = blankFrame(QSize{160, 160});
        // 2× 这一份带上缩放标记，模仿选区裁出来的帧：画之前要清成 1，画完原样还回去
        twoX.setDevicePixelRatio(2.0);

        const auto paintedOne = paintCursorHighlight(oneX, cursor, QPoint{}, 1.0);
        const auto paintedTwo = paintCursorHighlight(twoX, cursor, QPoint{}, 2.0);
        ASSERT_TRUE(paintedOne.has_value()) << paintedOne.error().message;
        ASSERT_TRUE(paintedTwo.has_value()) << paintedTwo.error().message;
        EXPECT_TRUE(*paintedOne);
        EXPECT_TRUE(*paintedTwo);

        const auto areaOne = scanChanged(oneX);
        const auto areaTwo = scanChanged(twoX);
        ASSERT_GT(areaOne.count, 0) << "一格都没改，这个用例就没有判据了";
        ASSERT_GT(areaTwo.count, 0);

        EXPECT_NEAR(areaOne.centroidX, cursor.x(), 1.5) << "圈心没落在光标那一格";
        EXPECT_NEAR(areaOne.centroidY, cursor.y(), 1.5);
        EXPECT_NEAR(areaTwo.centroidX, 2.0 * cursor.x(), 2.0) << "2× 帧上的圈心没按缩放因子走";
        EXPECT_NEAR(areaTwo.centroidY, 2.0 * cursor.y(), 2.0);
        EXPECT_TRUE(areaOne.hasAccentHue) << "主圈没用到 DesignTokens 里的主色";
        EXPECT_TRUE(areaTwo.hasAccentHue);

        EXPECT_DOUBLE_EQ(twoX.devicePixelRatio(), 2.0) << "画完没把缩放标记还回去，下游的换算会跟着错一格";
    }

    /**
     * @brief 钉住「帧左上角的全局读数减掉了」这一套换算
     * @details 第二块屏或选区里的帧，其原点在 (-1920,0) 这类非零位置上；只减一次或不减都会把圈画到帧外。
     */
    TEST_F(CursorHighlightFixture, FrameOriginShiftsTheRingInsideTheFrame)
    {
        auto frame = blankFrame(QSize{80, 80});

        // 这块屏在全局 (90,20) 起，光标全局 (130,60) 应当落在帧内的 (40,40)。
        // 两个分量都要不是零：只减 x 不减 y 的写法在 (90,0) 这种原点上看不出来。
        // 落点取帧的正中而不是边角：圈会被帧边界裁掉一半，裁过的环其质心就不指圈心了
        const auto painted = paintCursorHighlight(frame, QPoint{130, 60}, QPoint{90, 20}, 1.0);
        ASSERT_TRUE(painted.has_value()) << painted.error().message;
        ASSERT_TRUE(*painted);

        const auto area = scanChanged(frame);
        EXPECT_NEAR(area.centroidX, 40.0, 1.5) << "没减掉帧左上角的全局读数";
        EXPECT_NEAR(area.centroidY, 40.0, 1.5);
    }

    /**
     * @brief 钉住「光标不在这一帧里」时一个像素都不改
     * @details 多显示器与选区没罩住指针是常态：这种情况画在帧边上是错的，
     *          而返回失败会让调用方以为出了问题，所以判据是 false + 原样不动。
     */
    TEST_F(CursorHighlightFixture, CursorOutsideTheFrameLeavesTheFrameUntouched)
    {
        auto       frame     = blankFrame(QSize{60, 60});
        const auto reference = frame.copy();

        for (const QPoint cursor: {QPoint{100, 10}, QPoint{-1, 5}, QPoint{60, 30}, QPoint{59, 60}})
        {
            const auto painted = paintCursorHighlight(frame, cursor, QPoint{}, 1.0);
            ASSERT_TRUE(painted.has_value()) << painted.error().message;
            EXPECT_FALSE(*painted) << "光标在 (" << cursor.x() << "," << cursor.y() << ") 却报告画了";
        }
        EXPECT_TRUE(isUnchangedAgainst(frame, reference)) << "帧外的那几次调用改了像素";
    }

    /**
     * @brief 钉住半径按缩放因子走，物理直径与逻辑半径互指
     * @details 半径是设计令牌里那一档 16 逻辑像素；不乘缩放因子的话 200% 屏上圈只有一半大，
     *          看录屏的人分辨不出光标位置。
     */
    TEST_F(CursorHighlightFixture, RingRadiusFollowsTheDesignToken)
    {
        auto frame = blankFrame(QSize{120, 120});
        ASSERT_TRUE(*paintCursorHighlight(frame, QPoint{60, 60}, QPoint{}, 1.0));
        const auto oneX = scanChanged(frame);
        // 质心到最远改动格的距离就是外圈半径（衬圈压在主圈外面）：按逻辑半径那一档容住抗锯齿的毛边
        EXPECT_NEAR(oneX.centroidX, 60.0, 1.5);
        EXPECT_GE(oneX.count, 4 * kCursorHighlightRadius) << "改动格子太少，圈根本没画开";

        auto twoX = blankFrame(QSize{240, 240});
        ASSERT_TRUE(*paintCursorHighlight(twoX, QPoint{60, 60}, QPoint{}, 2.0));
        const auto area = scanChanged(twoX);
        // 环的面积随「半径 × 线宽」涨，两者都乘缩放因子就是平方关系；留出抗锯齿与像素取整的余量
        EXPECT_GT(area.count, 3 * oneX.count) << "2× 帧上的圈没按缩放因子放大";
    }

    /**
     * @brief 钉住坏输入的拒绝面：缩放因子不合法与空帧都要报错而不是静默
     */
    TEST_F(CursorHighlightFixture, RejectsNonPositiveRatioAndEmptyFrame)
    {
        auto frame = blankFrame(QSize{20, 20});
        for (const qreal badRatio: {0.0, -2.0})
        {
            const auto painted = paintCursorHighlight(frame, QPoint{5, 5}, QPoint{}, badRatio);
            ASSERT_FALSE(painted.has_value()) << badRatio;
            EXPECT_EQ(painted.error().code, ErrorCode::InvalidArgument) << badRatio;
            EXPECT_NE(painted.error().message.find("devicePixelRatio"), std::string::npos) << badRatio;
        }

        QImage     empty{};
        const auto onEmpty = paintCursorHighlight(empty, QPoint{5, 5}, QPoint{}, 1.0);
        ASSERT_FALSE(onEmpty.has_value());
        EXPECT_EQ(onEmpty.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(onEmpty.error().message.find("抓帧"), std::string::npos) << "报错要给出下一步做法";
    }

} // namespace
