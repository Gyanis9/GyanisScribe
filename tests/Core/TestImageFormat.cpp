// 成品保存格式的词表：token、标签、后缀三者要同解，越界的质量档要回绝而不是夹取。

#include "Core/ImageFormat.h"
#include "Core/Paths.h"
#include "Core/Types.h"

#include <QImage>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

using GyanisScribe::Core::Expected;
using GyanisScribe::Core::ImageFormat;
using GyanisScribe::Core::imageFormatExtension;
using GyanisScribe::Core::imageFormatToken;
using GyanisScribe::Core::isImageFormatWritable;
using GyanisScribe::Core::kDefaultImageQuality;
using GyanisScribe::Core::kMaximumImageQuality;
using GyanisScribe::Core::kMinimumImageQuality;
using GyanisScribe::Core::parseImageFormat;
using GyanisScribe::Core::parseImageQuality;
using GyanisScribe::Core::writableImageFormats;

namespace
{
    // 期望值手写在这一处，不从词表里推：表被改坏时用例要报红，而不是跟着一起改口径
    struct ExpectedRow
    {
        ImageFormat      format;
        std::string_view token;
        std::string_view extension;
    };

    const std::vector<ExpectedRow> kExpectedRows{
            ExpectedRow{ImageFormat::Png, "Png", "png"},
            ExpectedRow{ImageFormat::Jpeg, "Jpeg", "jpg"},
            ExpectedRow{ImageFormat::Webp, "Webp", "webp"},
    };
} // namespace

/**
 * @brief 钉住「token—档位—后缀」三者在同一张表上
 * @details 只测 parseImageFormat 的话，把后缀整列写错也照样全绿，而用户拿到的是文件名：
 *          .jpg 写成 .jpeg 之外的那些都打不开对应编码器，保存会当场失败。
 */
TEST(ImageFormat, TokenAndExtensionAgreeOnEveryRow)
{
    for (const auto &row: kExpectedRows)
    {
        EXPECT_EQ(imageFormatToken(row.format), row.token) << static_cast<int>(row.format);
        EXPECT_EQ(imageFormatExtension(row.format), row.extension) << static_cast<int>(row.format);
        const auto parsed = parseImageFormat(QString::fromUtf8(row.token.data(), static_cast<qsizetype>(row.token.size())));
        ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
        EXPECT_EQ(*parsed, row.format);
    }
}

/**
 * @brief 钉住词表外的取值被回绝，且文案给出可取的那几个值与去处
 * @details 猜一档等于把成品按另一种压缩写出去，事后无从找回原样；小写的 "png" 也一样要拒，
 *          与录屏那两格同一口径，用户手改配置时才知道规则是什么。
 */
TEST(ImageFormat, UnknownTokenIsRefusedWithRecoveryHint)
{
    for (const auto *stored: {"png", "JPG", "tiff", ""})
    {
        const auto parsed = parseImageFormat(QString::fromUtf8(stored));
        ASSERT_FALSE(parsed.has_value()) << "取值 \"{}\" 竟然被认下了" << stored;
        EXPECT_EQ(parsed.error().code, GyanisScribe::Core::ErrorCode::InvalidArgument);
        EXPECT_NE(parsed.error().message.find("Png / Jpeg / Webp"), std::string::npos) << "回绝的时候要列出可取值：" << parsed.error().message;
        EXPECT_NE(parsed.error().message.find("Save/ImageFormat"), std::string::npos) << "要点名是哪一格：" << parsed.error().message;
    }
}

/**
 * @brief 钉住质量档只在区间内被接受，越界不夹取
 * @details 把 0 悄悄当成「按默认」会让用户以为压得不够狠、实际压到最低；
 *          两端点各钉一次，边界值算不算可取这件事就没了第二种解释。
 */
