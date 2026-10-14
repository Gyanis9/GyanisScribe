// 色板那一排的换算直测：默认八格、读回与拒绝、拖一格的落点算法、加色去重与分享文件的双向。
// 纯函数层，不建窗口：工具栏与设置页共用这一份，判据钉在这里就等于两处都钉住（两处各写一遍迟早分家）。

#include "Annotation/AnnotationPalette.h"

#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Ui/DesignTokens.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <vector>

namespace GyanisScribe
{
    namespace
    {
        using GyanisScribe::Annotation::addPaletteColor;
        using GyanisScribe::Annotation::annotationPalette;
        using GyanisScribe::Annotation::defaultAnnotationPalette;
        using GyanisScribe::Annotation::kPaletteCapacity;
        using GyanisScribe::Annotation::movePaletteColor;
        using GyanisScribe::Annotation::paletteFileText;
        using GyanisScribe::Annotation::paletteFromTokens;
        using GyanisScribe::Annotation::paletteToTokens;
        using GyanisScribe::Annotation::parsePaletteFileText;
        using GyanisScribe::Annotation::removePaletteColor;

        class AnnotationPaletteFixture : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                // 每条用例一份 ini：ctest 把用例当独立进程并行起，共用一份会互相盖掉
                const auto caseName = std::string_view(::testing::UnitTest::GetInstance()->current_test_info()->name());
                m_iniPath           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Palette-{}.ini", caseName);
                std::error_code ignored;
                std::filesystem::remove(m_iniPath, ignored);
                m_settings = std::make_unique<Settings::SettingsStore>(m_iniPath);
            }

            void TearDown() override
            {
                m_settings.reset();
                std::error_code ignored;
                std::filesystem::remove(m_iniPath, ignored);
            }

            [[nodiscard]] Settings::ISettingsStore &settings() const
            {
                return *m_settings;
            }

            /// 造一格颜色（不透明，与色板存的形状同一种）
            [[nodiscard]] QColor swatch(const int red, const int green, const int blue) const
            {
                return QColor{red, green, blue};
            }

            /// 造一份不重复的色板，够测试拖动与上限用
            [[nodiscard]] std::vector<QColor> makePalette(const std::size_t count) const
            {
                std::vector<QColor> palette;
                palette.reserve(count);
                for (auto index = 0u; index < count; ++index)
                {
                    palette.push_back(swatch(static_cast<int>(index + 1), 10, 10));
                }
                return palette;
            }

