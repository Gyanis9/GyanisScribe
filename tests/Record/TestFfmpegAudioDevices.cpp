#include "Record/FfmpegAudioDevices.h"

#include "Record/FfmpegCommand.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>

using namespace GyanisScribe::Record;
using GyanisScribe::Base::ErrorCode;

namespace
{
    /// 等 ffmpeg 把设备列完的预算：这条链上没有别的时间判据，超了就算它没回话
    constexpr int kWaitMilliseconds = 15000;

    /// QProcess 起子进程要有应用对象；argv 缓冲要有静态存储期（Qt 只存指针不拷贝）。
    /// 应用对象由这套件建、由套件收尾：同一个可执行体里还有别的套件各建各的，
    /// 留着不销毁会让下一套再建一个时被 Qt 当场拦下（一个进程只许一个应用对象）
    int                               argumentCount  = 1;
    char                              argumentZero[] = "TestFfmpegAudioDevices";
    char                             *arguments[]    = {argumentZero, nullptr};
    std::unique_ptr<QCoreApplication> application;

    class FfmpegAudioDevicesEndToEndFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            application = std::make_unique<QCoreApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };
} // namespace

namespace
{
    /**
     * @brief 本机 ffmpeg 20.5 真的打出来的那份输出（CRLF 行尾，两路音频设备，视频段列不出来）
     * @details 判据用真实产物而不是手写的理想形状：这台机器上「麦克风」与「立体声混音」都在，
     *          而视频段那行以 Could not enumerate 开头——按标题行找音频段的写法在这里就空了。
     */
    const QString kRealListing = QStringLiteral("[dshow @ 000001F30626D100] Could not enumerate video devices (or none found).\r\n"
                                                "[dshow @ 000001F30626D100] \"麦克风 (Realtek(R) Audio)\" (audio)\r\n"
                                                "[dshow @ 000001F30626D100]   Alternative name "
                                                "\"@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\\wave_{3BEAAABD-124C-423A-9112-3668FFB0DE04}\"\r\n"
                                                "[dshow @ 000001F30626D100] \"立体声混音 (Realtek(R) Audio)\" (audio)\r\n"
                                                "[dshow @ 000001F30626D100]   Alternative name "
                                                "\"@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\\wave_{676C7DE8-EECF-4349-A6C4-C2FF82A18DCC}\"\r\n"
                                                "[in#0 @ 000001F306245880] Error opening input: Immediate exit requested\r\n"
                                                "Error opening input file dummy.\r\n");
} // namespace

/**
 * @brief 钉住真样本里的两路设备、友好名与稳定标识的归属
 * @details 一台设备的 Alternative name 必须落到它自己那一条上：串了台，
 *          用户选「麦克风」取走的却是「立体声混音」的流，录进来的声音与界面选的不是一回事。
 */
TEST(FfmpegAudioDevices, ParsesRealListingWithBothNamesPerDevice)
{
    const auto devices = parseDshowAudioDevices(kRealListing);
    ASSERT_EQ(devices.size(), 2) << "真样本里有两路音频设备，解析结果：\n" << kRealListing.toStdString();

    EXPECT_EQ(devices[0].friendlyName, QStringLiteral("麦克风 (Realtek(R) Audio)"));
    EXPECT_EQ(devices[1].friendlyName, QStringLiteral("立体声混音 (Realtek(R) Audio)"));
    EXPECT_TRUE(devices[0].alternativeName.startsWith(QStringLiteral("@device_cm_"))) << devices[0].alternativeName.toStdString();
    EXPECT_TRUE(devices[1].alternativeName.startsWith(QStringLiteral("@device_cm_"))) << devices[1].alternativeName.toStdString();
    EXPECT_NE(devices[0].alternativeName, devices[1].alternativeName) << "两路的稳定标识撞成同一个";
    EXPECT_FALSE(devices[0].alternativeName.contains(QStringLiteral("立体声"))) << "第一路把第二路的标识吃进来了";
}

/**
 * @brief 钉住孤立的 Alternative name 不凭空造出一台设备
 */
TEST(FfmpegAudioDevices, IgnoresAlternativeNameWithoutOwningDevice)
{
    const auto devices = parseDshowAudioDevices(QStringLiteral("[dshow @ 0x1]   Alternative name \"@device_cm_x\"\r\n"));
    EXPECT_TRUE(devices.isEmpty()) << "没有前置设备行时这行没有归属，硬造一台会让界面多出一个选不出的选项";
}

