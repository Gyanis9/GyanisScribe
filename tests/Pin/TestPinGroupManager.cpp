// 贴图组管理窗与其背后的三件新读数：哪几组、各有几张、叫什么，以及切组/腾空这两条动作。
// 判据分两层——管理器的公开读数（activeGroup、groupsInUse、closedPinCount）钉语义，
// 窗上的 tableReadout()/hintText() 钉「界面真摆出来了什么」；按钮一律发 clicked() 走的是用户那一条路。

#include "Pin/IPinWindow.h"
#include "Pin/PinGroupManagerWindow.h"
#include "Pin/PinManager.h"

#include "Platform/IPlatformIntegration.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>

namespace GyanisScribe
{
    namespace
    {
        std::unique_ptr<QApplication> application;

        QImage makeImage()
        {
            QImage image{32, 24, QImage::Format_ARGB32};
            image.fill(QColor{90, 140, 210});
            return image;
        }

        class PinGroupManagerFixture : public ::testing::Test
        {
        protected:
            static void SetUpTestSuite()
            {
                // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
                static int   argumentCount  = 1;
                static char  argumentZero[] = "TestPinGroupManager";
                static char *arguments[]    = {argumentZero, nullptr};
                application                 = std::make_unique<QApplication>(argumentCount, arguments);
            }

            static void TearDownTestSuite()
            {
                application.reset();
            }

            void SetUp() override
            {
                // 每条用例一份 ini：ctest 并行时同名的键会互相盖掉
                const auto caseName = std::string_view(::testing::UnitTest::GetInstance()->current_test_info()->name());
                m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-PinGroupManager-{}.ini", caseName);
                std::error_code ignored;
                std::filesystem::remove(m_iniFile, ignored);

                m_settings    = std::make_unique<Settings::SettingsStore>(m_iniFile);
                m_integration = Platform::createPlatformIntegration();
                m_pins        = std::make_unique<Pin::PinManager>(*m_settings, *m_integration);
            }

            void TearDown() override
            {
                m_window.reset();
                m_pins.reset();
                m_integration.reset();
                m_settings.reset();
                std::error_code ignored;
                std::filesystem::remove(m_iniFile, ignored);
            }

            [[nodiscard]] Pin::PinManager &pins() const
            {
                return *m_pins;
            }

            /**
             * @brief 造一扇窗并摆出来（不真上屏）
             * @details 隐藏的窗口上布局不给子控件算位置，但这里的判据读的是模型与文本，不吃几何；
             *          要按下的按钮走的是 clicked() 信号，也不需要真的能看见。
             */
            [[nodiscard]] Pin::PinGroupManagerWindow &makeWindow()
            {
                m_window = std::make_unique<Pin::PinGroupManagerWindow>(*m_pins);
                m_window->setAttribute(Qt::WA_DontShowOnScreen);
                m_window->openAndRefresh();
                return *m_window;
            }

            /// 按 accessibleName 找按钮：记序号的找法在一句话改动文案后就飘了
            [[nodiscard]] QPushButton *findButton(const QString &name) const
            {
                for (auto *button: m_window->findChildren<QPushButton *>())
                {
                    if (button->accessibleName() == name)
                    {
                        return button;
                    }
                }
                return nullptr;
            }

            /// 清空「当前行」：按下按钮却一行都没选中，就是用户还没点行的那个现场
            static void clearCurrentRow(QTableWidget &table)
            {
                table.clearSelection();
                table.setCurrentIndex(QModelIndex());
            }

            /// 桌上摆着哪几张：显隐只有 setConcealed 一个出口，判据直接读它
            [[nodiscard]] int onDeskCount() const
            {
                auto count = 0;
                for (auto index = 0; index < m_pins->count(); ++index)
                {
                    const auto *pin = m_pins->pinAt(index);
                    if (pin != nullptr && !pin->isConcealed())
                    {
                        ++count;
                    }
                }
                return count;
            }

            /// 摆成「第一组两张、第二组一张」的局面
            void makeTwoGroups() const
            {
                m_pins->addPin(makeImage(), QPoint(20, 20));
                m_pins->addPin(makeImage(), QPoint(200, 20));
                m_pins->addPin(makeImage(), QPoint(400, 20));
                EXPECT_TRUE(m_pins->movePinToGroup(2, 2).has_value()) << "前提不对：挪不进第二组";
            }

        private:
            std::filesystem::path                           m_iniFile;     ///< 本用例专用的设置文件
            std::unique_ptr<Settings::SettingsStore>        m_settings;    ///< 布局与组名的读写处
            std::unique_ptr<Platform::IPlatformIntegration> m_integration; ///< 贴图窗要的平台能力
            std::unique_ptr<Pin::PinManager>                m_pins;        ///< 被测管理器
            std::unique_ptr<Pin::PinGroupManagerWindow>     m_window;      ///< 被测窗口
        };

    } // namespace

