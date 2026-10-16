// 从盘上读回一张图：历史页的「重新识别」与贴图的历史转贴共用这一处。
// 读不出来时必须交回写明原因的失败——给一张空图等于让下游去猜「是没图还是读失败」。

#include "Core/Image/ImageLoading.h"

#include "Base/Paths.h"
#include "Base/Types.h"

#include <QApplication>
#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>

using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Core::loadImageFile;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 图片读写要一套 Qt 运行时：格式处理器的注册跟着应用对象走
    class ImageLoadingFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestImageLoading";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        /// 每个用例用自己的目录名，并行跑（ctest 逐用例起进程）时不会共用同一棵树
        [[nodiscard]] static std::filesystem::path probeDirectory(std::string_view name)
        {
            return std::filesystem::temp_directory_path() / std::format("GyanisScribe-ImageLoading-{}", name);
        }
    };
} // namespace

/**
 * @brief 写下去再读回来要是同一张：尺寸与像素都对得上
 * @details 期望值不来自被测函数的输出，而是写死的那张图的尺寸与颜色。
 */
TEST_F(ImageLoadingFixture, ReadsBackTheImageItJustWrote)
{
    const auto      directory = probeDirectory("roundtrip");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory, ignored);
    const auto path = directory / "probe.png";

    QImage source{8, 4, QImage::Format_ARGB32_Premultiplied};
    source.fill(QColor{10, 200, 30});
    ASSERT_TRUE(source.save(GyanisScribe::Base::Paths::toDisplayText(path), "PNG")) << "造样本图都失败了，后面的判据无从谈起";

    const auto loaded = loadImageFile(path);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    EXPECT_EQ(loaded->size(), source.size()) << "读回来的尺寸不对";
    EXPECT_EQ(loaded->pixelColor(1, 1), QColor(10, 200, 30)) << "读回来的不是写下去的那张";

    std::filesystem::remove_all(directory, ignored);
}

/**
 * @brief 读不到的文件要失败，且文案点名是哪一条、给出下一步
 * @details 这条路跑在历史页与贴图两条入口后面：静默给一张空图，用户看到的是
 *          「识别结果空白」或「贴上一张空白」，而真正的原因（文件没了 / 缺解码插件）一个字都没露。
 */
TEST_F(ImageLoadingFixture, MissingFileFailsWithAReasonNamingThePathAndTheNextStep)
{
    const auto missing = probeDirectory("missing") / "not-here.png";

    const auto loaded = loadImageFile(missing);
    ASSERT_FALSE(loaded.has_value()) << "读不到的文件必须报失败，不能给一张空图";
    EXPECT_EQ(loaded.error().code, ErrorCode::DependencyMissing);
    EXPECT_NE(loaded.error().message.find("not-here.png"), std::string::npos) << "文案要点名是哪一条：" << loaded.error().message;
    EXPECT_NE(loaded.error().message.find("移动或删掉"), std::string::npos) << "文案要给出下一步：" << loaded.error().message;
}
