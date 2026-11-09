// 审核对话框的用例：清单勾选、全选/全不选、打码方式与取消语义。
// 用例不调用 exec()，只直接点按钮或改勾选状态，避免在自动化里阻塞事件循环。

#include "Core/Annotation/AnnotationTool.h"
#include "Gui/Privacy/PrivacyReviewDialog.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QRect>
#include <QSignalSpy>
#include <QString>

#include <cstddef>
#include <memory>
#include <vector>

using GyanisScribe::Core::ToolKind;
using GyanisScribe::Core::Privacy::PiiItem;
using GyanisScribe::Core::Privacy::PiiKind;
using GyanisScribe::Gui::PrivacyReviewDialog;

namespace
{
    std::unique_ptr<QApplication> application;

    class PrivacyReviewDialogFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，装置函数返回之后还会读它们
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestPrivacyReviewDialog";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    /// 造一条只带类型与脱敏样例的命中项；用例不关心框，给一个非空小矩形即可
    PiiItem makeItem(const PiiKind kind, const QString &maskedSample)
    {
        return PiiItem{kind, QRect{0, 0, 10, 10}, maskedSample};
    }

    /// 三条不同类型的样本，顺序同时用于校验 selectedItems 按列表顺序返回
    std::vector<PiiItem> makeItems()
    {
        return {makeItem(PiiKind::Phone, QStringLiteral("138****8000")), makeItem(PiiKind::Email, QStringLiteral("a***@example.com")),
                makeItem(PiiKind::IdCard, QStringLiteral("110***********1234"))};
    }

    /// 勾选状态是否全部等于给定值
    bool allCheckStatesAre(QListWidget *list, const Qt::CheckState expected)
    {
        for (int row = 0; row < list->count(); ++row)
        {
            if (list->item(row)->checkState() != expected)
            {
                return false;
            }
        }
        return true;
    }
} // namespace

/**
 * @brief 钉住默认全选、默认马赛克，且构造完不自动接受
 * @details 每一行必须由类型与脱敏样例组成，不能把原始敏感内容带进列表。
 */
TEST_F(PrivacyReviewDialogFixture, DefaultsSelectEverythingAndMosaic)
{
    const std::vector<PiiItem> items = makeItems();
    PrivacyReviewDialog        dialog{items};

    EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Rejected)) << "构造完要等用户确认，不能自动接受";
    EXPECT_EQ(dialog.redactionTool(), ToolKind::Mosaic) << "默认打码方式该是马赛克";

    const auto selected = dialog.selectedItems();
    ASSERT_EQ(selected.size(), items.size()) << "默认全选";
    EXPECT_EQ(selected[0].kind, PiiKind::Phone);
    EXPECT_EQ(selected[1].kind, PiiKind::Email);
    EXPECT_EQ(selected[2].kind, PiiKind::IdCard);

    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("privacyList"));
    ASSERT_NE(list, nullptr) << "清单要以 privacyList 命名供调用方与用例定位";
    ASSERT_EQ(list->count(), static_cast<int>(items.size()));
    EXPECT_TRUE(allCheckStatesAre(list, Qt::Checked)) << "构造完每一行都该是勾选状态";
    EXPECT_TRUE(list->item(0)->text().contains(QStringLiteral("手机号"))) << "每行要带中文类型标签";
    EXPECT_TRUE(list->item(0)->text().contains(items[0].maskedSample)) << "每行要带脱敏样例";
    EXPECT_TRUE(list->item(1)->text().contains(QStringLiteral("邮箱"))) << "邮箱行要带邮箱类型标签";

    auto *note = dialog.findChild<QLabel *>(QStringLiteral("privacyNote"));
    ASSERT_NE(note, nullptr) << "说明文字要以 privacyNote 命名供用例核对";
    EXPECT_TRUE(note->text().contains(QStringLiteral("未上传"))) << "说明要写明内容未上传";
}

/**
 * @brief 钉住每一种命中类型都有自己的中文标签
 * @details 类型分派漏掉一支时，那一行只会剩下冒号和脱敏样例，审核人看不出这是什么。
 */