    /**
     * @brief 钉住管理器那份概况：张数算在册的而不是看得见的，名字与活动标记跟着现值
     * @details 管理窗要回答的是「哪几组、各有几张」，收着的那几张也属于这一组——
     *          按看得见的算就会少报，用户以为那张图没了。
     */
    TEST_F(PinGroupManagerFixture, SummariesCarryCountNameAndActiveMark)
    {
        makeTwoGroups();
        ASSERT_TRUE(pins().renameGroup(2, QStringLiteral("参照对比")).has_value());

        const auto summaries = pins().groupSummaries();
        ASSERT_EQ(summaries.size(), 2) << "两组在册却报出别的行数";
        EXPECT_EQ(summaries[0].group, 1);
        EXPECT_EQ(summaries[0].pinCount, 2) << "第一组在册两张：张数算的不是此刻看得见的";
        EXPECT_TRUE(summaries[0].isActive) << "活动组没标出来";
        EXPECT_EQ(summaries[1].group, 2);
        EXPECT_EQ(summaries[1].name, QStringLiteral("参照对比"));
        EXPECT_EQ(summaries[1].pinCount, 1);
        EXPECT_FALSE(summaries[1].isActive);
    }

    /**
     * @brief 钉住「摆出点名那一组」：显隐跟着换，别的组收起来
     */
    TEST_F(PinGroupManagerFixture, ShowingAGroupMovesTheDeskToIt)
    {
        makeTwoGroups();
        ASSERT_EQ(onDeskCount(), 2) << "前提不对：此刻桌上该是第一组那两张";

        const auto shown = pins().showGroup(2);
        ASSERT_TRUE(shown.has_value()) << shown.error().message;
        EXPECT_EQ(*shown, 2);
        EXPECT_EQ(pins().activeGroup(), 2);
        EXPECT_EQ(onDeskCount(), 1) << "切过去之后该只剩这一组摆在桌上";
    }

    /**
     * @brief 钉住「摆出没人在的那一组」被拒并说清怎么多出一组
     * @details 切到一个空壳组等于把整桌收空：用户找不到那几张图，还以为程序吞了它们。
     */
    TEST_F(PinGroupManagerFixture, ShowingAnEmptyGroupIsRefusedWithTheWayToMakeOne)
    {
        makeTwoGroups();

        const auto shown = pins().showGroup(7);
        ASSERT_FALSE(shown.has_value());
        EXPECT_EQ(shown.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument);
        EXPECT_NE(shown.error().message.find("移动到分组"), std::string::npos) << "拒绝要给出路：" << shown.error().message;
        EXPECT_EQ(pins().activeGroup(), 1) << "被拒的那一次不该顺手换了活动组";
    }

    /**
     * @brief 钉住「腾空这一组」关的是这一组、走的是关闭那一档
     * @details 与右键的「销毁」分档：关掉的还按得回来，桌上的其余两组不该跟着一起消失。
     */
    TEST_F(PinGroupManagerFixture, ClosingAGroupTakesOnlyItsPinsAndKeepsThemRecoverable)
    {
        makeTwoGroups();
        ASSERT_EQ(pins().count(), 3);

        const auto closed = pins().closeGroup(2);
        EXPECT_EQ(closed, 1);
        EXPECT_EQ(pins().count(), 2) << "别组被一起关掉了";
        EXPECT_EQ(pins().closedPinCount(), 1) << "关掉的这一张该还能按回来";
        EXPECT_EQ(pins().groupsInUse().size(), 1) << "组里最后一张都没了，这一组不该还挂着";
    }

    /**
     * @brief 钉住「本来没人在的一组」腾空时报 0，而不是把别组关掉
     */
    TEST_F(PinGroupManagerFixture, ClosingAnUnknownGroupClosesNothing)
    {
        makeTwoGroups();
        EXPECT_EQ(pins().closeGroup(9), 0);
        EXPECT_EQ(pins().count(), 3);
    }

    /**
     * @brief 钉住窗上表里真摆出来的那三列：组号、显示名、张数与当前标记
     * @details 没起名字的组显示「分组 N」——只验管理器那份清单，就看不出界面把空名字摆成了什么。
     */
    TEST_F(PinGroupManagerFixture, WindowTableListsEachGroupWithItsDisplayName)
    {
        makeTwoGroups();
        auto &window = makeWindow();

        const QStringList expected{QStringLiteral("1|分组 1|2 张|是"), QStringLiteral("2|分组 2|1 张|")};
        EXPECT_EQ(window.tableReadout(), expected) << "表里摆的不是全桌那份账";
    }

