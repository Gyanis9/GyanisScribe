#include "Ocr/OcrResultWindow.h"

#include "Core/Image/ImageFit.h"
#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Ocr/OcrEngineFactory.h"
#include "Ocr/OcrTextExporter.h"
#include "Ui/IconProvider.h"
#include "Ui/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <string_view>
#include <cmath>
#include <format>

namespace
{
    using GyanisScribe::Ocr::OcrResult;

    /// 预览区的建议起始尺寸，逻辑像素：再小就连行框都看不清了
    constexpr int kPreviewMinimumWidth  = 320;
    constexpr int kPreviewMinimumHeight = 240;

    /// 高亮框的线宽，逻辑像素
    constexpr int kHighlightPenWidth = 2;

    /// 置信度从小数换成百分数的倍率
    constexpr double kConfidencePercentScale = 100.0;

    /**
     * @brief 左侧原图预览：等比摆图，再把当前行的方框圈出来
     * @details 它不认识识别结果，只接一张图与一个方框；方框由外层用 Core 的换算算好递进来，
     *          摆图与画框因此必然同源，不会点着第 3 行圈住第 2 行。
     */
    class Preview : public QWidget
    {
    public:
        Preview()
        {
            setMinimumSize(kPreviewMinimumWidth, kPreviewMinimumHeight);
        }

        /// 要画的图；传空图就等于什么都不画
        void setImage(const QImage &image)
        {
            m_image = image;
            update();
        }

        /// 当前高亮方框（预览控件坐标）与颜色；空方框表示不圈任何行
        void setHighlight(const std::optional<QRect> &highlight, const QColor &color)
        {
            m_highlight = highlight;
            m_color     = color;
            update();
        }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter{this};
            if (m_image.isNull())
            {
                return;
            }

            const auto display = GyanisScribe::Core::displayRectFor(size(), m_image.size());
            if (display.isEmpty())
            {
                return;
            }

            // 底图先画，行框后画：框是叠加在图上的说明，不该被图盖住
            painter.drawImage(display, m_image);
            if (!m_highlight)
            {
                return;
            }

            QPen pen{m_color};
            pen.setWidth(kHighlightPenWidth);
            painter.setPen(pen);
            painter.drawRect(*m_highlight);
        }

    private:
        QImage               m_image;            ///< 原图
        std::optional<QRect> m_highlight;        ///< 当前行的方框
        QColor               m_color{Qt::black}; ///< 方框颜色，由外层按主题给
    };

    /**
     * @brief 造一个带图标、tooltip 与无障碍名的工具按钮
     * @param icons 图标供给器
     * @param iconName 图标名，必须真的在资源里
     * @param label 中文动作名，同时用作 tooltip 与无障碍名
     * @return 归调用方所有的按钮
     */
    QToolButton *makeToolButton(GyanisScribe::Ui::IconProvider &icons, std::string_view iconName, const QString &label)
    {
        auto *button = new QToolButton;
        button->setIcon(icons.icon(iconName));
        button->setToolTip(label);
        button->setAccessibleName(label);
        button->setAutoRaise(true);
        return button;
    }

    /// 导出对话框的默认文件名：与成品图同一套名字规则，只在末尾换扩展名
    std::filesystem::path suggestedExportPath(const QString &extension, const QString &nameTemplate)
    {
        return GyanisScribe::Base::Paths::defaultScreenshotDirectory() /
               GyanisScribe::Base::Paths::fromDisplayText(GyanisScribe::Base::Paths::imageFileName(QDateTime::currentDateTime(), extension.toStdString(), nameTemplate));
    }
} // namespace

namespace GyanisScribe::Ocr
{
    int lineIndexForBlock(const OcrResult &result, const int blockIndex)
    {
        // 块序号可能多于结果行数（行文本本身带换行时就会多出一块），越界一律不圈
        if (blockIndex < 0 || blockIndex >= static_cast<int>(result.lines.size()))
        {
            return -1;
        }
        return blockIndex;
    }

    std::optional<QRect> highlightRectFor(const OcrResult &result, const int lineIndex, const QSize &imageSize, const QSize &previewSize)
    {
        if (lineIndex < 0 || lineIndex >= static_cast<int>(result.lines.size()))
        {
            return {};
        }

        const auto lineRect = result.lines[lineIndex].boundingBox;
        if (lineRect.isEmpty())
        {
            return {};
        }

        const auto display = Core::displayRectFor(previewSize, imageSize);
        if (display.isEmpty())
        {
            return {};
        }
        return Core::imageRectToDisplay(lineRect, imageSize, display);
    }

