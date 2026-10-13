// 未保存截图那一份像素的落盘：写得出去、读得回来、按张数裁最旧的、撞名不覆盖别人的那一份。
// 目录由用例自己给（每条用例一份临时目录），并行跑时两条用例不会互相裁掉对方的文件。
#include "History/UnsavedArchive.h"

#include "Core/Paths.h"
#include "Core/Types.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QImage>
#include <QSize>
#include <QTime>

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>

using GyanisScribe::Core::ErrorCode;
using GyanisScribe::History::archiveUnsavedCapture;
using GyanisScribe::History::kUnsavedArchiveCapacity;

namespace
{
    /// 用例里一律用这个时刻起步，需要「下一次」时按秒往上加，不靠机器时钟
    const QDateTime kMoment{QDate{2026, 10, 13}, QTime{9, 30, 0}};

    QImage makeSample(const QColor &color)
    {
        QImage image{3, 2, QImage::Format_ARGB32};
        image.fill(color);
        return image;
    }

    /// 数目录里由这一功能写下的 PNG：判「裁掉几张」要看真实盘上剩下什么，不看返回值
    std::size_t pngCount(const std::filesystem::path &directory)
    {
        std::error_code ignored;
        std::size_t     count{};
        for (const auto &entry: std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, ignored))
        {
            if (entry.is_regular_file(ignored) && entry.path().extension() == std::filesystem::path(L".png"))
            {
                ++count;
            }
        }
        return count;
    }

    /**
     * @brief 每条用例一份独立的临时目录
     * @details 上限那一条要往目录里连写五张，共用一份目录时并行用例会把对方的张数算进来
     */
    class UnsavedArchiveFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_directory = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Unsaved-{}", ::testing::UnitTest::GetInstance()->current_test_info()->name());
            std::error_code cleanup;
            std::filesystem::remove_all(m_directory, cleanup);
        }

        void TearDown() override
        {
            std::error_code cleanup;
            std::filesystem::remove_all(m_directory, cleanup);
        }

        /// 交给 archiveUnsavedCapture 的那份目录（用例开始前不存在，函数要自己建）
        [[nodiscard]] const std::filesystem::path &directory() const
        {
            return m_directory;
        }

        std::filesystem::path m_directory; ///< 本用例独占的目录
    };

    /**
     * @brief 钉住写出去的那一份读回来还是同一张图，透明通道也还在
     * @details 这一份存在的唯一理由是「事后还能把那张找回来看」，写进去读不出等于没写。
     *          透明那层单独判：按设置里的格式走有损档时这一层就没了，而这里是兜底份。
     */
    TEST_F(UnsavedArchiveFixture, ArchivedCaptureKeepsItsPixelsAndAlpha)
    {
        const auto archived = archiveUnsavedCapture(makeSample(QColor{0, 200, 0, 128}), kMoment, directory(), kUnsavedArchiveCapacity);
        ASSERT_TRUE(archived.has_value()) << archived.error().message;
        EXPECT_TRUE(std::filesystem::is_regular_file(*archived)) << "返回的路径上并没有文件";
        EXPECT_EQ(archived->extension(), std::filesystem::path(L".png")) << "兜底那一份必须是无损档，有损档会把用户没存过的图再压一次";

        const QImage restored{GyanisScribe::Core::Paths::toDisplayText(*archived)};
        ASSERT_FALSE(restored.isNull());
        const QSize expectedSize{3, 2};
        EXPECT_EQ(restored.size(), expectedSize);
        EXPECT_TRUE(restored.hasAlphaChannel()) << "透明通道在落盘那一步丢了";
        const QColor sampleColor{0, 200, 0, 128};
        EXPECT_EQ(restored.pixelColor(1, 1), sampleColor);
    }

    /**
     * @brief 钉住空像素被拒而不是一张 0×0 的文件
     * @details 空图照样能写出一张合法 PNG，历史清单会指着它说「有这么一张」——那是静默变形，不是失败
     */
    TEST_F(UnsavedArchiveFixture, NullImageIsRejectedWithoutWritingAnything)
    {
        const auto archived = archiveUnsavedCapture(QImage{}, kMoment, directory(), kUnsavedArchiveCapacity);
        ASSERT_FALSE(archived.has_value());
        EXPECT_EQ(archived.error().code, ErrorCode::InvalidArgument);

        std::error_code exists;
        EXPECT_FALSE(std::filesystem::exists(directory(), exists)) << "拒了却还是把目录与文件留下了";
    }

    /**
     * @brief 钉住超出上限时裁的是最旧的那几张，最近那张留着
     * @details 留多少张是这一档对盘的承诺上限；裁错方向（留旧删新）会把用户刚丢的那一张先删掉，
     *          而那正是这一档要救的东西。
     */
    TEST_F(UnsavedArchiveFixture, OldestBeyondCapacityIsPruned)
    {
        constexpr std::size_t capacity = 3;
        std::filesystem::path newest;
        for (std::size_t index = 0; index < capacity + 2; ++index)
        {
            const auto archived = archiveUnsavedCapture(makeSample(QColor{static_cast<int>(index), 10, 10}), kMoment.addSecs(static_cast<int>(index)), directory(), capacity);
            ASSERT_TRUE(archived.has_value()) << archived.error().message;
            newest = *archived;
        }

        EXPECT_EQ(pngCount(directory()), capacity) << "写完五张之后目录里该只留上限那三张";
        EXPECT_TRUE(std::filesystem::exists(newest)) << "最新那一张被裁掉了，裁的方向反了";
    }

    /**
     * @brief 钉住上限之外只动自己写下的 PNG
     * @details 这个目录按名字归本功能所有，但用户会往里放东西、也会放别的后缀；
     *          「按张数全删」等于删掉别人的文件，那是不可逆的那一步。
     */
    TEST_F(UnsavedArchiveFixture, ForeignFilesInTheDirectorySurvive)
    {
        std::error_code made;
        std::filesystem::create_directories(directory(), made);
        const auto foreign = directory() / "notes.txt";
        std::filesystem::remove(foreign, made);
        std::ofstream{foreign} << "x";

        // 先放一张自己的，再放一张：上限 1 会触发裁一次，被裁的只能是旧的那张自己人
        ASSERT_TRUE(archiveUnsavedCapture(makeSample(QColor{1, 1, 1}), kMoment, directory(), 1).has_value());
        ASSERT_TRUE(archiveUnsavedCapture(makeSample(QColor{2, 2, 2}), kMoment.addSecs(1), directory(), 1).has_value());

        std::error_code exists;
        EXPECT_TRUE(std::filesystem::exists(foreign, exists)) << "裁张数时把目录里别人的文件也删了";
        EXPECT_EQ(pngCount(directory()), 1u) << "自己那两份没裁到上限";
    }

    /**
     * @brief 钉住同一毫秒里连拍两张不互相覆盖
     * @details 时间戳一样时文件名就一样，后一张盖掉前一张是「静默丢一张」；
     *          两条路径必须不同、两份像素都留在盘上。
     */
    TEST_F(UnsavedArchiveFixture, SameInstantDoesNotOverwriteTheEarlierCapture)
    {
        const auto first = archiveUnsavedCapture(makeSample(QColor{200, 0, 0}), kMoment, directory(), kUnsavedArchiveCapacity);
        ASSERT_TRUE(first.has_value()) << first.error().message;
        const auto second = archiveUnsavedCapture(makeSample(QColor{0, 0, 200}), kMoment, directory(), kUnsavedArchiveCapacity);
        ASSERT_TRUE(second.has_value()) << second.error().message;

        EXPECT_NE(*first, *second) << "同一时刻的两张交出了同一个路径，前一张已经被盖掉";
        EXPECT_EQ(pngCount(directory()), 2u);
        const QImage earlier{GyanisScribe::Core::Paths::toDisplayText(*first)};
        const QColor firstColor{200, 0, 0};
        EXPECT_EQ(earlier.pixelColor(0, 0), firstColor) << "旧的那一张内容已经变了";
    }
} // namespace