    /**
     * @brief 钉住「选中一行才按得下四个动作」
     * @details 没选中就按下，动作不知道该管哪一组；界面先按不住，比事后弹一句「没选行」更省一次误会。
     */
    TEST_F(PinGroupManagerFixture, WindowButtonsWakeOnlyWithASelectedRow)
    {
        makeTwoGroups();
        auto &window = makeWindow();

        auto *table    = window.findChild<QTableWidget *>();
        auto *show     = findButton(QStringLiteral("摆出这一组"));
        auto *rename   = findButton(QStringLiteral("重命名这一组"));
        auto *empty    = findButton(QStringLiteral("腾空这一组"));
        auto *deletion = findButton(QStringLiteral("删掉这一组"));
        ASSERT_NE(table, nullptr) << "表找不到，判据就退成了「按钮不存在也算过」";
        ASSERT_NE(show, nullptr);
        ASSERT_NE(rename, nullptr);
        ASSERT_NE(empty, nullptr);
        ASSERT_NE(deletion, nullptr);

        clearCurrentRow(*table);
        EXPECT_FALSE(show->isEnabled()) << "没选中一行时「摆出这一组」按得下去";
        EXPECT_FALSE(deletion->isEnabled());

        table->setCurrentCell(1, 0);
        EXPECT_TRUE(show->isEnabled());
        EXPECT_TRUE(rename->isEnabled());
        EXPECT_TRUE(empty->isEnabled());
        EXPECT_TRUE(deletion->isEnabled());
    }

    /**
     * @brief 钉住点「摆出这一组」那一下：桌上换人，「摆在桌上」那一列跟着挪行
     * @details 管理器换活动组时不出声，窗上那一列要自己重读；不重读就是上一组还挂着「是」。
     */
    TEST_F(PinGroupManagerFixture, ShowingARowFromTheWindowMovesTheDeskAndTheMark)
    {
        makeTwoGroups();
        auto &window = makeWindow();

        auto *table = window.findChild<QTableWidget *>();
        ASSERT_NE(table, nullptr);
        table->setCurrentCell(1, 0);
        auto *show = findButton(QStringLiteral("摆出这一组"));
        ASSERT_NE(show, nullptr);
        emit show->clicked();

        EXPECT_EQ(pins().activeGroup(), 2) << "按下那一下没换活动组";
        EXPECT_EQ(onDeskCount(), 1);
        const QStringList expected{QStringLiteral("1|分组 1|2 张|"), QStringLiteral("2|分组 2|1 张|是")};
        EXPECT_EQ(window.tableReadout(), expected) << "「摆在桌上」那一列还指着上一组";
        EXPECT_NE(window.hintText().toStdString().find("第 2 组"), std::string::npos) << "按完没一句回声：" << window.hintText().toStdString();
    }

    /**
     * @brief 钉住「腾空这一组」那一下：只关这一组，提示说清还能按回来几张
     * @details 那句上限不是装饰：册子只留最近五张，用户以为整组都还能回来就会在第五张之后找不到。
     */
    TEST_F(PinGroupManagerFixture, EmptyingARowClosesOnlyThatGroupAndSaysTheRecoverableCap)
    {
        makeTwoGroups();
        auto &window = makeWindow();

        auto *table = window.findChild<QTableWidget *>();
        ASSERT_NE(table, nullptr);
        table->setCurrentCell(1, 0);
        auto *empty = findButton(QStringLiteral("腾空这一组"));
        ASSERT_NE(empty, nullptr);
        emit empty->clicked();

        EXPECT_EQ(pins().count(), 2) << "别组被一起关掉了";
        EXPECT_EQ(pins().groupsInUse().size(), 1);
        EXPECT_EQ(pins().closedPinCount(), 1);
        EXPECT_NE(window.hintText().toStdString().find("1 张"), std::string::npos) << window.hintText().toStdString();
        EXPECT_NE(window.hintText().toStdString().find("5 张"), std::string::npos) << "提示没带上限：" << window.hintText().toStdString();
        const QStringList expected{QStringLiteral("1|分组 1|2 张|是")};
        EXPECT_EQ(window.tableReadout(), expected) << "没了的那一组还挂在表上";
    }