    QString statusLineFor(const OcrResult &result, const QSize &imageSize)
    {
        if (result.isEmpty())
        {
            // 空结果不是失败：把「这一张里没字」与「认不出」分开说，用户才知道下一步该换图还是换语言
            return QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("这一张图里没认出文字。可以换一种语言再认一次，或重新截一张更清晰的图")).c_str());
        }

        // 置信度按后端给不给得出分两种写法：给不出这一路读数就写「未提供」，
        // 打一个 0% 会被读成「这张图认得很差」，可它说的其实是「这个后端不报这个数」
        const std::string confidenceReadout = result.averageConfidence ? std::format("{}%", static_cast<int>(std::lround(*result.averageConfidence * kConfidencePercentScale)))
                                                                       : GyanisScribe::Base::uiTextUtf8("未提供");
        return QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("{} 行 · 平均置信度 {} · 图像 {}×{} · 语言 {}"), result.lines.size(),
                                                              confidenceReadout, imageSize.width(), imageSize.height(), result.language.toStdString())
                                         .c_str());
    }

    OcrResultWindow::OcrResultWindow(const QImage &image, const OcrResult &result, Ui::IconProvider &icons, const Ui::ThemeManager &theme, const QString &nameTemplate,
                                     QWidget *parent) : QWidget(parent), m_image(image), m_result(result), m_icons(icons), m_theme(theme), m_nameTemplate(nameTemplate)
    {
        setWindowTitle(GyanisScribe::Base::uiText("识别结果"));
        setWindowIcon(m_icons.icon("Ocr"));
        // 顶层窗口自己铺底：样式表只给具名的根上底色
        setObjectName(QStringLiteral("appRoot"));
        setAttribute(Qt::WA_StyledBackground, true);

        auto *root = new QHBoxLayout{this};
        root->setContentsMargins(GyanisScribe::Ui::kSpacingLarge, GyanisScribe::Ui::kSpacingLarge, GyanisScribe::Ui::kSpacingLarge, GyanisScribe::Ui::kSpacingLarge);
        root->setSpacing(GyanisScribe::Ui::kSpacingMedium);
        root->addWidget(m_preview = new Preview, 3);

        auto *right = new QVBoxLayout;
        root->addLayout(right, 2);

        m_text = new QTextEdit;
        m_text->setReadOnly(true);
        m_text->setAccessibleName(GyanisScribe::Base::uiText("识别文本"));
        right->addWidget(m_text);

        m_status = new QLabel{statusLineFor(m_result, m_image.size()), this};
        m_status->setAccessibleName(GyanisScribe::Base::uiText("识别摘要"));
        right->addWidget(m_status);

        auto *buttons = new QHBoxLayout;
        right->addLayout(buttons);

        auto *copyAll     = makeToolButton(m_icons, "Copy", GyanisScribe::Base::uiText("复制全部"));
        auto *copyPart    = makeToolButton(m_icons, "OcrCopy", GyanisScribe::Base::uiText("复制选中"));
        auto *exportTxt   = makeToolButton(m_icons, "OcrExport", GyanisScribe::Base::uiText("导出 TXT"));
        auto *exportJson  = makeToolButton(m_icons, "Save", GyanisScribe::Base::uiText("导出 JSON"));
        auto *rerun       = makeToolButton(m_icons, "Refresh", GyanisScribe::Base::uiText("重新识别"));
        auto *closeButton = makeToolButton(m_icons, "Close", GyanisScribe::Base::uiText("关闭"));
        m_language        = new QComboBox;
        m_language->addItems(supportedOcrLanguageTags());
        m_language->setAccessibleName(GyanisScribe::Base::uiText("识别语言"));

        buttons->addWidget(copyAll);
        buttons->addWidget(copyPart);
        buttons->addWidget(exportTxt);
        buttons->addWidget(exportJson);
        buttons->addWidget(m_language);
        buttons->addWidget(rerun);
        buttons->addWidget(closeButton);
        buttons->addStretch(1);

        setResult(m_image, m_result);

        QObject::connect(m_text, &QTextEdit::cursorPositionChanged, this,
                         [this]
                         {
                             // 光标落在哪一块就圈哪一行的方框；块序号对不上结果时不圈，而不是圈错行
                             m_lineIndex = lineIndexForBlock(m_result, m_text->textCursor().blockNumber());
                             refreshPreview();
                         });

        QObject::connect(copyAll, &QToolButton::clicked, this,
                         [this]
                         {
                             if (copyAllToClipboard().isEmpty())
                             {
                                 emit messageForUser(GyanisScribe::Base::uiText("这一张图没有认出可复制的文字"), Ui::ToastKind::Information);
                                 return;
                             }
                             emit messageForUser(GyanisScribe::Base::uiText("已复制全部文字"), Ui::ToastKind::Success);
                         });
        QObject::connect(copyPart, &QToolButton::clicked, this,
                         [this]
                         {
                             if (copySelectionToClipboard().isEmpty())
                             {
                                 emit messageForUser(GyanisScribe::Base::uiText("先在右边选中要复制的文字"), Ui::ToastKind::Information);
                                 return;
                             }
                             emit messageForUser(GyanisScribe::Base::uiText("已复制选中文字"), Ui::ToastKind::Success);
                         });
        QObject::connect(rerun, &QToolButton::clicked, this, [this] { emit recognizeRequested(selectedLanguage()); });
        QObject::connect(m_language, &QComboBox::currentTextChanged, this,
                         [this](const QString &language)
                         {
                             // 换语言立刻重认：让用户选完还要再点一次是多余的第三步
                             emit recognizeRequested(language);
                         });
        QObject::connect(exportTxt, &QToolButton::clicked, this, [this] { chooseAndExportTxt(); });
        QObject::connect(exportJson, &QToolButton::clicked, this, [this] { chooseAndExportJson(); });
        QObject::connect(closeButton, &QToolButton::clicked, this, &QWidget::close);
    }

    void OcrResultWindow::setResult(const QImage &image, const OcrResult &result)
    {
        m_image     = image;
        m_result    = result;
        m_lineIndex = -1;
        m_highlight = {};
        m_text->setPlainText(result.text());

        // 摘要与语言下拉都要跟着结果走：换语言重认之后旧摘要留在屏上比报错更难查
        m_status->setText(statusLineFor(m_result, m_image.size()));
        if (const int index = m_language->findText(m_result.language); index >= 0)
        {
            const QSignalBlocker blocker{m_language};
            m_language->setCurrentIndex(index);
        }
        refreshPreview();
    }

    QString OcrResultWindow::selectedLanguage() const
    {
        return m_language->currentText();
    }

    QString OcrResultWindow::copyAllToClipboard()
    {
        const auto text = m_result.text();
        if (text.isEmpty())
        {
            return {};
        }
        QApplication::clipboard()->setText(text);
        return text;
    }

    QString OcrResultWindow::copySelectionToClipboard()
    {
        const auto selected = m_text->textCursor().selectedText();
        if (selected.isEmpty())
        {
            return {};
        }
        QApplication::clipboard()->setText(selected);
        return selected;
    }

    Base::Expected<void> OcrResultWindow::exportTxt(const std::filesystem::path &filePath)
    {
        return writeTextFile(filePath, toTxt(m_result));
    }

    Base::Expected<void> OcrResultWindow::exportJson(const std::filesystem::path &filePath)
    {
        return writeTextFile(filePath, toJson(m_result, m_image.size()));
    }

    QString OcrResultWindow::statusText() const
    {
        return m_status == nullptr ? QString{} : m_status->text();
    }

    void OcrResultWindow::resizeEvent(QResizeEvent *event)
    {
        QWidget::resizeEvent(event);
        // 方框是按控件尺寸算的：窗口拖动之后必须重算，否则圈的位置整体偏移
        refreshPreview();
    }

    void OcrResultWindow::refreshPreview()
    {
        auto *preview = static_cast<Preview *>(m_preview);
        preview->setImage(m_image);
        m_highlight = highlightRectFor(m_result, m_lineIndex, m_image.size(), m_preview->size());
        preview->setHighlight(m_highlight, m_theme.palette().accent);
    }

    void OcrResultWindow::chooseAndExportTxt()
    {
        const auto suggested = suggestedExportPath(QStringLiteral("txt"), m_nameTemplate);
        const auto chosen    = QFileDialog::getSaveFileName(this, GyanisScribe::Base::uiText("导出 TXT"), QString::fromStdWString(suggested.wstring()),
                                                            GyanisScribe::Base::uiText("文本文件 (*.txt)"));
        // 取消对话框不是失败：不提示、也不动已有结果
        if (chosen.isEmpty())
        {
            return;
        }
        const auto path = chosen.toStdWString();
        if (auto exported = exportTxt(path); !exported)
        {
            const auto message =
                    QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("导出 TXT 失败：{}。请换一个目录再试一次"), exported.error().message).c_str());
            Base::Logger::warning(message.toStdString());
            emit messageForUser(message, Ui::ToastKind::Danger);
            return;
        }
        emit messageForUser(GyanisScribe::Base::uiText("已导出 TXT：") + chosen, Ui::ToastKind::Success);
    }

    void OcrResultWindow::chooseAndExportJson()
    {
        const auto suggested = suggestedExportPath(QStringLiteral("json"), m_nameTemplate);
        const auto chosen    = QFileDialog::getSaveFileName(this, GyanisScribe::Base::uiText("导出 JSON"), QString::fromStdWString(suggested.wstring()),
                                                            GyanisScribe::Base::uiText("JSON 文件 (*.json)"));
        if (chosen.isEmpty())
        {
            return;
        }
        const auto path = chosen.toStdWString();
        if (auto exported = exportJson(path); !exported)
        {
            const auto message =
                    QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("导出 JSON 失败：{}。请换一个目录再试一次"), exported.error().message).c_str());
            Base::Logger::warning(message.toStdString());
            emit messageForUser(message, Ui::ToastKind::Danger);
            return;
        }
        emit messageForUser(GyanisScribe::Base::uiText("已导出 JSON：") + chosen, Ui::ToastKind::Success);
    }

} // namespace GyanisScribe::Ocr
