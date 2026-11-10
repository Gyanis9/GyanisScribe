#include "Gui/Share/ShareDialog.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Image/ImageFormat.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/NamingRules.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Share/ShareRenderer.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/CheckBox.h"
#include "Gui/Controls/ComboBox.h"
#include "Gui/Controls/LineEdit.h"
#include "Gui/Controls/ScrollBar.h"
#include "Gui/Controls/Slider.h"
#include "Gui/Theme/DesignTokens.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include <utility>

namespace
{
    using GyanisScribe::Base::uiFormat;
    using GyanisScribe::Base::uiText;
    using GyanisScribe::Base::uiTextUtf8;
    using GyanisScribe::Core::ShareWatermarkCorner;
    using GyanisScribe::Gui::applyTextRole;
    using GyanisScribe::Gui::TextRole;

    /**
     * @brief 表单里的小节标题
     * @param parent 归属控件
     * @param text 已经过 uiText 的标题
     * @return 归 parent 所有的标题标签
     */
    QLabel *makeSectionLabel(QWidget *parent, const QString &text)
    {
        auto *label = new QLabel{text, parent};
        applyTextRole(*label, TextRole::Body);
        auto font = label->font();
        font.setWeight(QFont::DemiBold);
        label->setFont(font);
        return label;
    }

    /**
     * @brief 让颜色按钮以当前颜色作底并写出十六进制文本
     * @param button 颜色按钮
     * @param color 当前颜色
     * @details 底色由自绘按钮的色板档给出，文字按底色亮度换反差色：浅底上白字与
     *          深底上黑字都读不出来，颜色本身不该把标签吞掉。标签由按钮直接落笔，
     *          主题样式表的 color 规则盖不掉它。
     */
    void applyColorSwatch(GyanisScribe::Gui::Button *button, const QColor &color)
    {
        button->setSwatchColor(color);
        button->setText(color.name(QColor::HexRgb).toUpper());
    }

    /**
     * @brief 造一根样式滑杆
     * @param parent 归属控件
     * @param objectName 对象名
     * @param accessibleName 可访问名称
     * @param toolTip 中文提示
     * @param minimum 下限
     * @param maximum 上限
     * @param value 初始值
     * @return 配好范围的滑杆
     */
    QSlider *makeStyleSlider(QWidget *parent, const QString &objectName, const QString &accessibleName, const QString &toolTip, const int minimum, const int maximum,
                             const int value)
    {
        auto *slider = new GyanisScribe::Gui::Slider{Qt::Horizontal, parent};
        slider->setObjectName(objectName);
        slider->setAccessibleName(accessibleName);
        slider->setToolTip(toolTip);
        slider->setRange(minimum, maximum);
        slider->setValue(value);
        return slider;
    }