    /**
     * @brief 钉住「删掉这一组」把成员并回第一组，名字跟着清
     */
    TEST_F(PinGroupManagerFixture, DeletingARowMergesItsPinsBackAndDropsTheName)
    {
        makeTwoGroups();
        ASSERT_TRUE(pins().renameGroup(2, QStringLiteral("参照对比")).has_value());
        auto &window = makeWindow();

        auto *table = window.findChild<QTableWidget *>();
        ASSERT_NE(table, nullptr);
        table->setCurrentCell(1, 0);
        auto *deletion = findButton(QStringLiteral("删掉这一组"));
        ASSERT_NE(deletion, nullptr);
        emit deletion->clicked();

        EXPECT_EQ(pins().groupsInUse().size(), 1);
        EXPECT_TRUE(pins().groupName(2).isEmpty()) << "组没了名字还挂着";
        EXPECT_EQ(pins().count(), 3) << "删组是把标签拿掉，不是把图扔掉";
        EXPECT_NE(window.hintText().toStdString().find("并回第一组"), std::string::npos) << window.hintText().toStdString();
    }

    /**
     * @brief 钉住删第一组被拒：那句拒绝住在管理器里，界面上不另写一套「哪些组能删」
     */
    TEST_F(PinGroupManagerFixture, DeletingTheFirstRowRepeatsTheManagerRefusal)
    {
        makeTwoGroups();
        auto &window = makeWindow();

        auto *table = window.findChild<QTableWidget *>();
        ASSERT_NE(table, nullptr);
        table->setCurrentCell(0, 0);
        auto *deletion = findButton(QStringLiteral("删掉这一组"));
        ASSERT_NE(deletion, nullptr);
        emit deletion->clicked();

        EXPECT_EQ(pins().groupsInUse().size(), 2) << "第一组被删掉了：别组没了并回去的落点";
        EXPECT_NE(window.hintText().toStdString().find("第一组"), std::string::npos) << "拒绝没回声：" << window.hintText().toStdString();
    }

    /**
     * @brief 钉住改名与取消命名在表上的两种回声
     * @details 输入框要人点，判据钉不到那一步，因此驱动的是拿完名字之后的那一步（同一个实现体）。
     */
    TEST_F(PinGroupManagerFixture, RenamingARowRewritesItsNameAndBlankReturnsToNumbered)
    {
        makeTwoGroups();
        auto &window = makeWindow();

        window.renameGroupRow(2, QStringLiteral("  参照对比  "));
        const QStringList renamed{QStringLiteral("1|分组 1|2 张|是"), QStringLiteral("2|参照对比|1 张|")};
        EXPECT_EQ(window.tableReadout(), renamed) << "首尾空白没剪，或名字没写上表";
        EXPECT_NE(window.hintText().toStdString().find("参照对比"), std::string::npos) << window.hintText().toStdString();

        window.renameGroupRow(2, QStringLiteral("   "));
        const QStringList cleared{QStringLiteral("1|分组 1|2 张|是"), QStringLiteral("2|分组 2|1 张|")};
        EXPECT_EQ(window.tableReadout(), cleared) << "取消命名后表上还挂着那个旧名字";
        EXPECT_NE(window.hintText().toStdString().find("取消命名"), std::string::npos) << window.hintText().toStdString();
    }

    /**
     * @brief 钉住「没选中一行时按下」什么都不发生
     * @details 组号取不到就不动桌上的任何东西，也不报一句像成了的提示。
     */
    TEST_F(PinGroupManagerFixture, ClicksWithoutARowLeaveTheDeskAlone)
    {
        makeTwoGroups();
        auto &window = makeWindow();

        auto *table = window.findChild<QTableWidget *>();
        ASSERT_NE(table, nullptr);
        clearCurrentRow(*table);
        auto *show = findButton(QStringLiteral("摆出这一组"));
        ASSERT_NE(show, nullptr);
        emit  show->clicked();
        auto *deletion = findButton(QStringLiteral("删掉这一组"));
        ASSERT_NE(deletion, nullptr);
        emit deletion->clicked();

        EXPECT_EQ(pins().activeGroup(), 1);
        EXPECT_EQ(pins().groupsInUse().size(), 2);
        EXPECT_TRUE(window.hintText().isEmpty()) << "没选中却报了一句，像是有什么发生了：" << window.hintText().toStdString();
    }

    /**
     * @brief 钉住一张贴图都没有时那句出路
     * @details 空表读起来像窗体坏了：这一格要写清怎么才会多出第二行。
     */
    TEST_F(PinGroupManagerFixture, EmptyManagerSaysHowToGetASecondRow)
    {
        auto &window = makeWindow();
        EXPECT_TRUE(window.tableReadout().isEmpty());
        EXPECT_NE(window.hintText().toStdString().find("新建分组"), std::string::npos) << window.hintText().toStdString();
    }
} // namespace GyanisScribe
