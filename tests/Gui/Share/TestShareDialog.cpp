#include "Gui/Share/ShareDialog.h"
#include "Base/Paths.h"
#include "Core/Capture/SourceInfo.h"
#include "Core/Image/ImageFormat.h"
#include "Core/Settings/NamingRules.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Core/Share/ShareRenderer.h"
#include "Core/Share/ShareStyle.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QSlider>
#include <QTemporaryDir>
#include <QWidget>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
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

    /// 宽度超过预览上限的成品：导出与预览在这里必须分出大小
    [[nodiscard]] QImage makeLargeImage()
    {
        QImage image{QSize{1600, 400}, QImage::Format_ARGB32_Premultiplied};
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
        return makeDialog(makeImage(), parent);
    }

    [[nodiscard]] std::unique_ptr<ShareDialog> makeDialog(const QImage &image, QWidget *parent = nullptr)
    {
        Core::CaptureSourceInfo source;
        source.processName = QStringLiteral("chrome.exe");
        source.windowTitle = QStringLiteral("Tabs - Demo");
        source.screenName  = QStringLiteral("DISPLAY1");
        source.capturedAt  = QDateTime::currentDateTime();
        return std::make_unique<ShareDialog>(image, source, *m_settings, parent);
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

/// 复制到剪贴板的是整尺寸渲染，而不是 1200 像素宽的预览
TEST_F(ShareDialogFixture, CopiesTheFullSizedRender)
{
    const auto dialog = makeDialog(makeLargeImage());
    auto       copied = dialog->copyToClipboard();
    ASSERT_TRUE(copied.has_value()) << copied.error().message;
    const auto expected = Core::renderShareImage(makeLargeImage(), dialog->currentStyle(), 0);
    ASSERT_TRUE(expected.has_value()) << expected.error().message;
    const auto actual  = QApplication::clipboard()->image();
    const auto preview = dialog->previewImageForTesting();
    ASSERT_FALSE(preview.isNull());
    EXPECT_EQ(actual.size(), expected->size());
    EXPECT_GT(actual.width(), preview.width());
    EXPECT_GT(expected->width(), preview.width());
    EXPECT_EQ(actual.convertToFormat(QImage::Format_ARGB32), expected->convertToFormat(QImage::Format_ARGB32));
    EXPECT_NE(dialog->statusText().indexOf(QStringLiteral("已复制")), -1);
}

/// PNG 与 JPG 都写得出来，尺寸等于整尺寸渲染而不是预览
TEST_F(ShareDialogFixture, SavesPngAndJpegAtFullSize)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto dialog   = makeDialog(makeLargeImage());
    const auto expected = Core::renderShareImage(makeLargeImage(), dialog->currentStyle(), 0);
    ASSERT_TRUE(expected.has_value()) << expected.error().message;
    const auto preview = dialog->previewImageForTesting();
    ASSERT_FALSE(preview.isNull());

    const auto pngPath = std::filesystem::path{directory.path().toStdWString()} / L"share.png";
    ASSERT_TRUE(dialog->saveToFile(pngPath).has_value());
    EXPECT_TRUE(std::filesystem::exists(pngPath));
    const auto png = QImage{QString::fromStdWString(pngPath.wstring())};
    EXPECT_EQ(png.size(), expected->size());
    EXPECT_GT(png.width(), preview.width());

    const auto jpgPath = std::filesystem::path{directory.path().toStdWString()} / L"share.jpg";
    ASSERT_TRUE(dialog->saveToFile(jpgPath).has_value());
    const auto jpg = QImage{QString::fromStdWString(jpgPath.wstring())};
    EXPECT_EQ(jpg.size(), expected->size());
    EXPECT_GT(jpg.width(), preview.width());
    EXPECT_FALSE(jpg.hasAlphaChannel());
}