    /**
     * @brief 把预览图按标签当前大小等比缩上去
     * @param image 最近的预览图
     * @param label 预览标签
     */
    void showPreviewPixmap(const QImage &image, QLabel &label)
    {
        if (image.isNull())
        {
            return;
        }
        label.setPixmap(QPixmap::fromImage(image).scaled(label.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    /**
     * @brief 取路径后缀的小写形式，不带前导的点
     * @param filePath 文件路径
     * @return 小写后缀；没有后缀时为空串
     */
    QString imageSuffixOf(const std::filesystem::path &filePath)
    {
        auto suffix = QString::fromStdWString(filePath.extension().wstring()).toLower();
        if (suffix.startsWith(QChar{u'.'}))
        {
            suffix.remove(0, 1);
        }
        return suffix;
    }

    /**
     * @brief 判断后缀是不是可写的图片格式
     * @param suffix 不带点的小写后缀
     * @return png、jpg、jpeg 或 webp 时返回 true
     */
    bool isSupportedImageSuffix(const QString &suffix)
    {
        return suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg") || suffix == QStringLiteral("webp");
    }
} // namespace

namespace GyanisScribe::Gui
{

    ShareDialog::ShareDialog(QImage image, Core::CaptureSourceInfo source, Core::ISettingsStore &settings, QWidget *parent) :
        QDialog(parent), m_image(std::move(image)), m_source(std::move(source)), m_settings(settings)
    {
        setWindowTitle(uiText("美化分享"));
        setWindowModality(Qt::WindowModal);
        // 顶层窗口自己铺底：样式表只给具名的根上底色
        setObjectName(QStringLiteral("appRoot"));
        setAttribute(Qt::WA_StyledBackground, true);

        buildUi();
        loadTemplates();
        resize(960, 620);
        selectTemplate(persistedLastTemplateId());
    }

    const Core::ShareStyle &ShareDialog::currentStyle() const noexcept
    {
        return m_style;
    }

    QString ShareDialog::selectedTemplateId() const
    {
        return m_templateCombo->currentData().toString();
    }

    QImage ShareDialog::previewImageForTesting() const
    {
        return m_previewImage;
    }

    const Core::CaptureSourceInfo &ShareDialog::sourceForTesting() const noexcept
    {
        return m_source;
    }

    QString ShareDialog::statusText() const
    {
        return m_status;
    }

    Base::Expected<void> ShareDialog::copyToClipboard()
    {
        const auto rendered = Core::renderShareImage(m_image, m_style, 0);
        if (!rendered)
        {
            setStatus(uiText("没能复制到剪贴板：") + QString::fromStdString(rendered.error().message), true);
            return std::unexpected(rendered.error());
        }
        QApplication::clipboard()->setImage(*rendered);
        setStatus(uiText("已复制到剪贴板"), false);
        return {};
    }

    Base::Expected<void> ShareDialog::saveToFile(const std::filesystem::path &filePath)
    {
        const auto fail = [this](const Base::Error &reason)
        {
            const auto error = Base::Error{reason.code, uiFormat(uiTextUtf8("没能保存美化图片：{}"), reason.message)};
            Base::Logger::error(error.message);
            setStatus(QString::fromStdString(error.message), true);
            return std::unexpected(error);
        };

        if (m_image.isNull())
        {
            return fail(Base::Error{Base::ErrorCode::InvalidArgument, uiTextUtf8("没有可保存的图片")});
        }

        auto suffix = imageSuffixOf(filePath);
        if (!isSupportedImageSuffix(suffix))
        {
            return fail(Base::Error{Base::ErrorCode::InvalidArgument,
                                    uiFormat(uiTextUtf8("不支持的图片格式：{}"), suffix.isEmpty() ? uiTextUtf8("（无后缀）") : suffix.toStdString())});
        }

        const auto rendered = Core::renderShareImage(m_image, m_style, 0);
        if (!rendered)
        {
            return fail(rendered.error());
        }

        const auto pathText     = Base::Paths::toDisplayText(filePath);
        const auto writeFailure = Base::Error{Base::ErrorCode::FileWriteFailed, uiFormat(uiTextUtf8("「{}」写不进去。请检查这个目录能不能写"), pathText.toStdString())};

        QSaveFile file{pathText};
        if (!file.open(QIODevice::WriteOnly))
        {
            return fail(writeFailure);
        }

        QByteArray bytes;
        QBuffer    buffer{&bytes};
        buffer.open(QIODevice::WriteOnly);
        // 按后缀显式指定编码器：.jpeg 是常见写法，而 Qt 的 JPEG 编码器名只有 jpg
        const auto format = suffix == QStringLiteral("jpeg") ? QByteArray{"jpg"} : suffix.toUtf8();
        if (!rendered->save(&buffer, format.constData(), Core::kDefaultImageQuality))
        {
            buffer.close();
            file.cancelWriting();
            return fail(Base::Error{Base::ErrorCode::FileWriteFailed, uiFormat(uiTextUtf8("「{}」没能编码成目标格式"), pathText.toStdString())});
        }
        buffer.close();

        if (file.write(bytes) != bytes.size())
        {
            file.cancelWriting();
            return fail(writeFailure);
        }
        if (!file.commit())
        {
            return fail(writeFailure);
        }

        setStatus(QString::fromStdString(uiFormat(uiTextUtf8("已保存到 {}"), pathText.toStdString())), false);
        return {};
    }

    std::filesystem::path ShareDialog::savePathForDialog(const std::filesystem::path &chosen)
    {
        if (isSupportedImageSuffix(imageSuffixOf(chosen)))
        {
            return chosen;
        }
        auto corrected = chosen;
        corrected += ".png";
        return corrected;
    }

    QString ShareDialog::suggestedFilePath() const
    {
        const auto configured = m_settings.stringValue(Core::kPathScreenshot, QString{});
        const auto directory  = configured.isEmpty() ? Base::Paths::defaultScreenshotDirectory() : Base::Paths::fromDisplayText(configured);
        const auto context    = Base::NameTemplateContext{QFileInfo{m_source.processName}.completeBaseName(), m_source.windowTitle, m_source.screenName};
        const auto name       = Base::Paths::imageFileName(QDateTime::currentDateTime(), "png", Core::nameTemplateOrDefault(m_settings), context);
        return Base::Paths::toDisplayText(Base::Paths::uniqueFilePath(directory, name));
    }

    Base::Expected<void> ShareDialog::saveTemplateAs(const QString &name)
    {
        const auto added = Core::addCustomShareTemplate(m_custom, name, m_style);
        if (!added)
        {
            setStatus(QString::fromStdString(added.error().message), true);
            return std::unexpected(added.error());
        }

        if (const auto written = m_settings.setValue(Core::kShareTemplates, Core::serializeShareTemplates(*added)); !written)
        {
            const auto error = Base::Error{Base::ErrorCode::FileWriteFailed, uiFormat(uiTextUtf8("模板保存失败：{}"), written.error().message)};
            setStatus(QString::fromStdString(error.message), true);
            return std::unexpected(error);
        }

        m_custom           = *added;
        const auto newId   = m_custom.back().id;
        const auto newName = m_custom.back().name;
        loadTemplates();
        selectTemplate(newId);
        setStatus(QString::fromStdString(uiFormat(uiTextUtf8("已保存模板「{}」"), newName.toStdString())), false);
        return {};
    }

    Base::Expected<void> ShareDialog::deleteSelectedTemplate()
    {
        const auto id = selectedTemplateId();
        if (Core::isBuiltInShareTemplateId(id))
        {
            const auto error = Base::Error{Base::ErrorCode::InvalidArgument, uiTextUtf8("内置模板不能删除")};
            setStatus(QString::fromStdString(error.message), true);
            return std::unexpected(error);
        }

        QString name;
        for (const auto &item: m_custom)
        {
            if (item.id == id)
            {
                name = item.name;
                break;
            }
        }

        const auto removed = Core::removeCustomShareTemplate(m_custom, id);
        if (!removed)
        {
            setStatus(QString::fromStdString(removed.error().message), true);
            return std::unexpected(removed.error());
        }

        if (const auto written = m_settings.setValue(Core::kShareTemplates, Core::serializeShareTemplates(*removed)); !written)
        {
            const auto error = Base::Error{Base::ErrorCode::FileWriteFailed, uiFormat(uiTextUtf8("模板删除失败：{}"), written.error().message)};
            setStatus(QString::fromStdString(error.message), true);
            return std::unexpected(error);
        }

        m_custom = *removed;
        loadTemplates();
        selectTemplate(QStringLiteral("light"));
        setStatus(QString::fromStdString(uiFormat(uiTextUtf8("已删除模板「{}」"), name.toStdString())), false);
        return {};
    }

    void ShareDialog::flushPreviewForTesting()
    {
        m_previewTimer->stop();
        renderPreview();
    }

    void ShareDialog::resizeEvent(QResizeEvent *event)
    {
        QDialog::resizeEvent(event);
        showPreviewPixmap(m_previewImage, *m_preview);
    }

    bool ShareDialog::eventFilter(QObject *watched, QEvent *event)
    {
        if (watched == m_preview && event->type() == QEvent::Resize)
        {
            showPreviewPixmap(m_previewImage, *m_preview);
        }
        return QDialog::eventFilter(watched, event);
    }

    void ShareDialog::buildUi()
    {
        auto *root = new QHBoxLayout{this};
        root->setContentsMargins(kSpacingLarge, kSpacingLarge, kSpacingLarge, kSpacingLarge);
        root->setSpacing(kSpacingLarge);

        m_preview = new QLabel{this};
        m_preview->setObjectName(QStringLiteral("sharePreview"));
        m_preview->setMinimumSize(320, 240);
        m_preview->setScaledContents(false);
        m_preview->setAlignment(Qt::AlignCenter);
        // 缩放后的图会反过来影响标签的建议尺寸：忽略它，版面大小只由左栏剩余的宽度决定
        m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_preview->setAccessibleName(uiText("美化预览"));
        m_preview->setToolTip(uiText("这里显示套上当前样式之后的样子；复制与保存出去的仍是原尺寸"));
        m_preview->installEventFilter(this);
        root->addWidget(m_preview, 1);

        auto *scroll = new QScrollArea{this};
        scroll->setObjectName(QStringLiteral("shareScroll"));
        scroll->setAccessibleName(uiText("美化设置"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBar(new ScrollBar{Qt::Vertical, scroll});
        scroll->setHorizontalScrollBar(new ScrollBar{Qt::Horizontal, scroll});
        scroll->setMinimumWidth(360);
        root->addWidget(scroll);

        auto *formWidget = new QWidget;
        auto *form       = new QFormLayout{formWidget};
        form->setContentsMargins(kSpacingMedium, kSpacingMedium, kSpacingMedium, kSpacingMedium);
        form->setVerticalSpacing(kSpacingSmall);
        form->setHorizontalSpacing(kSpacingSmall);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        auto *intro = new QLabel{uiText("左侧是实时预览；下面的改动只影响这张美化成品，原图与标注现场保持不变。"), formWidget};
        intro->setObjectName(QStringLiteral("pageHint"));
        intro->setWordWrap(true);
        intro->setAccessibleName(uiText("美化分享说明"));
        applyTextRole(*intro, TextRole::Body);
        form->addRow(intro);

        form->addRow(makeSectionLabel(formWidget, uiText("模板")));
        m_templateCombo = new ComboBox{formWidget};
        m_templateCombo->setObjectName(QStringLiteral("shareTemplateCombo"));
        m_templateCombo->setAccessibleName(uiText("模板"));
        m_templateCombo->setToolTip(uiText("选一套内置模板或保存过的自定义模板；换模板会把这套样式加载到下面的控件里"));
        form->addRow(uiText("当前模板"), m_templateCombo);

        form->addRow(makeSectionLabel(formWidget, uiText("背景")));
        m_backgroundButton = new Button{formWidget};
        m_backgroundButton->setObjectName(QStringLiteral("shareBackgroundColor"));
        m_backgroundButton->setAccessibleName(uiText("背景色"));
        m_backgroundButton->setToolTip(uiText("卡片背后的底色；点一下打开取色器"));
        m_backgroundButton->setMinimumHeight(28);
        form->addRow(uiText("背景色"), m_backgroundButton);

        m_secondColorButton = new Button{formWidget};
        m_secondColorButton->setObjectName(QStringLiteral("shareBackgroundSecondColor"));
        m_secondColorButton->setAccessibleName(uiText("渐变终点"));
        m_secondColorButton->setToolTip(uiText("渐变终点的底色；打开「从上到下渐变」后才用得上"));
        m_secondColorButton->setMinimumHeight(28);
        form->addRow(uiText("渐变终点"), m_secondColorButton);

        m_gradient = new CheckBox{formWidget};
        m_gradient->setObjectName(QStringLiteral("shareGradient"));
        m_gradient->setText(uiText("从上到下渐变"));
        m_gradient->setAccessibleName(uiText("渐变背景"));
        m_gradient->setToolTip(uiText("打开后背景从背景色渐变到渐变终点；关着时只用背景色"));
        form->addRow(m_gradient);

        form->addRow(makeSectionLabel(formWidget, uiText("尺寸")));
        m_padding = makeStyleSlider(formWidget, QStringLiteral("sharePadding"), uiText("内边距"), uiText("卡片四周留出多少像素的空白；越大画布越宽，成品四周越透气"),
                                    Core::kMinimumSharePadding, Core::kMaximumSharePadding, Core::ShareStyle{}.paddingPixels);
        form->addRow(uiText("内边距"), m_padding);

        m_radius = makeStyleSlider(formWidget, QStringLiteral("shareRadius"), uiText("圆角半径"), uiText("卡片四角的圆角半径，像素；特别大时会按卡片短边自动收小"),
                                   Core::kMinimumShareCornerRadius, Core::kMaximumShareCornerRadius, Core::ShareStyle{}.cornerRadiusPixels);
        form->addRow(uiText("圆角半径"), m_radius);

        m_borderWidth = makeStyleSlider(formWidget, QStringLiteral("shareBorderWidth"), uiText("描边宽度"), uiText("卡片外圈描边的宽度，像素；0 表示不画描边"),
                                        Core::kMinimumShareBorderPixels, Core::kMaximumShareBorderPixels, Core::ShareStyle{}.borderPixels);
        form->addRow(uiText("描边宽度"), m_borderWidth);

        m_shadowRadius = makeStyleSlider(formWidget, QStringLiteral("shareShadowRadius"), uiText("投影宽度"), uiText("卡片外圈投影向外铺开的宽度，像素；0 表示不画投影"),
                                         Core::kMinimumShareShadowPixels, Core::kMaximumShareShadowPixels, Core::ShareStyle{}.shadowPixels);
        form->addRow(uiText("投影宽度"), m_shadowRadius);

        form->addRow(makeSectionLabel(formWidget, uiText("代码窗口")));
        m_editorChrome = new CheckBox{formWidget};
        m_editorChrome->setObjectName(QStringLiteral("shareEditorChrome"));
        m_editorChrome->setText(uiText("在卡片顶部显示标题栏"));
        m_editorChrome->setAccessibleName(uiText("代码窗口标题栏"));
        m_editorChrome->setToolTip(uiText("打开后在卡片顶部画一条编辑器标题栏：三个圆点加标题文字"));
        form->addRow(m_editorChrome);

        m_editorTitle = new LineEdit{formWidget};
        m_editorTitle->setObjectName(QStringLiteral("shareEditorTitle"));
        m_editorTitle->setAccessibleName(uiText("标题栏文字"));
        m_editorTitle->setToolTip(uiText("标题栏里的文字；留空只画三个圆点"));
        m_editorTitle->setPlaceholderText(uiText("留空只画三个圆点"));
        m_editorTitle->setMaxLength(Core::kMaximumShareEditorTitleLength);
        form->addRow(uiText("标题文字"), m_editorTitle);

        form->addRow(makeSectionLabel(formWidget, uiText("水印")));
        m_watermarkText = new LineEdit{formWidget};
        m_watermarkText->setObjectName(QStringLiteral("shareWatermarkText"));
        m_watermarkText->setAccessibleName(uiText("水印文字"));
        m_watermarkText->setToolTip(uiText("贴在卡片下角的文字；留空不画水印"));
        m_watermarkText->setPlaceholderText(uiText("留空不加水印"));
        m_watermarkText->setMaxLength(Core::kMaximumShareWatermarkLength);
        form->addRow(uiText("水印文字"), m_watermarkText);

        m_watermarkOpacity = makeStyleSlider(formWidget, QStringLiteral("shareWatermarkOpacity"), uiText("水印不透明度"), uiText("水印的不透明度：0 完全透明，255 完全不透明"),
                                             Core::kMinimumShareWatermarkOpacity, Core::kMaximumShareWatermarkOpacity, Core::ShareStyle{}.watermarkOpacity);
        form->addRow(uiText("不透明度"), m_watermarkOpacity);

        m_watermarkCorner = new ComboBox{formWidget};
        m_watermarkCorner->setObjectName(QStringLiteral("shareWatermarkCorner"));
        m_watermarkCorner->setAccessibleName(uiText("水印位置"));
        m_watermarkCorner->setToolTip(uiText("水印贴在卡片的右下角还是左下角"));
        m_watermarkCorner->addItem(uiText("右下角"));
        m_watermarkCorner->addItem(uiText("左下角"));
        form->addRow(uiText("水印位置"), m_watermarkCorner);

        form->addRow(makeSectionLabel(formWidget, uiText("模板操作")));
        auto *templateRow    = new QWidget{formWidget};
        auto *templateLayout = new QHBoxLayout{templateRow};
        templateLayout->setContentsMargins(0, 0, 0, 0);
        templateLayout->setSpacing(kSpacingSmall);
        m_saveTemplate = new Button{uiText("另存为模板"), templateRow};
        m_saveTemplate->setObjectName(QStringLiteral("shareSaveTemplate"));
        m_saveTemplate->setAccessibleName(uiText("另存为模板"));
        m_saveTemplate->setToolTip(uiText("把当前样式存成一套自定义模板，以后在同一台机器上还能选回来"));
        m_deleteTemplate = new Button{uiText("删除模板"), templateRow};
        m_deleteTemplate->setObjectName(QStringLiteral("shareDeleteTemplate"));
        m_deleteTemplate->setAccessibleName(uiText("删除模板"));
        m_deleteTemplate->setToolTip(uiText("删除当前选中的自定义模板；内置模板不能删"));
        templateLayout->addWidget(m_saveTemplate);
        templateLayout->addWidget(m_deleteTemplate);
        templateLayout->addStretch(1);
        form->addRow(templateRow);

        auto *exportRow    = new QWidget{formWidget};
        auto *exportLayout = new QHBoxLayout{exportRow};
        exportLayout->setContentsMargins(0, 0, 0, 0);
        exportLayout->setSpacing(kSpacingSmall);
        m_copyButton = new Button{uiText("复制到剪贴板"), exportRow};
        m_copyButton->setObjectName(QStringLiteral("shareCopy"));
        m_copyButton->setAccessibleName(uiText("复制到剪贴板"));
        m_copyButton->setToolTip(uiText("按原尺寸把美化成品放进剪贴板；原图与标注保持不动"));
        m_saveFileButton = new Button{uiText("保存图片…"), exportRow};
        m_saveFileButton->setObjectName(QStringLiteral("shareSaveFile"));
        m_saveFileButton->setAccessibleName(uiText("保存图片"));
        m_saveFileButton->setToolTip(uiText("按原尺寸把美化成品另存成 PNG、JPG 或 WebP；原图与标注保持不动"));
        m_closeButton = new Button{uiText("关闭"), exportRow};
        m_closeButton->setObjectName(QStringLiteral("shareClose"));
        m_closeButton->setAccessibleName(uiText("关闭美化分享"));
        m_closeButton->setToolTip(uiText("关掉美化对话框回到标注现场；已经复制或保存的成品不受影响"));
        exportLayout->addWidget(m_copyButton);
        exportLayout->addWidget(m_saveFileButton);
        exportLayout->addWidget(m_closeButton);
        exportLayout->addStretch(1);
        form->addRow(exportRow);

        m_statusLabel = new QLabel{formWidget};
        m_statusLabel->setObjectName(QStringLiteral("shareStatus"));
        m_statusLabel->setWordWrap(true);
        m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_statusLabel->setAccessibleName(uiText("操作状态"));
        m_statusLabel->setToolTip(uiText("这里显示复制、保存与模板操作的结果和失败原因"));
        form->addRow(m_statusLabel);

        scroll->setWidget(formWidget);

        m_previewTimer = new QTimer{this};
        m_previewTimer->setSingleShot(true);
        m_previewTimer->setInterval(50);
        QObject::connect(m_previewTimer, &QTimer::timeout, this, &ShareDialog::renderPreview);

        QObject::connect(m_templateCombo, &QComboBox::currentIndexChanged, this, [this](int) { selectTemplate(m_templateCombo->currentData().toString()); });

        QObject::connect(m_gradient, &QCheckBox::toggled, this,
                         [this]
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });
        QObject::connect(m_padding, &QSlider::valueChanged, this,
                         [this](int)
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });
        QObject::connect(m_radius, &QSlider::valueChanged, this,
                         [this](int)
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });
        QObject::connect(m_borderWidth, &QSlider::valueChanged, this,
                         [this](int)
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });
        QObject::connect(m_shadowRadius, &QSlider::valueChanged, this,
                         [this](int)
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });
        QObject::connect(m_editorChrome, &QCheckBox::toggled, this,
                         [this]
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });
        QObject::connect(m_editorTitle, &QLineEdit::textChanged, this,
                         [this](const QString &)
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });
        QObject::connect(m_watermarkText, &QLineEdit::textChanged, this,
                         [this](const QString &)
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });
        QObject::connect(m_watermarkOpacity, &QSlider::valueChanged, this,
                         [this](int)
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });
        QObject::connect(m_watermarkCorner, &QComboBox::currentIndexChanged, this,
                         [this](int)
                         {
                             applyControlsToStyle();
                             schedulePreview();
                         });

        QObject::connect(m_backgroundButton, &QPushButton::clicked, this,
                         [this]
                         {
                             const auto picked = QColorDialog::getColor(m_style.backgroundTop, this, uiText("选择背景色"));
                             if (!picked.isValid())
                             {
                                 return;
                             }
                             m_style.backgroundTop = QColor{picked.red(), picked.green(), picked.blue()};
                             applyColorSwatch(m_backgroundButton, m_style.backgroundTop);
                             schedulePreview();
                         });
        QObject::connect(m_secondColorButton, &QPushButton::clicked, this,
                         [this]
                         {
                             const auto picked = QColorDialog::getColor(m_style.backgroundBottom, this, uiText("选择渐变终点色"));
                             if (!picked.isValid())
                             {
                                 return;
                             }
                             m_style.backgroundBottom = QColor{picked.red(), picked.green(), picked.blue()};
                             applyColorSwatch(m_secondColorButton, m_style.backgroundBottom);
                             schedulePreview();
                         });

        QObject::connect(m_saveTemplate, &QPushButton::clicked, this,
                         [this]
                         {
                             bool       accepted = false;
                             const auto name     = QInputDialog::getText(this, uiText("另存为模板"), uiText("模板名称"), QLineEdit::Normal, QString{}, &accepted);
                             if (!accepted)
                             {
                                 return;
                             }
                             // 空名交给 saveTemplateAs 就地报「模板名不能为空」：按钮这一路也要给用户一个原因
                             (void) saveTemplateAs(name);
                         });
        QObject::connect(m_deleteTemplate, &QPushButton::clicked, this,
                         [this]
                         {
                             const auto answer = QMessageBox::question(this, uiText("删除模板"), uiText("确定删除当前选中的自定义模板吗？删除后不能恢复。"));
                             if (answer != QMessageBox::Yes)
                             {
                                 return;
                             }
                             (void) deleteSelectedTemplate();
                         });

        QObject::connect(m_copyButton, &QPushButton::clicked, this, [this] { (void) copyToClipboard(); });
        QObject::connect(m_saveFileButton, &QPushButton::clicked, this,
                         [this]
                         {
                             const auto chosen = QFileDialog::getSaveFileName(this, uiText("保存图片"), suggestedFilePath(),
                                                                              uiText("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;WebP 图片 (*.webp)"));
                             if (chosen.isEmpty())
                             {
                                 return;
                             }
                             (void) saveToFile(savePathForDialog(std::filesystem::path{Base::Paths::fromDisplayText(chosen)}));
                         });
        QObject::connect(m_closeButton, &QPushButton::clicked, this, &ShareDialog::accept);
    }

    void ShareDialog::loadTemplates()
    {
        m_custom = Core::parseShareTemplates(m_settings.stringValue(Core::kShareTemplates, QString{}));

        const QSignalBlocker blocker{m_templateCombo};
        m_templateCombo->clear();
        for (const auto &item: Core::builtInShareTemplates())
        {
            m_templateCombo->addItem(item.name, item.id);
        }
        for (const auto &item: m_custom)
        {
            m_templateCombo->addItem(item.name, item.id);
        }
    }

    void ShareDialog::applyStyleToControls()
    {
        {
            const QSignalBlocker blocker{m_gradient};
            m_gradient->setChecked(m_style.gradient);
        }
        applyColorSwatch(m_backgroundButton, m_style.backgroundTop);
        applyColorSwatch(m_secondColorButton, m_style.backgroundBottom);
        m_secondColorButton->setEnabled(m_style.gradient);

        {
            const QSignalBlocker blocker{m_padding};
            m_padding->setValue(m_style.paddingPixels);
        }
        {
            const QSignalBlocker blocker{m_radius};
            m_radius->setValue(m_style.cornerRadiusPixels);
        }
        {
            // 描边宽度滑杆兼作开关：关着描边的模板把滑杆落到 0，改别的控件才不会把描边顺手打开
            const QSignalBlocker blocker{m_borderWidth};
            m_borderWidth->setValue(m_style.border ? m_style.borderPixels : 0);
        }
        {
            const QSignalBlocker blocker{m_shadowRadius};
            m_shadowRadius->setValue(m_style.shadow ? m_style.shadowPixels : 0);
        }
        {
            const QSignalBlocker blocker{m_editorChrome};
            m_editorChrome->setChecked(m_style.editorChrome);
        }
        {
            const QSignalBlocker blocker{m_editorTitle};
            m_editorTitle->setText(m_style.editorTitle);
        }
        m_editorTitle->setEnabled(m_style.editorChrome);
        {
            const QSignalBlocker blocker{m_watermarkText};
            m_watermarkText->setText(m_style.watermarkText);
        }
        {
            const QSignalBlocker blocker{m_watermarkOpacity};
            m_watermarkOpacity->setValue(m_style.watermarkOpacity);
        }
        {
            const QSignalBlocker blocker{m_watermarkCorner};
            m_watermarkCorner->setCurrentIndex(m_style.watermarkCorner == ShareWatermarkCorner::BottomLeft ? 1 : 0);
        }
    }

    void ShareDialog::applyControlsToStyle()
    {
        m_style.gradient           = m_gradient->isChecked();
        m_style.paddingPixels      = m_padding->value();
        m_style.cornerRadiusPixels = m_radius->value();
        m_style.borderPixels       = m_borderWidth->value();
        m_style.border             = m_style.borderPixels > 0;
        m_style.shadowPixels       = m_shadowRadius->value();
        m_style.shadow             = m_style.shadowPixels > 0;
        m_style.editorChrome       = m_editorChrome->isChecked();
        m_style.editorTitle        = m_editorTitle->text();
        m_style.watermarkText      = m_watermarkText->text();
        m_style.watermarkOpacity   = m_watermarkOpacity->value();
        m_style.watermarkCorner    = m_watermarkCorner->currentIndex() == 1 ? ShareWatermarkCorner::BottomLeft : ShareWatermarkCorner::BottomRight;

        m_secondColorButton->setEnabled(m_style.gradient);
        m_editorTitle->setEnabled(m_style.editorChrome);
    }

    void ShareDialog::schedulePreview()
    {
        m_previewTimer->start();
    }

    void ShareDialog::renderPreview()
    {
        const auto rendered = Core::renderShareImage(m_image, m_style, 1200);
        if (!rendered)
        {
            setStatus(QString::fromStdString(rendered.error().message), true);
            return;
        }
        m_previewImage = *rendered;
        showPreviewPixmap(m_previewImage, *m_preview);
    }

    void ShareDialog::updateTemplateActions()
    {
        const auto id = selectedTemplateId();
        m_deleteTemplate->setEnabled(!id.isEmpty() && !Core::isBuiltInShareTemplateId(id));
    }

    void ShareDialog::selectTemplate(const QString &id)
    {
        const auto builtIns = Core::builtInShareTemplates();

        const Core::ShareTemplate *selected = nullptr;
        for (const auto &item: builtIns)
        {
            if (item.id == id)
            {
                selected = &item;
                break;
            }
        }
        if (selected == nullptr)
        {
            for (const auto &item: m_custom)
            {
                if (item.id == id)
                {
                    selected = &item;
                    break;
                }
            }
        }
        if (selected == nullptr)
        {
            Base::Logger::warning(uiFormat(uiTextUtf8("美化模板「{}」不在目录里，回到 light"), id.toStdString()));
            if (id != QStringLiteral("light"))
            {
                selectTemplate(QStringLiteral("light"));
                return;
            }
            // light 是内置目录的第一套；走到这里说明目录本身坏了，仍然拿它兜底
            selected = &builtIns.front();
        }

        {
            const QSignalBlocker blocker{m_templateCombo};
            m_templateCombo->setCurrentIndex(m_templateCombo->findData(selected->id));
        }
        m_style = selected->style;
        applyStyleToControls();
        updateTemplateActions();

        if (const auto written = m_settings.setValue(Core::kShareLastTemplate, selected->id); !written)
        {
            Base::Logger::warning(uiFormat(uiTextUtf8("上次使用的美化模板没能写入：{}"), written.error().message));
            setStatus(QString::fromStdString(uiFormat(uiTextUtf8("模板选择没能记住：{}"), written.error().message)), true);
        }
        renderPreview();
    }

    QString ShareDialog::persistedLastTemplateId() const
    {
        const auto stored = m_settings.stringValue(Core::kShareLastTemplate, QString{});
        if (!stored.isEmpty())
        {
            if (Core::isBuiltInShareTemplateId(stored))
            {
                return stored;
            }
            for (const auto &item: m_custom)
            {
                if (item.id == stored)
                {
                    return stored;
                }
            }
        }
        return QStringLiteral("light");
    }

    void ShareDialog::setStatus(const QString &text, const bool error)
    {
        m_status = text;
        m_statusLabel->setText(text);
        m_statusLabel->setAccessibleName(error ? uiText("操作失败原因") : uiText("操作状态"));
    }

} // namespace GyanisScribe::Gui
