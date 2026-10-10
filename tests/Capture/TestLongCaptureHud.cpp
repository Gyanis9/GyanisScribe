// 长截图控制条的两件事：摆在选区之外（否则会被自己拍进下一帧），以及两枚按钮各有可观察的结果。
#include "Capture/LongCaptureHud.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>

#include <gtest/gtest.h>

#include <memory>

using GyanisScribe::Capture::LongCaptureHud;

namespace
{
    std::unique_ptr<QApplication> application;

    class LongCaptureHudFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestLongCaptureHud";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    /// 按无障碍名找按钮：按钮文案会随语言变，名字是这一条用例与界面之间的约定
    [[nodiscard]] QPushButton *buttonNamed(LongCaptureHud &hud, const QString &name)
    {
        for (auto *button: hud.findChildren<QPushButton *>())
        {
            if (button->accessibleName() == name)
            {
                return button;
            }
        }
        return nullptr;
    }
} // namespace

/**
 * @brief 钉住控制条永远落在选区之外，四个方向按「下、上、右、左」依次试
 * @details 控制条一旦被拍进取样框，那一帧就与前后都对不上，接缝从此接不上；
 *          摆位是这条流程唯一的防线，所以逐个方向钉。
 */
TEST_F(LongCaptureHudFixture, HudLandsOutsideTheRegionItMustNotBeSeenIn)
{
    const QRect available{0, 0, 800, 600};
    const QSize hudSize{200, 80};

    // 选区在中部：往下摆得下
    const QRect middle{200, 100, 300, 200};
    const auto  below = LongCaptureHud::positionFor(middle, available, hudSize);
    EXPECT_GT(below.top(), middle.bottom()) << "摆得下时该落在选区下方，而不是上面或旁边";
    EXPECT_FALSE(below.intersects(middle)) << "摆的位置压住了选区";

    // 选区贴住屏底：翻到上方
    const QRect atBottom{200, available.bottom() - 200 + 1, 300, 200};
    const auto  flipped = LongCaptureHud::positionFor(atBottom, available, hudSize);
    EXPECT_TRUE(flipped.bottom() < atBottom.top() || flipped.left() > atBottom.right() || flipped.top() > atBottom.bottom()) << "贴到屏底时没躲开选区";
    EXPECT_TRUE(available.contains(flipped)) << "躲开选区时不许跑出这块屏";

    // 选区几乎占满整屏：四个方向都放不下，仍要给出一个可用区内的矩形（宁可靠角也别出屏）
    const QRect almostFull{40, 40, 700, 500};
    const auto  squeezed = LongCaptureHud::positionFor(almostFull, available, hudSize);
    EXPECT_TRUE(available.contains(squeezed)) << "没地方摆时也该留在屏内";

    // 尺寸给空就不摆：空矩形会让控件塌成一个点，用户连按钮都按不到
    EXPECT_TRUE(LongCaptureHud::positionFor(middle, available, QSize{}).isNull());
}

/**
 * @brief 钉住两枚按钮各自发出自己的信号
 * @details 「完成」与「取消」在服务里是两条完全不同的收尾：交成品还是作废，
 *          接错一枚就等于用户按了取消却存下一张图。
 */
TEST_F(LongCaptureHudFixture, ButtonsEmitTheirOwnSignals)
{
    LongCaptureHud hud;
    int            finished  = 0;
    int            cancelled = 0;
    QObject::connect(&hud, &LongCaptureHud::finished, [&finished] { ++finished; });
    QObject::connect(&hud, &LongCaptureHud::cancelled, [&cancelled] { ++cancelled; });

    auto *finishButton = buttonNamed(hud, QStringLiteral("完成长截图"));
    auto *cancelButton = buttonNamed(hud, QStringLiteral("取消长截图"));
    ASSERT_NE(finishButton, nullptr) << "没有名为「完成长截图」的按钮：无障碍名与用例不同解了";
    ASSERT_NE(cancelButton, nullptr) << "没有名为「取消长截图」的按钮";

    cancelButton->click();
    EXPECT_EQ(cancelled, 1) << "取消没发声";
    EXPECT_EQ(finished, 0) << "取消顺手把「完成」也发了";

    finishButton->click();
    EXPECT_EQ(finished, 1) << "完成没发声";
    EXPECT_EQ(cancelled, 1) << "完成又把取消发了一遍";
}

/**
 * @brief 钉住读数写在控件里而不是窗口标题上
 * @details 读数这一轮里每帧都要变；只在标题上变的读数在无边框控制条上根本看不见。
 */
TEST_F(LongCaptureHudFixture, ReadoutLandsOnTheLabelInsideTheHud)
{
    LongCaptureHud hud;
    hud.setReadout(QStringLiteral("已接 120 像素高"));

    const auto labels = hud.findChildren<QLabel *>();
    bool       found  = false;
    for (const auto *label: labels)
    {
        found = found || label->text() == QStringLiteral("已接 120 像素高");
    }
    EXPECT_TRUE(found) << "读数没落在控制条里的那行文字上";
}