/**
 * @brief 钉住「一行都没认出来」算失败而不是「这台机没有设备」
 * @details 空表在界面上会被读成「没有可取的声音」，而真实的另一种原因是 ffmpeg 换了输出格式、
 *          解析跟不上。两种情况交回同一个读数就是静默变形。
 */
TEST(FfmpegAudioDevices, EmptyListingIsAFailureNotAnEmptyTable)
{
    const auto parsed = audioDevicesFromListing(QStringLiteral("nothing here\r\n"));
    ASSERT_FALSE(static_cast<bool>(parsed)) << "空清单被当成「没有设备」交回了";
    EXPECT_EQ(parsed.error().code, ErrorCode::InternalError);
    EXPECT_NE(parsed.error().message.find("nothing"), std::string::npos) << "失败原因要把输出末段带上，否则无从判断是哪种";

    const auto good = audioDevicesFromListing(kRealListing);
    ASSERT_TRUE(static_cast<bool>(good));
    EXPECT_EQ(good->size(), 2);
}

/**
 * @brief 端到端：真起 ffmpeg，把这台机器的音频输入列出来
 * @details 上面三条钉的是「样本怎么读」，这一条钉的是「进程起得来、读得到、归属落得下」——
 *          随包那一份 ffmpeg 能不能真的用于取声前的枚举，只有跑一次才知道，
 *          而设置页那两个下拉只在跑通的时候才有内容可选。
 * @note 缺 GYANISSCRIBE_FFMPEG 就 SKIP，与编码端到端同一条环境变量；把它的目录顶到 PATH 前面，
 *       于是这一条的成立条件由用例自己造，不依赖「这台机器恰好装了什么、上一套用例改了 PATH 没有」。
 */
TEST_F(FfmpegAudioDevicesEndToEndFixture, ListsThisMachineDevicesWithTheFfmpegThisBuildFinds)
{
    const auto requested = qEnvironmentVariable("GYANISSCRIBE_FFMPEG");
    if (requested.isEmpty())
    {
        GTEST_SKIP() << "没有 GYANISSCRIBE_FFMPEG，这一条要真 ffmpeg";
    }
    const auto path = QStringLiteral("%1%2%3").arg(QFileInfo(requested).absolutePath(), QDir::listSeparator(), qEnvironmentVariable("PATH"));
    qputenv("PATH", path.toLocal8Bit());

    // 走产品自己那条查找顺序（程序目录随包那份 → PATH），量的就是真实形状
    const auto program = locateFfmpeg();
    ASSERT_TRUE(static_cast<bool>(program)) << program.error().message;

    FfmpegAudioDeviceEnumerator enumerator;
    QVector<AudioInputDevice>   listed;
    std::string                 failure;
    bool                        settled = false;
    QObject::connect(&enumerator, &FfmpegAudioDeviceEnumerator::updated,
                     [&listed, &settled](const QVector<AudioInputDevice> &devices)
                     {
                         listed  = devices;
                         settled = true;
                     });
    QObject::connect(&enumerator, &FfmpegAudioDeviceEnumerator::failed,
                     [&failure, &settled](const GyanisScribe::Base::Error &error)
                     {
                         failure = error.message;
                         settled = true;
                     });

    enumerator.refresh();
    // 有界轮询等「两边任一落定」：等待的是这一步有没有回话，不是赌它多久跑完
    const QDeadlineTimer deadline{kWaitMilliseconds};
    while (!settled && !deadline.hasExpired())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    ASSERT_TRUE(settled) << "等了 " << kWaitMilliseconds << " 毫秒，ffmpeg 那一头既没列出来也没报错";
    ASSERT_TRUE(failure.empty()) << "列设备这一步失败了（找到的 ffmpeg 在 " << program->toStdString() << "）：" << failure;
    EXPECT_FALSE(listed.isEmpty()) << "进程跑完了却一条音频输入都没列出来：这台机器的 ffmpeg 与解析预期不一致";
    for (const auto &device: listed)
    {
        EXPECT_FALSE(device.friendlyName.isEmpty()) << "列出来的设备连名字都没有，下拉里就是一条空白";
    }
    EXPECT_TRUE(std::any_of(listed.begin(), listed.end(), [](const AudioInputDevice &device) { return !device.alternativeName.isEmpty(); }))
            << "全表都没有稳定标识，取声就只能按友好名匹配——换台机器或换个驱动就对不上号";
}