TEST(ImageFormat, QualityIsRangeCheckedNotClamped)
{
    EXPECT_TRUE(parseImageQuality(kMinimumImageQuality).has_value());
    EXPECT_TRUE(parseImageQuality(kMaximumImageQuality).has_value());
    EXPECT_TRUE(parseImageQuality(kDefaultImageQuality).has_value());

    for (const int stored: {0, 5, kMaximumImageQuality + 1, -1})
    {
        const auto parsed = parseImageQuality(stored);
        ASSERT_FALSE(parsed.has_value()) << stored;
        EXPECT_EQ(parsed.error().code, GyanisScribe::Core::ErrorCode::InvalidArgument);
        EXPECT_NE(parsed.error().message.find("Save/ImageQuality"), std::string::npos) << parsed.error().message;
    }
}

/**
 * @brief 钉住「后缀决定编码器」这一条整条保存路径都建立在上面的假设真的成立
 * @details 代码里只传后缀与质量，不再传一次格式名——两个出处迟早会不同解。
 *          那就必须证明写出来的文件头确实是那一档：文件头的字节是独立于 Qt 的判据。
 */
TEST(ImageFormatWrite, ExtensionPicksTheEncoder)
{
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(QColor(10, 200, 60));

    struct Probe
    {
        ImageFormat                format;
        std::vector<unsigned char> magic; ///< 该档文件头的几个字节，按各家格式的规范写死
    };

    const std::vector<Probe> probes{
            Probe{ImageFormat::Png, {0x89, 0x50, 0x4E, 0x47}},
            Probe{ImageFormat::Jpeg, {0xFF, 0xD8, 0xFF}},
            Probe{ImageFormat::Webp, {0x52, 0x49, 0x46, 0x46}}, // RIFF....WEBP
    };

    const auto      caseName  = std::string(::testing::UnitTest::GetInstance()->current_test_info()->name());
    const auto      directory = std::filesystem::temp_directory_path() / std::format("GyanisScribe-ImageFormat-{}", caseName);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    ASSERT_TRUE(GyanisScribe::Core::Paths::ensureDirectory(directory).has_value());

    int probed = 0;
    for (const auto &probe: probes)
    {
        // 编码器是插件：这一档在这台机器上写不出来时，设置页根本不会摆它，用例也不该拿它判红
        if (!isImageFormatWritable(probe.format))
        {
            continue;
        }
        ++probed;
        const auto extension = imageFormatExtension(probe.format);
        const auto path      = directory / std::format("probe.{}", extension);
        // 质量给默认档：这一条判的是「挑对了编码器」，压缩率不在这里管
        ASSERT_TRUE(image.save(QString::fromStdWString(path.wstring()), nullptr, kDefaultImageQuality)) << "写不出这一档：Qt 的图像插件里没有它（后缀 " << extension << "）";

        std::ifstream                    input(path, std::ios::binary);
        const std::vector<unsigned char> head{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        ASSERT_GE(head.size(), probe.magic.size());
        EXPECT_TRUE(std::equal(probe.magic.begin(), probe.magic.end(), head.begin())) << "后缀 " << extension << " 写出来的不是这一档";
    }

    EXPECT_GE(probed, 2) << "两档都没跑动，说明能力过滤把可写的也筛掉了";
    std::filesystem::remove_all(directory, ignored);
}

/**
 * @brief 钉住「写不出来的那一档不会被摆出来」这条能力过滤
 * @details 容器那份 Qt 没有 WebP 的写插件（实测），Windows 那份有：判据取 Qt 自己报的可写格式。
 *          PNG 编在 QtGui 里、JPG 两边都带插件，所以至少要有这两档——筛成空表的话设置页就只剩
 *          一个不能选的下拉框，而这条会先红。
 */
TEST(ImageFormatCapability, UnwritableFormatsAreFilteredOut)
{
    const auto writable = writableImageFormats();
    EXPECT_GE(writable.size(), 2U) << "可写档位被筛得太狠，设置页会只剩一档甚至空着";
    EXPECT_NE(std::find(writable.begin(), writable.end(), ImageFormat::Png), writable.end());
    EXPECT_NE(std::find(writable.begin(), writable.end(), ImageFormat::Jpeg), writable.end());
    for (const auto format: writable)
    {
        EXPECT_TRUE(isImageFormatWritable(format)) << static_cast<int>(format);
    }
}