TEST_F(PrivacyReviewDialogFixture, EveryKindHasChineseTypeLabel)
{
    struct KindLabel
    {
        PiiKind kind;
        QString label;
    };

    const std::vector<KindLabel> cases{{PiiKind::Phone, QStringLiteral("手机号")},         {PiiKind::Landline, QStringLiteral("固定电话")},
                                       {PiiKind::IdCard, QStringLiteral("身份证")},        {PiiKind::BankCard, QStringLiteral("银行卡")},
                                       {PiiKind::Email, QStringLiteral("邮箱")},           {PiiKind::Ip, QStringLiteral("IP 地址")},
                                       {PiiKind::Barcode, QStringLiteral("二维码/条形码")}};

    std::vector<PiiItem> items;
    items.reserve(cases.size());
    for (const auto &one: cases)
    {
        items.push_back(makeItem(one.kind, QStringLiteral("masked")));
    }

    PrivacyReviewDialog dialog{items};
    auto               *list = dialog.findChild<QListWidget *>(QStringLiteral("privacyList"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), static_cast<int>(cases.size()));

    for (int row = 0; row < list->count(); ++row)
    {
        const QString &label = cases[static_cast<std::size_t>(row)].label;
        EXPECT_TRUE(list->item(row)->text().contains(label)) << "第 " << row << " 行缺少类型标签 " << label.toStdString();
    }
}

/**
 * @brief 钉住逐项取消只影响被取消的那一条，其余顺序保持不变
 */
TEST_F(PrivacyReviewDialogFixture, UncheckingOneRemovesOnlyThatItem)
{
    const std::vector<PiiItem> items = makeItems();
    PrivacyReviewDialog        dialog{items};

    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("privacyList"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 3);
    list->item(1)->setCheckState(Qt::Unchecked);

    const auto selected = dialog.selectedItems();
    ASSERT_EQ(selected.size(), 2U) << "取消一项后应只剩两项";
    EXPECT_EQ(selected[0].kind, PiiKind::Phone);
    EXPECT_EQ(selected[1].kind, PiiKind::IdCard) << "选中项要按列表顺序返回";
    EXPECT_EQ(list->item(0)->checkState(), Qt::Checked) << "同排的第一项不该被连带取消";
    EXPECT_EQ(list->item(2)->checkState(), Qt::Checked) << "同排的第三项不该被连带取消";
}

/**
 * @brief 钉住全不选与全选按钮能一次改掉所有行的勾选状态
 */
TEST_F(PrivacyReviewDialogFixture, SelectNoneThenAllTogglesEveryRow)
{
    PrivacyReviewDialog dialog{makeItems()};

    auto *list       = dialog.findChild<QListWidget *>(QStringLiteral("privacyList"));
    auto *selectNone = dialog.findChild<QPushButton *>(QStringLiteral("privacySelectNone"));
    auto *selectAll  = dialog.findChild<QPushButton *>(QStringLiteral("privacySelectAll"));
    ASSERT_NE(list, nullptr);
    ASSERT_NE(selectNone, nullptr);
    ASSERT_NE(selectAll, nullptr);

    selectNone->click();
    EXPECT_TRUE(dialog.selectedItems().empty()) << "全不选之后不该还有选中项";
    EXPECT_TRUE(allCheckStatesAre(list, Qt::Unchecked)) << "全不选要把每行都改成未勾选";

    selectAll->click();
    EXPECT_EQ(dialog.selectedItems().size(), 3U) << "全选之后应恢复三项";
    EXPECT_TRUE(allCheckStatesAre(list, Qt::Checked)) << "全选要把每行都改成勾选";
}

/**
 * @brief 钉住一项都不勾时「应用打码」灰掉，重新勾上才恢复
 * @details 应用按钮默认是对话框的默认按钮：灰掉之后回车也不会接受一份空批次。
 */