/// WebP 也按整尺寸写；缺插件时跳过
TEST_F(ShareDialogFixture, SavesWebpAtFullSize)
{
    if (!QImageWriter::supportedImageFormats().contains(QByteArray{"webp"}))
    {
        GTEST_SKIP() << "当前 Qt 没有 WebP 编码插件";
    }

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto dialog   = makeDialog(makeLargeImage());
    const auto expected = Core::renderShareImage(makeLargeImage(), dialog->currentStyle(), 0);
    ASSERT_TRUE(expected.has_value()) << expected.error().message;

    const auto webpPath = std::filesystem::path{directory.path().toStdWString()} / L"share.webp";
    const auto saved    = dialog->saveToFile(webpPath);
    ASSERT_TRUE(saved.has_value()) << saved.error().message;
    EXPECT_TRUE(std::filesystem::exists(webpPath));
    const auto webp = QImage{QString::fromStdWString(webpPath.wstring())};
    EXPECT_EQ(webp.size(), expected->size());
}

/// 写不出去要给中文原因、可判定的错误码，并把状态行点亮
TEST_F(ShareDialogFixture, SaveFailureReportsAnActionableReason)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto blockerPath = std::filesystem::path{directory.path().toStdWString()} / L"blocker";
    {
        std::ofstream blocker{blockerPath};
        ASSERT_TRUE(blocker.good());
        blocker << "not a directory";
    }

    const auto dialog = makeDialog();
    const auto saved  = dialog->saveToFile(blockerPath / L"share.png");
    ASSERT_FALSE(saved.has_value());
    EXPECT_EQ(saved.error().code, Base::ErrorCode::FileWriteFailed);
    EXPECT_NE(saved.error().message.find("美化"), std::string::npos) << saved.error().message;
    EXPECT_NE(saved.error().message.find("写不进去"), std::string::npos) << saved.error().message;
    EXPECT_FALSE(dialog->statusText().isEmpty());
}

/// 保存对话框的手填后缀不在支持列表里时统一补 .png
TEST_F(ShareDialogFixture, SaveDialogPathFallsBackToPng)
{
    using std::filesystem::path;
    EXPECT_EQ(ShareDialog::savePathForDialog(path{L"share.txt"}), path{L"share.txt.png"});
    EXPECT_EQ(ShareDialog::savePathForDialog(path{L"share"}), path{L"share.png"});
    EXPECT_EQ(ShareDialog::savePathForDialog(path{L"share.PNG"}), path{L"share.PNG"});
    EXPECT_EQ(ShareDialog::savePathForDialog(path{L"share.jpeg"}), path{L"share.jpeg"});
    EXPECT_EQ(ShareDialog::savePathForDialog(path{L"share.webp"}), path{L"share.webp"});
}

/// 默认文件名用设置里的截图目录与命名模板，后缀是 .png
TEST_F(ShareDialogFixture, SuggestedPathUsesTheScreenshotDirectory)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(m_settings->setValue(Core::kPathScreenshot, directory.path()).has_value());
    const auto dialog = makeDialog();
    const auto path   = dialog->suggestedFilePath();
    EXPECT_TRUE(path.startsWith(directory.path()));
    EXPECT_TRUE(path.endsWith(QStringLiteral(".png")));
}

/// 命名模板展开来源元数据，撞上已有文件时让位
TEST_F(ShareDialogFixture, SuggestedPathExpandsTemplateAndAvoidsCollisions)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(m_settings->setValue(Core::kPathScreenshot, directory.path()).has_value());
    ASSERT_TRUE(m_settings->setValue(Core::kSaveNameTemplate, QStringLiteral("%app%_%title%_%monitor%")).has_value());
    const auto dialog = makeDialog();

    const auto first = dialog->suggestedFilePath();
    EXPECT_TRUE(first.startsWith(directory.path()));
    EXPECT_TRUE(first.endsWith(QStringLiteral(".png")));
    EXPECT_NE(first.indexOf(QStringLiteral("chrome")), -1) << first.toStdString();
    EXPECT_NE(first.indexOf(QStringLiteral("Tabs")), -1) << first.toStdString();
    EXPECT_NE(first.indexOf(QStringLiteral("DISPLAY1")), -1) << first.toStdString();

    QFile existing{first};
    ASSERT_TRUE(existing.open(QIODevice::WriteOnly));
    existing.write("x");
    existing.close();

    const auto second = dialog->suggestedFilePath();
    EXPECT_NE(second, first);
    EXPECT_TRUE(second.endsWith(QStringLiteral("-2.png"))) << second.toStdString();
}
