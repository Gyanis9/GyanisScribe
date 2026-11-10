#include "Gui/Share/ShareDialog.h"
#include "Core/Capture/SourceInfo.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Core/Share/ShareStyle.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QImage>
#include <QSlider>
#include <QWidget>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>

using namespace GyanisScribe;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::ShareDialog;

namespace
{
    std::unique_ptr<QApplication> application;

    [[nodiscard]] QImage makeImage()
    {
        QImage image{QSize{400, 200}, QImage::Format_ARGB32_Premultiplied};
        image.fill(QColor{30, 120, 200});
        return image;
    }
} // namespace

class ShareDialogFixture : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        static int   argumentCount  = 1;
        static char  argumentZero[] = "TestGuiShare";
        static char *arguments[]    = {argumentZero, nullptr};
        application                 = std::make_unique<QApplication>(argumentCount, arguments);
    }

    static void TearDownTestSuite()
    {
        application.reset();
    }

    void SetUp() override
    {
        const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        m_iniPath           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-ShareDialog-{}.ini", caseName);
        std::error_code ignored;
        std::filesystem::remove(m_iniPath, ignored);
        m_settings = std::make_unique<SettingsStore>(m_iniPath);
    }

    void TearDown() override
    {
        m_settings.reset();
        std::error_code ignored;
        std::filesystem::remove(m_iniPath, ignored);
    }

    [[nodiscard]] std::unique_ptr<ShareDialog> makeDialog(QWidget *parent = nullptr)
    {
        Core::CaptureSourceInfo source;
        source.processName = QStringLiteral("chrome.exe");
        source.windowTitle = QStringLiteral("Tabs - Demo");
        source.screenName  = QStringLiteral("DISPLAY1");
        source.capturedAt  = QDateTime::currentDateTime();
        return std::make_unique<ShareDialog>(makeImage(), source, *m_settings, parent);
    }

    std::filesystem::path          m_iniPath;
    std::unique_ptr<SettingsStore> m_settings;
};

/// 初始模板来自 Share/LastTemplate；缺失时回到 light
TEST_F(ShareDialogFixture, StartsOnTheLastUsedTemplate)
{
    ASSERT_TRUE(m_settings->setValue(Core::kShareLastTemplate, QStringLiteral("dark")).has_value());
    const auto dialog = makeDialog();
    EXPECT_EQ(dialog->selectedTemplateId(), QStringLiteral("dark"));
    EXPECT_EQ(dialog->currentStyle().backgroundTop, QColor(0x1E, 0x1F, 0x22));
    EXPECT_FALSE(dialog->previewImageForTesting().isNull());
}

/// 换模板写回设置并换样式
TEST_F(ShareDialogFixture, SwitchingTemplatePersistsTheChoice)
{
    const auto  dialog = makeDialog();
    auto *const combo  = dialog->findChild<QComboBox *>(QStringLiteral("shareTemplateCombo"));
    ASSERT_NE(combo, nullptr);
    const auto index = combo->findData(QStringLiteral("gradient"));
    ASSERT_GE(index, 0);
    combo->setCurrentIndex(index);
    EXPECT_EQ(dialog->selectedTemplateId(), QStringLiteral("gradient"));
    EXPECT_TRUE(dialog->currentStyle().gradient);
    EXPECT_EQ(m_settings->stringValue(Core::kShareLastTemplate, QString{}), QStringLiteral("gradient"));
}

/// 改内边距后预览变宽
TEST_F(ShareDialogFixture, ChangingPaddingResizesThePreview)
{
    const auto  dialog = makeDialog();
    const auto  before = dialog->previewImageForTesting().size();
    auto *const slider = dialog->findChild<QSlider *>(QStringLiteral("sharePadding"));
    ASSERT_NE(slider, nullptr);
    slider->setValue(200);
    dialog->flushPreviewForTesting();
    EXPECT_GT(dialog->previewImageForTesting().width(), before.width());
}

/// 另存为模板 → 设置里出现 JSON、下拉多一项；删除 → 回落 light
TEST_F(ShareDialogFixture, SavesAndDeletesCustomTemplates)
{
    const auto dialog = makeDialog();
    auto       saved  = dialog->saveTemplateAs(QStringLiteral("我的模板"));
    ASSERT_TRUE(saved.has_value()) << saved.error().message;
    EXPECT_NE(m_settings->stringValue(Core::kShareTemplates, QString{}).indexOf(QStringLiteral("我的模板")), -1);
    EXPECT_EQ(dialog->selectedTemplateId().startsWith(QStringLiteral("custom-")), true);

    auto removed = dialog->deleteSelectedTemplate();
    ASSERT_TRUE(removed.has_value()) << removed.error().message;
    EXPECT_EQ(dialog->selectedTemplateId(), QStringLiteral("light"));
    EXPECT_EQ(m_settings->stringValue(Core::kShareTemplates, QString{}).indexOf(QStringLiteral("我的模板")), -1) << "删掉后设置里还留着这套模板";
}

/// 坏模板名拒绝并给出原因；内置模板不能删
TEST_F(ShareDialogFixture, RejectsBadTemplateNamesAndBuiltInDeletion)
{
    const auto dialog = makeDialog();
    EXPECT_FALSE(dialog->saveTemplateAs(QString{}).has_value());
    EXPECT_FALSE(dialog->saveTemplateAs(QString(41, QChar(u'名'))).has_value());
    EXPECT_FALSE(dialog->saveTemplateAs(QStringLiteral("留白")).has_value());
    EXPECT_FALSE(dialog->statusText().isEmpty());

    auto *const combo = dialog->findChild<QComboBox *>(QStringLiteral("shareTemplateCombo"));
    ASSERT_NE(combo, nullptr);
    combo->setCurrentIndex(combo->findData(QStringLiteral("light")));
    EXPECT_FALSE(dialog->deleteSelectedTemplate().has_value());
}

/// 关对话框不影响父窗口
TEST_F(ShareDialogFixture, ClosingKeepsTheParentAlive)
{
    auto parent = std::make_unique<QWidget>();
    parent->show();
    auto dialog = makeDialog(parent.get());
    dialog->show();
    EXPECT_TRUE(dialog->isVisible());
    dialog->close();
    EXPECT_FALSE(dialog->isVisible());
    EXPECT_TRUE(parent->isVisible());
}