TEST_F(PrivacyReviewDialogFixture, ApplyIsDisabledWhenNothingIsChecked)
{
    PrivacyReviewDialog dialog{makeItems()};

    auto *apply      = dialog.findChild<QPushButton *>(QStringLiteral("privacyApply"));
    auto *selectNone = dialog.findChild<QPushButton *>(QStringLiteral("privacySelectNone"));
    auto *selectAll  = dialog.findChild<QPushButton *>(QStringLiteral("privacySelectAll"));
    ASSERT_NE(apply, nullptr);
    ASSERT_NE(selectNone, nullptr);
    ASSERT_NE(selectAll, nullptr);
    EXPECT_TRUE(apply->isEnabled()) << "默认全选时应用按钮该是可点的";

    selectNone->click();
    EXPECT_FALSE(apply->isEnabled()) << "一项都没勾时应用按钮还亮着，点下去会关掉对话框却不改画布";
    EXPECT_TRUE(apply->isDefault()) << "灰掉的默认按钮仍会在回车时被触发";

    selectAll->click();
    EXPECT_TRUE(apply->isEnabled()) << "重新全选之后应用按钮没恢复";
}

/**
 * @brief 钉住风格下拉只有马赛克与高斯模糊，并各自映射到对应的 ToolKind
 */
TEST_F(PrivacyReviewDialogFixture, StyleComboMapsToToolKind)
{
    PrivacyReviewDialog dialog{makeItems()};

    auto *style = dialog.findChild<QComboBox *>(QStringLiteral("privacyStyle"));
    ASSERT_NE(style, nullptr);
    ASSERT_EQ(style->count(), 2) << "风格下拉只该有马赛克与高斯模糊两档";
    EXPECT_EQ(style->currentIndex(), 0) << "默认选中马赛克";
    EXPECT_EQ(dialog.redactionTool(), ToolKind::Mosaic);

    style->setCurrentIndex(1);
    EXPECT_EQ(dialog.redactionTool(), ToolKind::Blur) << "第二档要映射成高斯模糊";

    style->setCurrentIndex(0);
    EXPECT_EQ(dialog.redactionTool(), ToolKind::Mosaic) << "切回第一档要回到马赛克";
}

/**
 * @brief 钉住取消把已接受的结果改回 Rejected，应用按钮才把对话框置为 Accepted
 * @details 先手动置成 Accepted 再点取消：QDialog 的初始 result() 本来就是 Rejected，
 *          不先改掉的话，删掉取消按钮到 reject() 的连线这条用例照样是绿的。
 */
TEST_F(PrivacyReviewDialogFixture, CancelRejectsAndApplyAccepts)
{
    PrivacyReviewDialog dialog{makeItems()};
    auto               *cancel = dialog.findChild<QPushButton *>(QStringLiteral("privacyCancel"));
    ASSERT_NE(cancel, nullptr);

    dialog.done(static_cast<int>(QDialog::Accepted));
    ASSERT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted)) << "前提：先让对话框带上已接受的结果";

    QSignalSpy rejected{&dialog, &QDialog::rejected};
    ASSERT_TRUE(rejected.isValid()) << "要能监听 rejected 才能数清取消发了几次";

    cancel->click();
    EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Rejected)) << "取消后结果应是 Rejected";
    EXPECT_EQ(rejected.count(), 1) << "取消按钮只该发一次 rejected";

    PrivacyReviewDialog applied{makeItems()};
    auto               *apply = applied.findChild<QPushButton *>(QStringLiteral("privacyApply"));
    ASSERT_NE(apply, nullptr);
    apply->click();

    EXPECT_EQ(applied.result(), static_cast<int>(QDialog::Accepted)) << "应用打码要把对话框置为已接受";
}

/**
 * @brief 钉住空清单也能构造，但既不自动接受也不给出选中项
 * @details 生产路径不会打开空清单；这里守住测试或误用时不会凭空返回「已接受」。
 */
TEST_F(PrivacyReviewDialogFixture, EmptyItemsDoNotAutoAccept)
{
    PrivacyReviewDialog dialog{std::vector<PiiItem>{}};

    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("privacyList"));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->count(), 0);
    EXPECT_TRUE(dialog.selectedItems().empty());
    EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Rejected)) << "空清单也不能自动确认";
}