        private:
            std::filesystem::path                    m_iniPath;
            std::unique_ptr<Settings::SettingsStore> m_settings;
        };

    } // namespace

    /**
     * @brief 钉住「这一格没填」回到内置那八格，而不是留一张空表
     * @details 空表等于把色板整排撤掉，用户想的是「我不自定义了」，不是「我不要色板」
     */
    TEST_F(AnnotationPaletteFixture, UnsetPaletteFallsBackToTheBuiltinEight)
    {
        const auto palette = annotationPalette(settings());
        ASSERT_TRUE(palette.has_value()) << palette.error().message;
        EXPECT_EQ(palette->size(), Ui::kAnnotationPalette.size());
        EXPECT_EQ(paletteToTokens(*palette), paletteToTokens(defaultAnnotationPalette())) << "默认档与内置那张表不同解";
        EXPECT_EQ((*palette)[0], Ui::kAnnotationPalette[0]) << "第一格该是内置那八格的第一格";
    }

    /**
     * @brief 钉住存进去的顺序就是读回来的顺序（顺序本身是用户拖出来的结果）
     */
    TEST_F(AnnotationPaletteFixture, StoredPaletteReadsBackInOrder)
    {
        const QStringList tokens{QStringLiteral("#101010"), QStringLiteral("#0F0E0D"), QStringLiteral("#010203")};
        ASSERT_TRUE(settings().setValue(GyanisScribe::Settings::kAnnotationPalette, tokens).has_value());

        const auto palette = annotationPalette(settings());
        ASSERT_TRUE(palette.has_value()) << palette.error().message;
        EXPECT_EQ(paletteToTokens(*palette), tokens) << "顺序被改了或某格被丢了";
    }

    /**
     * @brief 钉住三种坏写法各自被拒：写法不对点名那一格、超上限报两个数、空清单说清出路
     * @details 超上限不截断：悄悄丢掉后面几格，用户看到的是「我存的八格少了两格」，而原因哪儿都没有
     */
    TEST_F(AnnotationPaletteFixture, BadTokensTooManyAndEmptyAreEachRefused)
    {
        const auto bad = paletteFromTokens({QStringLiteral("#FF0000"), QStringLiteral("red")});
        ASSERT_FALSE(bad.has_value());
        EXPECT_NE(bad.error().message.find("red"), std::string::npos) << "要点名坏在哪一格：" << bad.error().message;

        QStringList tooMany;
        for (auto index = 0; index <= static_cast<int>(kPaletteCapacity); ++index)
        {
            tooMany << QStringLiteral("#%1").arg(100000 + index, 6, 10, QLatin1Char('0'));
        }
        const auto overflow = paletteFromTokens(tooMany);
        ASSERT_FALSE(overflow.has_value());
        EXPECT_NE(overflow.error().message.find(std::format("{}", kPaletteCapacity)), std::string::npos) << "要报出上限：" << overflow.error().message;

        const auto empty = paletteFromTokens({});
        ASSERT_FALSE(empty.has_value());
        EXPECT_NE(empty.error().message.find("清掉"), std::string::npos) << "空清单要说清回到默认的那条路：" << empty.error().message;
    }

    /**
     * @brief 钉住「拖一格到另一格那儿」是插入而不是互换
     * @details 互换在拖过半排时会把用户的意思反着算：他要把第 0 格放到第 3 格旁边，结果换来换去还在原地
     */
    TEST_F(AnnotationPaletteFixture, MovingInsertsAtTheTargetInsteadOfSwapping)
    {
        // 这一排是 #010A0A #020A0A #030A0A #040A0A #050A0A：把第 0 格挪到第 3 格那儿，
        // 该得到「摘走首格、插到第 3 格的位置」，也就是 #02 #03 #04 #01 #05
        const auto        palette = makePalette(5);
        const QStringList inserted{QStringLiteral("#020A0A"), QStringLiteral("#030A0A"), QStringLiteral("#040A0A"), QStringLiteral("#010A0A"), QStringLiteral("#050A0A")};
        const QStringList swapped{QStringLiteral("#040A0A"), QStringLiteral("#020A0A"), QStringLiteral("#030A0A"), QStringLiteral("#010A0A"), QStringLiteral("#050A0A")};

        const QStringList moved = paletteToTokens(movePaletteColor(palette, 0, 3));
        EXPECT_EQ(moved, inserted) << "落点算错了：要的是插到那一格那儿";
        EXPECT_NE(moved, swapped) << "做成了互换：拖过半排时这正是反着算的那种错";

        // 越界与原地不动都原样交回，不静默改顺序
        EXPECT_EQ(paletteToTokens(movePaletteColor(palette, 0, 99)), paletteToTokens(palette));
        EXPECT_EQ(paletteToTokens(movePaletteColor(palette, 4, 9)), paletteToTokens(palette));
        EXPECT_EQ(paletteToTokens(movePaletteColor(palette, 2, 2)), paletteToTokens(palette));
    }

    /**
     * @brief 钉住加色那一格的三条不加：已有同色、无效颜色、表已满
     * @details 同名不同值的两格在色板上看不出差别，只是占地方；满了还硬塞就是让那一排挤出台阶
     */
    TEST_F(AnnotationPaletteFixture, AddingSkipsDuplicatesInvalidColorsAndAFullTable)
    {
        const auto palette = makePalette(3);
        const auto added   = addPaletteColor(palette, swatch(1, 10, 10));
        EXPECT_EQ(added.size(), 3) << "同一格颜色被加了两次";

        const auto invalid = addPaletteColor(palette, QColor{});
        EXPECT_EQ(invalid.size(), 3) << "无效颜色进了色板";

        const auto full = makePalette(kPaletteCapacity);
        EXPECT_EQ(addPaletteColor(full, swatch(200, 30, 40)).size(), kPaletteCapacity) << "满了还往里塞";

        const auto grown = addPaletteColor(palette, swatch(200, 30, 40));
        ASSERT_EQ(grown.size(), 4);
        EXPECT_EQ(paletteToTokens(grown).back(), QStringLiteral("#C81E28")) << "新格该加在末尾，不打乱已有的顺序";
    }

    /**
     * @brief 钉住去色只摘那一格，越界那一次什么都不改
     */
    TEST_F(AnnotationPaletteFixture, RemovingTakesOnlyThatSwatch)
    {
        const auto        palette = makePalette(4);
        const QStringList expected{QStringLiteral("#010A0A"), QStringLiteral("#030A0A"), QStringLiteral("#040A0A")};
        EXPECT_EQ(paletteToTokens(removePaletteColor(palette, 1)), expected);
        EXPECT_EQ(paletteToTokens(removePaletteColor(palette, 9)), paletteToTokens(palette)) << "越界的那一次不该改动表";
    }

    /**
     * @brief 钉住分享文件的双向：写出去能原样读回来，手写的逗号清单也认
     * @details 这一份是要能手改、能从记事本粘进来的东西，做成 JSON 就失去了「看一眼就懂」那一半
     */
    TEST_F(AnnotationPaletteFixture, PaletteFileRoundTripsAndAcceptsHandWrittenLists)
    {
        const auto palette = makePalette(3);
        const auto text    = paletteFileText(palette);
        EXPECT_TRUE(text.endsWith(QLatin1Char('\n'))) << "末尾没有换行：粘到记事本里最后一格会被当成半截";

        const auto readBack = parsePaletteFileText(text);
        ASSERT_TRUE(readBack.has_value()) << readBack.error().message;
        EXPECT_EQ(paletteToTokens(*readBack), paletteToTokens(palette));

        const auto handWritten = parsePaletteFileText(QStringLiteral("#FF0000, #00FF00\n\n#0000FF\n"));
        ASSERT_TRUE(handWritten.has_value()) << handWritten.error().message;
        const QStringList expected{QStringLiteral("#FF0000"), QStringLiteral("#00FF00"), QStringLiteral("#0000FF")};
        EXPECT_EQ(paletteToTokens(*handWritten), expected) << "逗号与空行的写法没被认下来";
    }

    /**
     * @brief 钉住空白文件与只有一行空白的文件被拒，而不是当成「清空色板」
     * @details 导入一份空东西等于把整排色板撤掉：那种事要在界面上明确按「恢复内置色板」，不是靠一份空文件误触
     */
    TEST_F(AnnotationPaletteFixture, EmptyFileIsRefusedRatherThanAnEmptyPalette)
    {
        for (const auto *empty: {"", "   ", "\n\n", " , \n"})
        {
            const auto parsed = parsePaletteFileText(QString::fromUtf8(empty));
            ASSERT_FALSE(parsed.has_value()) << empty;
        }
    }
} // namespace GyanisScribe
