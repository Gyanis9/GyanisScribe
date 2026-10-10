#include "Record/FfmpegAudioDevices.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <gtest/gtest.h>

using namespace GyanisScribe::Record;
using GyanisScribe::Core::ErrorCode;

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
