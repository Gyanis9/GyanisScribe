#include "App/CaptureIntent.h"
#include "App/MainWindow.h"
#include "App/TrayController.h"
#include "App/UpdateCheck.h"
#include "Annotation/AnnotationPalette.h"
#include "Capture/ScreenUtils.h"
#include "Core/ImageFormat.h"
#include "Core/Logger.h"
#include "Core/OutputDecoration.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "History/HistoryRules.h"
#include "History/HistoryView.h"
#include "History/UnsavedArchive.h"
#include "Ocr/OcrEngineFactory.h"
#include "Pick/ColorFormat.h"
#include "Pin/PinLayout.h"
#include "Record/FfmpegAudioDevices.h"
#include "Record/FfmpegVideoEncoderEnumerator.h"
#include "Record/RecorderConfig.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Ui/DesignTokens.h"
#include "Ui/HotkeyEdit.h"
#include "Ui/IconProvider.h"
#include "Ui/Language.h"
#include "Ui/ThemeManager.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <array>

#include <filesystem>
#include <format>
#include <string>
namespace
{
    using GyanisScribe::Core::Expected;
    using GyanisScribe::Core::Logger;
    using GyanisScribe::Settings::ISettingsStore;
    using GyanisScribe::Ui::AppLanguage;
    using GyanisScribe::Ui::IconProvider;
    using GyanisScribe::Ui::stepIndexOf;
    using GyanisScribe::Ui::ThemeKind;
    using GyanisScribe::Ui::ThemeManager;

    using GyanisScribe::App::appLanguageAt;
    using GyanisScribe::App::indexOfAppLanguage;
    using GyanisScribe::App::indexOfThemeKind;
    using GyanisScribe::App::LanguageChoice;
    using GyanisScribe::App::MainWindowPage;
    using GyanisScribe::App::PageChoice;
    using GyanisScribe::App::ThemeChoice;
    using GyanisScribe::App::themeKindAt;

    /// 主窗口的全部页：序号、目标页、导航行标签与图标绑在同一张表上，表的顺序就是导航的顺序
    constexpr std::array<PageChoice, 10> kPages = {
            PageChoice{MainWindowPage::General, "通用", "Settings"},      PageChoice{MainWindowPage::Capture, "截图", "Capture"},
            PageChoice{MainWindowPage::Annotation, "标注", "Annotation"}, PageChoice{MainWindowPage::Pin, "贴图", "Pin"},
            PageChoice{MainWindowPage::Record, "录屏", "Record"},         PageChoice{MainWindowPage::Ocr, "文字识别", "Ocr"},
            PageChoice{MainWindowPage::Hotkey, "快捷键", "Hotkey"},       PageChoice{MainWindowPage::Theme, "主题", "Theme"},
            PageChoice{MainWindowPage::About, "关于", "About"},           PageChoice{MainWindowPage::History, "历史", "History"},
    };

    /// 下拉框的全部行：序号、档位、可见标签三者绑在这一张表上
    constexpr std::array<ThemeChoice, 4> kThemeChoices = {
            ThemeChoice{ThemeKind::Light, "浅色"},
            ThemeChoice{ThemeKind::Dark, "深色"},
            ThemeChoice{ThemeKind::System, "跟随系统"},
            ThemeChoice{ThemeKind::HighContrast, "高对比度"},
    };

    /// 界面语言的全部行：标签用各自语言自称，因此这一张表不进翻译目录
    constexpr std::array<LanguageChoice, 2> kLanguageChoices = {
            LanguageChoice{AppLanguage::Chinese, "中文"},
            LanguageChoice{AppLanguage::English, "English"},
    };

    /// 导航列的固定宽度：最长的标签是「文字识别」四个汉字加一枚图标与左右内边距，再宽就要挤右侧的字段了
    constexpr int kNavColumnWidth = 180;

    /// 导航行上记图标名的角色：换主题时按它重取图标，不用把 kPages 与行号再对一遍
    constexpr int kNavIconNameRole = Qt::UserRole + 1;

    /// 历史条数这一格能填到的最大值：再多就得靠搜索找，而截图量本身也撑不到这里
    constexpr int kMaximumEntriesLimit = 10000;

    /// 历史天数这一格能填到的最大值，按十年给：比这更久的留法就是「不限」那一档
    constexpr int kRetentionDaysLimit = 3650;

    /**
     * @brief 一页设置的骨架：顶部一行说明文字加一张空表单
     * @param page 这一页的容器，版面由本函数接管
     * @param intro 已经过 uiText 的说明文字
     * @return 归 page 所有的表单，调用方往它 addRow
     * @details 每一类都得先有一句「这一类管什么、改了会怎样」：需求第五节要的是能自己上手的设置界面，
     *          只列控件名会把「改了不重启就不生效」这类关键信息藏起来。
     */
    QFormLayout *makePageForm(QWidget *page, const QString &intro)
    {
        auto *layout = new QVBoxLayout{page};
        layout->setContentsMargins(GyanisScribe::Ui::kSpacingLarge, GyanisScribe::Ui::kSpacingLarge, GyanisScribe::Ui::kSpacingLarge, GyanisScribe::Ui::kSpacingLarge);
        layout->setSpacing(GyanisScribe::Ui::kSpacingSmall);

        auto *text = new QLabel{intro, page};
        text->setObjectName(QStringLiteral("pageHint"));
        text->setWordWrap(true);
        text->setMaximumWidth(GyanisScribe::Ui::kIntroMaximumWidth);
        // 说明行是次要文本，同时要让读屏软件把它当成这一段正文念出来
        text->setAccessibleName(GyanisScribe::Core::uiText("这一类的说明"));
        layout->addWidget(text);

        auto *form = new QFormLayout;
        form->setSpacing(GyanisScribe::Ui::kSpacingSmall);
        // 标签左对齐并与字段同一水平线：右对齐会让标签与字段之间那条缝忽宽忽窄，顶对齐让单行字段看着下沉
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        layout->addLayout(form);
        layout->addStretch();
        return form;
    }

    /**
     * @brief 给一页设置套上卡片外观、页标题，并把字段宽度收到同一档
     * @param page 这一页的容器（makePageForm 已经接管它的版面）
     * @param title 已经过 uiText 的页名，与导航行同源
     * @details 标题与导航行是同一个词：没有标题时，切进这一页只看得见左侧导航那一行高亮，
     *          右侧整片是控件，「我在哪一节」要回头看导航。
     *          说明行的次要色走样式表按对象名给，不在这里 setPalette：钉在控件上会在切主题时
     *          停在旧的那一档色，而样式表是整片换掉的。
     */
    void decoratePage(QWidget &page, const QString &title)
    {
        page.setObjectName(QStringLiteral("pageCard"));
        page.setAttribute(Qt::WA_StyledBackground, true);

        auto *layout = page.layout();
        if (auto *box = qobject_cast<QVBoxLayout *>(layout); box != nullptr)
        {
            auto *heading = new QLabel{title, &page};
            heading->setObjectName(QStringLiteral("pageTitle"));
            GyanisScribe::Ui::applyTextRole(*heading, GyanisScribe::Ui::TextRole::PageTitle);
            box->insertWidget(0, heading);
        }

        // 字段宽度收到一档：路径框与下拉拉到整窗宽之后，「这一格填什么」和下一格就分不出来了
        if (auto *form = page.findChild<QFormLayout *>(); form != nullptr)
        {
            for (int row = 0; row < form->rowCount(); ++row)
            {
                if (auto *field = form->itemAt(row, QFormLayout::FieldRole); field != nullptr)
                {
                    if (auto *widget = field->widget(); widget != nullptr)
                    {
                        widget->setMaximumWidth(GyanisScribe::Ui::kFieldMaximumWidth);
                    }
                }
            }
        }
    }

    /**
     * @brief 一行「可写目录」的设置控件
     * @param settings 配置存储
     * @param key 键名
     * @param title 中文名
     * @param fallback 键不存在时显示的默认目录（只作提示，不写回配置）
     * @param icons 图标供给器，浏览按钮的图标由它取
     * @return 归调用方所有的行控件
     */
    QWidget *makePathRow(ISettingsStore &settings, const std::string_view key, const QString &title, const std::filesystem::path &fallback, IconProvider &icons)
    {
        auto      *row     = new QWidget;
        auto      *layout  = new QHBoxLayout{row};
        auto      *editor  = new QLineEdit{row};
        const auto stored  = settings.stringValue(key, QString{});
        const auto display = stored.isEmpty() ? GyanisScribe::Core::Paths::toDisplayText(fallback) : stored;
        editor->setText(display);
        editor->setClearButtonEnabled(false);
        editor->setAccessibleName(title);
        editor->setToolTip(GyanisScribe::Core::uiText("%1。留空表示用默认目录").arg(title));

        auto *browse = new QPushButton{GyanisScribe::Core::uiText("浏览…"), row};
        icons.applyIcon(*browse, "Save");
        browse->setAccessibleName(GyanisScribe::Core::uiText("选择%1的目录").arg(title));

        layout->addWidget(editor);
        layout->addWidget(browse);

        QObject::connect(browse, &QPushButton::clicked, row,
                         [&, editor, title]
                         {
                             const auto picked = QFileDialog::getExistingDirectory(row, GyanisScribe::Core::uiText("选择%1").arg(title), editor->text());
                             if (!picked.isEmpty())
                             {
                                 editor->setText(picked);
                             }
                         });

        // 失焦即写：设置页没有「保存」按钮，改完就看到效果是需求第五节的要求
        QObject::connect(editor, &QLineEdit::editingFinished, row,
                         [&, editor, key, title]
                         {
                             const auto text = editor->text().trimmed();
                             if (text.isEmpty())
                             {
                                 // 清空表示回到默认：这里不写空串，而是把键删到回落状态，显示也一并回落到默认目录
                                 if (auto written = settings.setValue(key, QString{}); !written)
                                 {
                                     Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                                 }
                                 editor->setText(GyanisScribe::Core::Paths::toDisplayText(fallback));
                                 return;
                             }
                             if (auto written = settings.setValue(key, text); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return row;
    }

    /**
     * @brief 一行「0 表示不限」的整数设置
     * @param settings 配置存储
     * @param key 键名
     * @param title 中文名，用作无障碍名称与日志里的线索
     * @param suffix 跟在数字后面的量词
     * @param maximum 这一格能填到的最大值
     * @param fallback 配置里没有这一项时显示的值
     * @return 归调用方所有的数字框，由 addRow 挂上父对象
     * @details 0 那一档在界面上写成「不限」而不是「0」：读端把 0 解释成不限，
     *          摆一个 0 出来人人都可能读成「一条都不留」，而那恰好是它会删光历史的样子。
     */
    QSpinBox *makeOptionalLimitSpin(ISettingsStore &settings, const std::string_view key, const QString &title, const QString &suffix, const int maximum, const int fallback)
    {
        auto *spin = new QSpinBox;
        spin->setRange(0, maximum);
        spin->setSpecialValueText(GyanisScribe::Core::uiText("不限"));
        spin->setSuffix(suffix);
        spin->setAccessibleName(title);
        // 配置里的负数会被范围夹到 0 那一档，也就是「不限」——与 History::policyFromSettings 的读法同解
        spin->setValue(settings.intValue(key, fallback));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, key, title](int value)
                         {
                             if (auto written = settings.setValue(key, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 一行「离散阶梯」下拉：选项直接列出阶梯里的值
     * @tparam Size 阶梯档数
     * @param settings 配置存储
     * @param key 键名，存进去的就是选中的那个值
     * @param steps 阶梯表，取值只能是 Ui::kAnnotationWidths 或 Ui::kFontSizes
     * @param fallback 配置里没有、或存的值不在阶梯里时的回落档
     * @param title 中文名，用作无障碍名称与日志里的线索
     * @return 归调用方所有的下拉框，由 addRow 挂上父对象
     */
    template<std::size_t Size>
    QComboBox *makeStepCombo(ISettingsStore &settings, const std::string_view key, const std::array<int, Size> &steps, const int fallback, const QString &title)
    {
        auto *combo = new QComboBox;
        for (const auto stepValue: steps)
        {
            combo->addItem(QString::number(stepValue), stepValue);
        }
        combo->setAccessibleName(title);

        const auto stored = settings.intValue(key, fallback);
        if (const auto step = stepIndexOf(steps, stored); step)
        {
            combo->setCurrentIndex(*step);
        } else
        {
            // 就近取一档会让人以为存的那档生效了：摆回默认档，同时把坏值说进日志
            Logger::warning(std::format("{}读不出阶梯里的档位：配置里存的是 {}，这一行按 {} 显示", title.toStdString(), stored, fallback));
            combo->setCurrentIndex(static_cast<int>(stepIndexOf(steps, fallback).value_or(0)));
        }

        // 接线排在设初值之后：否则「把界面摆到位」这一步会被当成用户改动写回配置
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, key, title, combo](int)
                         {
                             if (auto written = settings.setValue(key, combo->currentData().toInt()); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return combo;
    }

    /**
     * @brief 决定整数设置项这一行显示哪个值
     * @tparam T 该键校验函数的成功载荷类型
     * @param title 中文名，日志里点名是哪一行
     * @param stored 配置读出的整数
     * @param parsed 该键自己的校验结果：只看它成不成立，成功载荷不回界面（界面要的是整数档位）
     * @param fallback 校验不过时的回落值
     * @return 这一行该显示的值
     * @details 复用模块里的校验函数而不是在这里重抄一遍范围：两处判据不同解时，
     *          设置页会显示一档运行期根本不接受的值，而报错文案也各说一套。
     */
    template<typename T>
    int shownValueForSetting(const QString &title, const int stored, const Expected<T> &parsed, const int fallback)
    {
        if (parsed)
        {
            return stored;
        }
        Logger::warning(std::format("{}读不出：{}，这一行按 {} 显示", title.toStdString(), parsed.error().message, fallback));
        return fallback;
    }

    /**
     * @brief 录屏时要不要把光标位置圈在帧上
     * @param settings 配置存储
     * @return 归调用方所有的复选框，由 addRow 挂上父对象
     */
    QCheckBox *makeCursorHighlightCheck(ISettingsStore &settings, const IconProvider &icons)
    {
        auto *check = new QCheckBox;
        check->setIcon(icons.icon("CursorHighlight"));
        check->setText(GyanisScribe::Core::uiText("把光标位置圈在画面里"));
        check->setAccessibleName(GyanisScribe::Core::uiText("光标高亮"));
        check->setToolTip(GyanisScribe::Core::uiText("整屏、选区与窗口录屏都生效；录窗口时按平台报出的客户区左上角落位，那份读数拿不到时这一帧就不画圈"));
        check->setChecked(settings.boolValue(GyanisScribe::Settings::kRecordCursorHighlight, false));

        // 接线排在设初值之后：摆到位不等于用户改过，否则一开设置页就把默认值写进配置
        QObject::connect(check, &QCheckBox::toggled, check,
                         [&settings](bool checked)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kRecordCursorHighlight, checked); !written)
                             {
                                 Logger::warning(std::format("录屏光标高亮开关没能写入：{}", written.error().message));
                             }
                         });
        return check;
    }

    /**
     * @brief 截图时要不要把鼠标指针画进成品
     * @param settings 配置存储
     * @return 归调用方所有的复选框，由 addRow 挂上父对象
     * @details 默认档与 CaptureService 读的是同一个常量：两处各写一份就会造成
     *          「界面显示已开、编排按未开走」这种两边不同解。
     */
    QCheckBox *makeIncludeCursorCheck(ISettingsStore &settings)
    {
        auto *check = new QCheckBox;
        check->setText(GyanisScribe::Core::uiText("把鼠标指针画进成品"));
        check->setAccessibleName(GyanisScribe::Core::uiText("含鼠标指针"));
        check->setToolTip(
                GyanisScribe::Core::uiText("整屏与区域截图生效；滚动长截图按帧接图，画进去会留下一串指针，那一趟不带。非 Windows 平台取不到系统指针的形状，开了也不会出现"));
        check->setChecked(settings.boolValue(GyanisScribe::Settings::kCaptureIncludeCursor, GyanisScribe::Settings::kDefaultCaptureIncludeCursor));

        // 接线排在设初值之后：摆到位不等于用户改过，否则一开设置页就把默认值写进配置
        QObject::connect(check, &QCheckBox::toggled, check,
                         [&settings](bool checked)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kCaptureIncludeCursor, checked); !written)
                             {
                                 Logger::warning(std::format("截图含指针开关没能写入：{}", written.error().message));
                             }
                         });
        return check;
    }

    /**
     * @brief 一枚绑定到设置键的复选框：勾选写 true、去勾写 false
     * @param settings 配置存储
     * @param key 设置键
     * @param defaultOn 「没填」那一档，必须与消费端读的时候给的默认值一致
     * @param label 复选框正文
     * @param accessibleName 无障碍名
     * @param tooltip 悬停说明
     * @return 归调用方所有的复选框
     * @details 接线排在设初值之后：摆到位不等于用户改过，否则一开设置页就把默认值写进配置
     */
    QCheckBox *makeSettingCheck(ISettingsStore &settings, const std::string_view key, const bool defaultOn, const QString &label, const QString &accessibleName,
                                const QString &tooltip)
    {
        auto *check = new QCheckBox;
        check->setText(label);
        check->setAccessibleName(accessibleName);
        check->setToolTip(tooltip);
        check->setChecked(settings.boolValue(key, defaultOn));

        QObject::connect(check, &QCheckBox::toggled, check,
                         [&settings, key](const bool checked)
                         {
                             if (auto written = settings.setValue(key, checked); !written)
                             {
                                 Logger::warning(std::format("设置项 {} 没能写入：{}", key, written.error().message));
                             }
                         });
        return check;
    }

    /**
     * @brief 界面语言下拉：档位读写都走 Ui/Language 的词表
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     */
    QComboBox *makeUiLanguageCombo(ISettingsStore &settings)
    {
        auto *combo = new QComboBox;
        for (const auto &choice: kLanguageChoices)
        {
            combo->addItem(GyanisScribe::Core::uiText(choice.label.data()));
        }
        combo->setAccessibleName(GyanisScribe::Core::uiText("界面语言"));
        combo->setToolTip(GyanisScribe::Core::uiText("切换语言要重启智录才生效"));

        const auto stored = settings.stringValue(GyanisScribe::Settings::kUiLanguage, QString::fromUtf8(std::string{GyanisScribe::Ui::appLanguageText(AppLanguage::Chinese)}));
        if (const auto parsed = GyanisScribe::Ui::parseAppLanguage(stored.toStdString()); parsed)
        {
            combo->setCurrentIndex(indexOfAppLanguage(*parsed));
        } else
        {
            // 配置里的值读不出时下拉框显示中文，但原因要落进日志：只把界面悄悄拨回默认档，用户会以为是自己选错的
            Logger::warning(std::format("设置页读不出界面语言：{}，下拉框按中文显示", parsed.error().message));
            combo->setCurrentIndex(indexOfAppLanguage(AppLanguage::Chinese));
        }

        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo](int index)
                         {
                             const auto choice = appLanguageAt(index);
                             if (!choice)
                             {
                                 return;
                             }
                             const auto token = QString::fromUtf8(std::string{GyanisScribe::Ui::appLanguageText(*choice)});
                             if (auto written = settings.setValue(GyanisScribe::Settings::kUiLanguage, token); !written)
                             {
                                 Logger::warning(std::format("界面语言没能保存：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }

    /**
     * @brief 配色方案下拉：标签与档位都来自 themeChoices()
     * @param theme 主题管理器，选中即时生效
     * @return 归调用方所有的下拉框
     */
    QComboBox *makeThemeCombo(ThemeManager &theme)
    {
        auto *combo = new QComboBox;
        for (const auto &choice: kThemeChoices)
        {
            combo->addItem(GyanisScribe::Core::uiText(choice.label.data()));
        }
        combo->setAccessibleName(GyanisScribe::Core::uiText("主题"));
        combo->setCurrentIndex(indexOfThemeKind(theme.requestedKind()));
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&theme](int index)
                         {
                             if (const auto kind = themeKindAt(index); kind)
                             {
                                 if (auto applied = theme.setTheme(*kind); !applied)
                                 {
                                     Logger::warning(std::format("主题没能切换：{}", applied.error().message));
                                 }
                             }
                         });
        return combo;
    }

    /**
     * @brief 识别语言下拉：取值来自后端词表，不在界面这一层另列一份
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     */
    QComboBox *makeOcrLanguageCombo(ISettingsStore &settings)
    {
        auto *combo = new QComboBox;
        combo->addItems(GyanisScribe::Ocr::supportedOcrLanguageTags());
        combo->setAccessibleName(GyanisScribe::Core::uiText("识别语言"));
        const auto stored = settings.stringValue(GyanisScribe::Settings::kOcrLanguage, QString::fromUtf8(GyanisScribe::Ocr::kDefaultOcrLanguage));
        if (const int index = combo->findText(stored); index >= 0)
        {
            combo->setCurrentIndex(index);
        }
        QObject::connect(combo, &QComboBox::currentTextChanged, combo,
                         [&settings, combo](const QString &text)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kOcrLanguage, text); !written)
                             {
                                 Logger::warning(std::format("识别语言没能保存：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }

    /**
     * @brief 取色复制写法下拉：项名与序号都来自 Pick 那张表
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     */
    QComboBox *makeColorFormatCombo(ISettingsStore &settings)
    {
        auto *combo = new QComboBox;
        combo->addItems(GyanisScribe::Pick::colorFormatLabels());
        combo->setAccessibleName(GyanisScribe::Core::uiText("颜色写法"));
        const auto stored = GyanisScribe::Pick::parseColorFormat(settings.stringValue(GyanisScribe::Settings::kPickFormat, QStringLiteral("Hex")).toStdString());
        combo->setCurrentIndex(GyanisScribe::Pick::indexOfColorFormat(stored ? *stored : GyanisScribe::Pick::ColorFormat::Hex));
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo](int index)
                         {
                             const auto format = GyanisScribe::Pick::colorFormatAt(index);
                             if (!format)
                             {
                                 return;
                             }
                             const auto token = QString::fromUtf8(GyanisScribe::Pick::colorFormatToken(*format).data(),
                                                                  static_cast<qsizetype>(GyanisScribe::Pick::colorFormatToken(*format).size()));
                             if (auto written = settings.setValue(GyanisScribe::Settings::kPickFormat, token); !written)
                             {
                                 Logger::warning(std::format("颜色写法没能保存：{}", written.error().message));
                             }
                         });
        return combo;
    }

    /**
     * @brief 新建贴图的默认不透明度（百分比）
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     */
    QSpinBox *makeOpacitySpin(ISettingsStore &settings)
    {
        auto *spin = new QSpinBox;
        // 下限取 1：0% 的贴图在屏幕上看不见却还挡鼠标，那种「贴图功能坏了」的现象最难查
        spin->setRange(1, 100);
        spin->setSuffix(QStringLiteral(" %"));
        spin->setAccessibleName(GyanisScribe::Core::uiText("默认不透明度"));

        const auto title  = GyanisScribe::Core::uiText("贴图默认不透明度");
        const auto stored = settings.intValue(GyanisScribe::Settings::kPinDefaultOpacity, GyanisScribe::Pin::kDefaultOpacityPercent);
        const auto parsed = GyanisScribe::Pin::parseOpacityPercentSetting(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Pin::kDefaultOpacityPercent));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kPinDefaultOpacity, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 录屏的默认帧率
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     */
    QSpinBox *makeFrameRateSpin(ISettingsStore &settings)
    {
        auto *spin = new QSpinBox;
        spin->setRange(1, GyanisScribe::Record::kMaximumFrameRate);
        spin->setSuffix(GyanisScribe::Core::uiText(" 帧/秒"));
        spin->setAccessibleName(GyanisScribe::Core::uiText("默认帧率"));

        const auto title  = GyanisScribe::Core::uiText("录屏默认帧率");
        const auto stored = settings.intValue(GyanisScribe::Settings::kRecordDefaultFrameRate, GyanisScribe::Record::kDefaultFrameRate);
        const auto parsed = GyanisScribe::Record::parseFrameRateSetting(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Record::kDefaultFrameRate));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kRecordDefaultFrameRate, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 截图的延时秒数
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     * @details 0 是「不延时」这一档而不是没填，所以给 0 挂上说明文字；这一格存在的理由是
     *          菜单与悬停提示一碰就没，不延时永远截不到它们。
     */
    QSpinBox *makeCaptureDelaySpin(ISettingsStore &settings)
    {
        auto *spin = new QSpinBox;
        spin->setRange(0, GyanisScribe::App::kMaximumCaptureDelaySeconds);
        spin->setSuffix(GyanisScribe::Core::uiText(" 秒"));
        spin->setAccessibleName(GyanisScribe::Core::uiText("延时截图"));
        spin->setToolTip(GyanisScribe::Core::uiText("按下截图后等这么久再开始，用来截菜单、悬停提示这类一碰就没的画面；0 表示不延时"));

        const auto title  = GyanisScribe::Core::uiText("延时截图");
        const auto stored = settings.intValue(GyanisScribe::Settings::kCaptureDelaySeconds, 0);
        const auto parsed = GyanisScribe::App::parseCaptureDelay(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, 0));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kCaptureDelaySeconds, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 圆角截图的半径
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     * @details 切不切由「圆角截图」那一格决定，这一格只管切多圆，所以它一直可编辑（把两处绑成
     *          开关联动只会让人以为半径本身是个开关）。范围与默认档住在截图那一侧，读端遇到越界值按默认档继续。
     */
    QSpinBox *makeCornerRadiusSpin(ISettingsStore &settings)
    {
        auto *spin = new QSpinBox;
        spin->setRange(GyanisScribe::Capture::kCaptureCornerRadiusMinimum, GyanisScribe::Capture::kCaptureCornerRadiusMaximum);
        spin->setSuffix(GyanisScribe::Core::uiText(" 像素"));
        spin->setAccessibleName(GyanisScribe::Core::uiText("圆角半径"));
        spin->setToolTip(GyanisScribe::Core::uiText("开着「圆角截图」时把四角切多圆：只对区域截图生效，整屏、窗口与滚动长截图不带"));

        const auto title  = GyanisScribe::Core::uiText("圆角半径");
        const auto stored = settings.intValue(GyanisScribe::Settings::kCaptureCornerRadius, GyanisScribe::Capture::kCaptureCornerRadiusDefault);
        const auto parsed = GyanisScribe::Capture::parseCaptureCornerRadius(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Capture::kCaptureCornerRadiusDefault));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kCaptureCornerRadius, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 录屏倒计时的秒数
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     * @details 0 是「立即开始」这一档而不是「没填」，工具提示里说明这一档的名字。
     */
    QSpinBox *makeCountdownSpin(ISettingsStore &settings)
    {
        auto *spin = new QSpinBox;
        spin->setRange(0, GyanisScribe::Record::kMaximumCountdownSeconds);
        spin->setSuffix(GyanisScribe::Core::uiText(" 秒"));
        spin->setAccessibleName(GyanisScribe::Core::uiText("录屏倒计时"));
        spin->setToolTip(GyanisScribe::Core::uiText("按下录屏键后先数这么几秒再开始录，用来把要录的画面摆好；0 表示立即开始"));

        const auto title  = GyanisScribe::Core::uiText("录屏倒计时");
        const auto stored = settings.intValue(GyanisScribe::Settings::kRecordCountdownSeconds, GyanisScribe::Record::kDefaultCountdownSeconds);
        const auto parsed = GyanisScribe::Record::parseCountdownSetting(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Record::kDefaultCountdownSeconds));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kRecordCountdownSeconds, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 录屏的画面码率，单位 kbps
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     * @details 0 是「按质量优先」这一档而不是「没填」，所以给 0 挂上说明文字：
     *          空着个 0 会让人以为码率没生效，而它正是这一档的名字。
     */
    QSpinBox *makeVideoBitrateSpin(ISettingsStore &settings)
    {
        auto *spin = new QSpinBox;
        spin->setRange(GyanisScribe::Record::kDefaultVideoBitrateKbps, GyanisScribe::Record::kMaximumVideoBitrateKbps);
        spin->setSingleStep(500);
        spin->setSuffix(GyanisScribe::Core::uiText(" kbps"));
        spin->setSpecialValueText(GyanisScribe::Core::uiText("按质量优先"));
        spin->setAccessibleName(GyanisScribe::Core::uiText("画面码率"));

        const auto title  = GyanisScribe::Core::uiText("画面码率");
        const auto stored = settings.intValue(GyanisScribe::Settings::kRecordVideoBitrate, GyanisScribe::Record::kDefaultVideoBitrateKbps);
        const auto parsed = GyanisScribe::Record::parseVideoBitrateSetting(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Record::kDefaultVideoBitrateKbps));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kRecordVideoBitrate, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 把词表里的窄字符取值转成 Qt 文本
     * @param token 配置里存的那一串
     * @return 原样的 Qt 文本，不经翻译
     */
    QString tokenText(const std::string_view token)
    {
        // 按「指针 + 长度」取，不靠零终止：词表里短到一眼看完的 token 也一样
        return QString::fromUtf8(token.data(), static_cast<qsizetype>(token.size()));
    }

    /**
     * @brief 录屏范围下拉：档位、配置里的取值与设置页标签都取自 Record::kRecordScopeChoices
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     * @details 初值按 RecorderConfig 的 mode 初值回落，而不是在这里另写一档：
     *          两处不同解时，用户没动过这一格却录到与界面显示不符的画面。
     */
    QComboBox *makeRecordScopeCombo(ISettingsStore &settings, const IconProvider &icons)
    {
        auto *combo = new QComboBox;
        for (const auto &choice: GyanisScribe::Record::kRecordScopeChoices)
        {
            // 图标名与标签同在词表里：三档录法的字形不一样，扫一眼就分得出，不必逐条读字
            combo->addItem(icons.icon(choice.iconName), GyanisScribe::Core::uiText(choice.label.data()), tokenText(choice.token));
        }
        combo->setAccessibleName(GyanisScribe::Core::uiText("录制范围"));
        combo->setToolTip(GyanisScribe::Core::uiText("「上次选区」用最近一次截图框出的那块矩形；「当前窗口」录按下开始时最靠前的那个窗口。改完从下一次录屏开始生效"));

        const auto defaultScope = GyanisScribe::Record::RecorderConfig{}.mode;
        const auto stored       = settings.stringValue(GyanisScribe::Settings::kRecordScope, tokenText(GyanisScribe::Record::recordScopeText(defaultScope)));
        const auto parsed       = GyanisScribe::Record::parseRecordScope(stored);
        if (!parsed)
        {
            // 读不出时下拉框显示默认档，但原因要落进日志：只把界面悄悄拨回整屏，用户会以为是自己选错的
            Logger::warning(std::format("设置页读不出录制范围：{}，下拉框按默认档显示", parsed.error().message));
        }
        const auto chosen = parsed ? *parsed : defaultScope;
        combo->setCurrentIndex(combo->findData(tokenText(GyanisScribe::Record::recordScopeText(chosen))));

        // 接线排在设初值之后：否则「把界面摆到位」这一步会被当成用户改动写回配置
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo](int)
                         {
                             const auto token = combo->currentData().toString();
                             if (auto written = settings.setValue(GyanisScribe::Settings::kRecordScope, token); !written)
                             {
                                 Logger::warning(std::format("录制范围没能写入：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }

    /**
     * @brief 「截完之后做什么」的下拉框：档位、配置里的取值与标签都取自 App::kCaptureIntentChoices
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     * @details 初值按词表第一档回落，而不是在这里另写一档：两处不同解时，用户没动过这一格，
     *          按下热键走的却与界面显示的不是同一步。
     */
    QComboBox *makeCaptureIntentCombo(ISettingsStore &settings, const IconProvider &icons)
    {
        auto *combo = new QComboBox;
        for (const auto &choice: GyanisScribe::App::kCaptureIntentChoices)
        {
            // 与录制范围那格同一条形状：图标名跟着词表走，界面只负责取
            combo->addItem(icons.icon(choice.iconName), GyanisScribe::Core::uiText(choice.label.data()), tokenText(choice.token));
        }
        combo->setAccessibleName(GyanisScribe::Core::uiText("截完之后"));
        combo->setToolTip(GyanisScribe::Core::uiText("选「复制到剪贴板」「直接存成文件」「直接贴到屏幕上」时都不再弹标注窗；改完从下一次截图开始生效"));

        const auto stored =
                settings.stringValue(GyanisScribe::Settings::kCaptureAfterAction, tokenText(GyanisScribe::App::captureIntentText(GyanisScribe::App::defaultCaptureIntent())));
        const auto parsed = GyanisScribe::App::parseCaptureIntent(stored);
        if (!parsed)
        {
            Logger::warning(std::format("设置页读不出「截完之后」：{}，下拉框按默认档显示", parsed.error().message));
        }
        const auto chosen = parsed ? *parsed : GyanisScribe::App::defaultCaptureIntent();
        combo->setCurrentIndex(combo->findData(tokenText(GyanisScribe::App::captureIntentText(chosen))));

        // 接线排在设初值之后：否则「把界面摆到位」这一步会被当成用户改动写回配置
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo](int)
                         {
                             const auto token = combo->currentData().toString();
                             if (auto written = settings.setValue(GyanisScribe::Settings::kCaptureAfterAction, token); !written)
                             {
                                 Logger::warning(std::format("「截完之后」没能写入：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }
    /**
     * @brief 成品图片存成什么格式的下拉：档位、token 与后缀都在 Core::kImageFormatChoices 里
     * @param settings 配置存储
     * @return 归调用方所有的下拉框，由 addRow 挂上父对象
     */
    QComboBox *makeImageFormatCombo(ISettingsStore &settings)
    {
        auto *combo = new QComboBox;
        // 只摆这台机器真写得出来的那几档：Qt 的图像编码器是插件，摆一档写不出的就是让用户白失败一次
        for (const auto format: GyanisScribe::Core::writableImageFormats())
        {
            const auto &choice = *std::ranges::find_if(GyanisScribe::Core::kImageFormatChoices, [format](const auto &row) { return row.format == format; });
            combo->addItem(GyanisScribe::Core::uiText(choice.label.data()), tokenText(choice.token));
        }
        combo->setAccessibleName(GyanisScribe::Core::uiText("保存格式"));
        combo->setToolTip(GyanisScribe::Core::uiText("「直接存成文件」与贴图右键的「保存」按这一档写文件；PNG 无损，JPG 与 WebP 用来把照片类的截图压小"));

        const auto stored = settings.stringValue(GyanisScribe::Settings::kSaveImageFormat, tokenText(GyanisScribe::Core::imageFormatToken(GyanisScribe::Core::ImageFormat::Png)));
        const auto parsed = GyanisScribe::Core::parseImageFormat(stored);
        if (!parsed)
        {
            Logger::warning(std::format("设置页读不出「保存格式」：{}，下拉框按 PNG 显示", parsed.error().message));
        }
        const auto chosen      = parsed.value_or(GyanisScribe::Core::ImageFormat::Png);
        auto       chosenIndex = combo->findData(tokenText(GyanisScribe::Core::imageFormatToken(chosen)));
        if (chosenIndex < 0)
        {
            // 配置里那一档这台机器写不出来（换过机器或手改过配置）：按第一档摆，用户改一次就写回新值
            Logger::warning(std::format("设置里存的保存格式「{}」在这台机器上写不出来，下拉框按第一档显示", stored.toStdString()));
            chosenIndex = 0;
        }
        combo->setCurrentIndex(chosenIndex);

        // 接线排在设初值之后：否则「把界面摆到位」这一步会被当成用户改动写回配置
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo](int)
                         {
                             const auto token = combo->currentData().toString();
                             if (auto written = settings.setValue(GyanisScribe::Settings::kSaveImageFormat, token); !written)
                             {
                                 Logger::warning(std::format("「保存格式」没能写入：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }

    /**
     * @brief 有损档的质量数字框
     * @param settings 配置存储
     * @return 归调用方所有的数字框，由 addRow 挂上父对象
     * @details 越界的存值不夹取、按默认档显示并记一条日志：夹取会让用户以为自己要的那个值生效了。
     */
    QSpinBox *makeImageQualitySpin(ISettingsStore &settings)
    {
        auto *spin = new QSpinBox;
        spin->setRange(GyanisScribe::Core::kMinimumImageQuality, GyanisScribe::Core::kMaximumImageQuality);
        spin->setAccessibleName(GyanisScribe::Core::uiText("保存质量"));
        spin->setToolTip(GyanisScribe::Core::uiText("只有 JPG 与 WebP 吃这一格，PNG 是无损的；数字越大越清晰，文件也越大"));

        const auto stored = settings.intValue(GyanisScribe::Settings::kSaveImageQuality, GyanisScribe::Core::kDefaultImageQuality);
        const auto parsed = GyanisScribe::Core::parseImageQuality(stored);
        if (!parsed)
        {
            Logger::warning(std::format("设置页读不出「保存质量」：{}，这一格按 {} 显示", parsed.error().message, GyanisScribe::Core::kDefaultImageQuality));
        }
        spin->setValue(parsed.value_or(GyanisScribe::Core::kDefaultImageQuality));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kSaveImageQuality, value); !written)
                             {
                                 Logger::warning(std::format("「保存质量」没能写入：{}", written.error().message));
                             }
                         });
        return spin;
    }


    /**
     * @brief 一路音频输入的下拉框
     * @param settings 配置存储
     * @param key 这一路写哪个键
     * @param title 这一路的中文名，日志与无障碍名都从它取
     * @param enumerator 设备清单的来源，两格共用一个
     * @return 归调用方所有的下拉框
     * @details 清单是异步来的：起 ffmpeg 进程不能压在主线程上。配置里存的那一路这次列不出来时，
     *          下拉框回到「不取这一路」而配置原样不动——取不到声会由录屏那一步拒绝开始并说明原因，
     *          在这里顺手改掉用户的选择不但掩盖了设备被占用，还会让下一次插回设备时用回他原来的选择。
     */
    QComboBox *makeAudioDeviceCombo(ISettingsStore &settings, const std::string_view key, const QString &title, GyanisScribe::Record::FfmpegAudioDeviceEnumerator &enumerator)
    {
        auto *combo = new QComboBox;
        combo->addItem(GyanisScribe::Core::uiText("不取这一路"), QString());
        combo->setAccessibleName(title);

        const auto stored = settings.stringValue(key, QString());
        QObject::connect(&enumerator, &GyanisScribe::Record::FfmpegAudioDeviceEnumerator::updated, combo,
                         [combo, stored, title](const QVector<GyanisScribe::Record::AudioInputDevice> &devices)
                         {
                             for (const auto &device: devices)
                             {
                                 combo->addItem(device.friendlyName, device.alternativeName);
                             }
                             if (const int index = combo->findData(stored); index >= 0)
                             {
                                 combo->setCurrentIndex(index);
                             } else if (!stored.isEmpty())
                             {
                                 Logger::warning(std::format("设置页列不出{}：上次选的设备 {} 这次不在清单里，配置原样留着", title.toStdString(), stored.toStdString()));
                             }
                         });
        QObject::connect(&enumerator, &GyanisScribe::Record::FfmpegAudioDeviceEnumerator::failed, combo,
                         [combo, title](const GyanisScribe::Core::Error &error)
                         {
                             // 列不出来时这一格只剩「不取这一路」：原因挂在悬停提示上，用户不必去翻日志才知道这里为什么没有可选项
                             combo->setToolTip(QString::fromUtf8(error.message.c_str()));
                             Logger::warning(std::format("设置页列不出{}：{}", title.toStdString(), error.message));
                         });
        enumerator.refresh();

        // 接线排在设初值之后：把界面摆到位不等于用户改过，否则一开设置页就把选择写回配置
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, key, title, combo](int)
                         {
                             if (auto written = settings.setValue(key, combo->currentData().toString()); !written)
                             {
                                 Logger::warning(std::format("音频设备没能写入：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }

    /**
     * @brief 视频编码器下拉：自动一档在前，探得通的候选跟在后面
     * @param settings 配置存储
     * @param enumerator 编码器清单的来源
     * @return 归调用方所有的下拉框
     * @details 「自动」就是编码前按偏好顺序逐个探的那条既有点；列出来的几个是设置页当场探通的，
     *          选中即钉死（真编码前还会按真实尺寸复核一次，探不过退回自动并把原因写进日志）。
     *          清单是异步拿的：先只摆「自动」，探测器回来再补候选并把她上次钉的那一档选中。
     */
    QComboBox *makeVideoEncoderCombo(ISettingsStore &settings, GyanisScribe::Record::FfmpegVideoEncoderEnumerator &enumerator)
    {
        auto *combo = new QComboBox;
        combo->addItem(GyanisScribe::Core::uiText("自动（逐个探测）"), QString::fromUtf8(std::string{GyanisScribe::Settings::kRecordVideoEncoderAutoToken}));
        combo->setAccessibleName(GyanisScribe::Core::uiText("视频编码器"));
        combo->setToolTip(GyanisScribe::Core::uiText("列出的是这台机器上前不久真探通的编码器；自动档会在每次编码前按偏好顺序探一遍"));

        const auto stored = settings.stringValue(GyanisScribe::Settings::kRecordVideoEncoder, QString::fromUtf8(std::string{GyanisScribe::Settings::kRecordVideoEncoderAutoToken}));
        QObject::connect(&enumerator, &GyanisScribe::Record::FfmpegVideoEncoderEnumerator::updated, combo,
                         [combo, stored](const QStringList &usable)
                         {
                             for (const auto &name: usable)
                             {
                                 combo->addItem(name, name);
                             }
                             if (const int index = combo->findData(stored); index >= 0)
                             {
                                 combo->setCurrentIndex(index);
                             } else if (!stored.isEmpty())
                             {
                                 Logger::warning(std::format("设置页列不出上次钉的编码器 {}：这次没探通，配置原样留着，编码时会按自动档走", stored.toStdString()));
                             }
                         });
        QObject::connect(&enumerator, &GyanisScribe::Record::FfmpegVideoEncoderEnumerator::failed, combo,
                         [combo](const GyanisScribe::Core::Error &error)
                         {
                             // 列不出来时只剩「自动」：原因挂在悬停提示上，用户不必翻日志才知道这里为什么没有候选
                             combo->setToolTip(QString::fromUtf8(error.message.c_str()));
                             Logger::warning(std::format("设置页列不出编码器清单：{}", error.message));
                         });
        enumerator.refresh();

        // 接线排在设初值之后：把界面摆到位不等于用户改过，否则一开设置页就把选择写回配置
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo](int)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kRecordVideoEncoder, combo->currentData().toString()); !written)
                             {
                                 Logger::warning(std::format("视频编码器没能写入：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }

    /**
     * @brief 按当前输出格式决定帧率那一格的天花板
     * @param formatCombo 输出格式下拉
     * @param frameRateSpin 帧率那一格
     * @param settings 配置存储
     * @details GIF 的上限比 MP4 低（阈值单源：Record::kMaximumGifFrameRate，与 validateRecorderConfig 同解）：
     *          选了 GIF 就把这一格的上限收到 24，不让用户配出一个到录制那一刻才被拒的组合。
     *          显示的夹取不写回配置——切回 MP4 要还回用户原来填的那一档；配置里那份仍由
     *          validateRecorderConfig 在开录前把关（手改配置绕过界面时它兜着）。
     */
    void applyFrameRateAvailability(const QComboBox &formatCombo, QSpinBox &frameRateSpin, ISettingsStore &settings)
    {
        const auto parsed = GyanisScribe::Record::parseRecordFormat(formatCombo.currentData().toString());
        const auto chosen = parsed ? *parsed : GyanisScribe::Record::RecorderConfig{}.format;
        const auto gif    = chosen == GyanisScribe::Record::OutputFormat::Gif;

        const auto maximum = gif ? GyanisScribe::Record::kMaximumGifFrameRate : GyanisScribe::Record::kMaximumFrameRate;
        const auto stored  = settings.intValue(GyanisScribe::Settings::kRecordDefaultFrameRate, GyanisScribe::Record::kDefaultFrameRate);

        const QSignalBlocker blocker{frameRateSpin};
        frameRateSpin.setMaximum(maximum);
        frameRateSpin.setToolTip(gif ? GyanisScribe::Core::uiText("GIF 最高 24 帧/秒：再高肉眼看不出来，两遍调色板还按帧数翻倍涨。切回 MP4 会恢复你原来那一档")
                                     : GyanisScribe::Core::uiText("录屏的目标帧率；GIF 档的上限比 MP4 低（24 帧/秒）"));
        frameRateSpin.setValue(gif ? std::min(stored, maximum) : stored);
    }

    /**
     * @brief 按当前输出格式决定两格取声可用不可用
     * @param formatCombo 输出格式下拉
     * @param microphoneCombo 麦克风那一格
     * @param systemSoundCombo 系统声音那一格
     * @details GIF 容器没有音轨，选了它就把两格关掉：判据与 RecordingService 跳过取声用的是同一句
     *          outputFormatCarriesAudio。不动 tooltip——列不出设备时原因正挂在 tooltip 上，覆盖它就挤掉了那条信息。
     */
    void applyAudioAvailability(const QComboBox &formatCombo, QComboBox &microphoneCombo, QComboBox &systemSoundCombo)
    {
        const auto parsed       = GyanisScribe::Record::parseRecordFormat(formatCombo.currentData().toString());
        const auto chosen       = parsed ? *parsed : GyanisScribe::Record::RecorderConfig{}.format;
        const auto carriesAudio = GyanisScribe::Record::outputFormatCarriesAudio(chosen);
        microphoneCombo.setEnabled(carriesAudio);
        systemSoundCombo.setEnabled(carriesAudio);
    }

    /**
     * @brief 录屏输出格式下拉：档位、配置里的取值与设置页标签都取自 Record::kRecordFormatChoices
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     * @details 格式定错比范围定错更贵：帧已经抓完才按错的容器压出去，整段成品作废。
     *          因此界面这一格读不出时只回落显示，开始录屏那一头是直接拒绝的。
     */
    QComboBox *makeRecordFormatCombo(ISettingsStore &settings)
    {
        auto *combo = new QComboBox;
        for (const auto &choice: GyanisScribe::Record::kRecordFormatChoices)
        {
            combo->addItem(GyanisScribe::Core::uiText(choice.label.data()), tokenText(choice.token));
        }
        combo->setAccessibleName(GyanisScribe::Core::uiText("输出格式"));
        // 上限那一个数取自 kMaximumGifFrameRate：写死了就会在阈值调那天变成一句假话
        combo->setToolTip(GyanisScribe::Core::uiText("GIF 走两遍调色板，帧率上限只有 %1 帧/秒，比 MP4 低；MP4 适合长片段。改完从下一次录屏开始生效")
                                  .arg(GyanisScribe::Record::kMaximumGifFrameRate));

        const auto defaultFormat = GyanisScribe::Record::RecorderConfig{}.format;
        const auto stored        = settings.stringValue(GyanisScribe::Settings::kRecordFormat, tokenText(GyanisScribe::Record::recordFormatText(defaultFormat)));
        const auto parsed        = GyanisScribe::Record::parseRecordFormat(stored);
        if (!parsed)
        {
            Logger::warning(std::format("设置页读不出输出格式：{}，下拉框按默认档显示", parsed.error().message));
        }
        const auto chosen = parsed ? *parsed : defaultFormat;
        combo->setCurrentIndex(combo->findData(tokenText(GyanisScribe::Record::recordFormatText(chosen))));

        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo](int)
                         {
                             const auto token = combo->currentData().toString();
                             if (auto written = settings.setValue(GyanisScribe::Settings::kRecordFormat, token); !written)
                             {
                                 Logger::warning(std::format("输出格式没能写入：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }
} // namespace

namespace GyanisScribe::App
{
    std::span<const PageChoice> pageChoices()
    {
        return kPages;
    }

    int indexOfMainWindowPage(const MainWindowPage page)
    {
        for (std::size_t index = 0; index < kPages.size(); ++index)
        {
            if (kPages[index].page == page)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    std::span<const ThemeChoice> themeChoices()
    {
        return kThemeChoices;
    }

    std::optional<ThemeKind> themeKindAt(const int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kThemeChoices.size())
        {
            return {};
        }
        return kThemeChoices[static_cast<std::size_t>(index)].kind;
    }

    int indexOfThemeKind(const ThemeKind themeKind)
    {
        for (std::size_t index = 0; index < kThemeChoices.size(); ++index)
        {
            if (kThemeChoices[index].kind == themeKind)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }


    std::optional<AppLanguage> appLanguageAt(const int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kLanguageChoices.size())
        {
            return {};
        }
        return kLanguageChoices[static_cast<std::size_t>(index)].language;
    }

    int indexOfAppLanguage(const AppLanguage language)
    {
        for (std::size_t index = 0; index < kLanguageChoices.size(); ++index)
        {
            if (kLanguageChoices[index].language == language)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    QString aboutText(const QString &version)
    {
        // 版本号由调用方从 QApplication::applicationVersion() 传进来：真源只有一处。
        // 整段是一条字面量：翻译目录按整段查，拆成相邻字面量会让目录里只收到第一片
        return GyanisScribe::Core::uiText("智录 GyanisScribe %1\n\n截图、标注、贴图、录屏与文字识别。\n全局热键：Ctrl+Alt+A 截图，Ctrl+Alt+R "
                                          "录屏（再按一次停止并保存），Ctrl+Alt+O 识别。\n界面图标全部为本项目原创 SVG；第三方组件与其许可证见仓库 docs/ThirdPartyLicenses.md。\n")
                .arg(version);
    }

    MainWindow::~MainWindow() = default;

    MainWindow::MainWindow(ISettingsStore &settings, ThemeManager &theme, IconProvider &icons, History::HistoryStore &historyStore, Platform::PlatformCapabilities capabilities,
                           Platform::IPlatformIntegration &integration, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_theme(theme), m_icons(icons), m_capabilities(capabilities), m_integration(integration),
        m_audioDevices(std::make_unique<GyanisScribe::Record::FfmpegAudioDeviceEnumerator>()),
        m_videoEncoders(std::make_unique<GyanisScribe::Record::FfmpegVideoEncoderEnumerator>()), m_historyStore(historyStore)
    {
        setWindowTitle(GyanisScribe::Core::uiText("智录 设置"));
        setWindowIcon(icons.icon("App"));
        // 根容器要自己铺底：样式表只给具名的根与卡片上色，不铺裸 QWidget
        setObjectName(QStringLiteral("appRoot"));
        setAttribute(Qt::WA_StyledBackground, true);

        // 根是竖的两段：上面一条常设动作条，下面才是「导航 | 页」这两列。
        // 动作条是给「没有托盘、热键也没登记上」的那一类机器留的常设入口，
        // 也是有托盘时不用先去右下角翻菜单的那条 shortcut。
        auto *root = new QVBoxLayout{this};
        root->setContentsMargins(GyanisScribe::Ui::kSpacingLarge, GyanisScribe::Ui::kSpacingLarge, GyanisScribe::Ui::kSpacingLarge, GyanisScribe::Ui::kSpacingLarge);
        root->setSpacing(GyanisScribe::Ui::kSpacingMedium);

        auto *layout = new QHBoxLayout;
        layout->setSpacing(GyanisScribe::Ui::kSpacingMedium);
        m_nav   = new QListWidget{this};
        m_stack = new QStackedWidget{this};
        m_nav->setObjectName(QStringLiteral("settingsNav"));
        m_nav->setFixedWidth(kNavColumnWidth);
        m_nav->setAccessibleName(GyanisScribe::Core::uiText("设置导航"));
        m_nav->setFrameShape(QFrame::NoFrame);

        // 导航行与页容器都在这一趟循环里生成：一处顺序，标签、图标与页内容不可能各自漂移
        for (const auto &choice: kPages)
        {
            auto *content = buildPage(choice.page);
            if (content == nullptr)
            {
                // 这一页还没有搭法：跳开这一行，导航与页容器都少一行，因此仍然同序
                continue;
            }
            decoratePage(*content, GyanisScribe::Core::uiText(choice.label.data()));
            auto *entry = new QListWidgetItem{m_icons.icon(choice.icon), GyanisScribe::Core::uiText(choice.label.data())};
            // 图标名留在行上：QIcon 是取色那一刻的快照，换主题时要按这个名字重取
            entry->setData(kNavIconNameRole, QString::fromUtf8(choice.icon));
            m_nav->addItem(entry);
            m_stack->addWidget(content);
        }

        // 换主题时重取图标：样式表与调色板都会自己跟上，只有已经发出去的 QIcon 需要按名字再取一次
        QObject::connect(&m_theme, &ThemeManager::themeChanged, this,
                         [this]
                         {
                             m_icons.refreshIcons(*this);
                             for (int row = 0; row < m_nav->count(); ++row)
                             {
                                 auto      *entry = m_nav->item(row);
                                 const auto name  = entry->data(kNavIconNameRole).toString();
                                 if (!name.isEmpty())
                                 {
                                     entry->setIcon(m_icons.icon(name.toStdString()));
                                 }
                             }
                         });

        QObject::connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
        m_nav->setCurrentRow(0);

        layout->addWidget(m_nav);
        layout->addWidget(m_stack, 1);
        root->addWidget(buildActionBar());
        root->addLayout(layout);
        resize(680, 560);
    }

    void MainWindow::open(const MainWindowPage page)
    {
        const auto index = indexOfMainWindowPage(page);
        if (index < 0)
        {
            // 页不在表里（比如新页忘了登记）就别切：切到第 0 页会把用户的操作读成「没反应」
            Logger::warning(std::format("主窗口没有这一页（枚举值 {}），已忽略这次跳转", static_cast<int>(page)));
            return;
        }
        // 两边都摆到位：导航已经停在这一行时 currentRowChanged 不再发信号，只设导航会让页容器留在原处
        m_nav->setCurrentRow(index);
        m_stack->setCurrentIndex(index);
        show();
        raise();
        activateWindow();
    }

    void MainWindow::exportSettingsToFile(const QString &filePath)
    {
        // 界面里的路径串换回路径要走 wstring：Windows 上 narrow 那一串按本地 ANSI 码页解释，
        // 中文目录名会被读成另一条不存在的路（Core::Paths::fromDisplayText 是同一条规则的唯一出处）
        const auto target   = Core::Paths::fromDisplayText(filePath);
        const auto exported = m_settings.exportToFile(target);
        if (!exported)
        {
            Logger::warning(std::format("配置导出失败：{}", exported.error().message));
            emit messageForUser(QString::fromUtf8(exported.error().message.c_str()), GyanisScribe::Ui::ToastKind::Danger);
            return;
        }

        // 条数从存储现取：导出的就是这一份的全部键，界面这层不再数一遍别的口径
        const auto count = static_cast<int>(m_settings.allKeys().size());
        Logger::info(std::format("配置导出完成：{} 项写到 {}", count, Core::Paths::toDisplayText(target).toStdString()));
        emit messageForUser(GyanisScribe::Core::uiText("已导出 %1 项设置").arg(count), GyanisScribe::Ui::ToastKind::Success);
    }

    void MainWindow::importSettingsFromFile(const QString &filePath)
    {
        const auto source   = Core::Paths::fromDisplayText(filePath);
        const auto imported = m_settings.importFromFile(source);
        if (!imported)
        {
            Logger::warning(std::format("配置导入失败：{}", imported.error().message));
            emit messageForUser(QString::fromUtf8(imported.error().message.c_str()), GyanisScribe::Ui::ToastKind::Danger);
            return;
        }

        // 逐项走的仍是 setValue，订阅配置的模块（热键、主题）已经收到通知；这里只把改动刷到盘上
        m_settings.sync();
        Logger::info(std::format("配置导入完成：{} 项来自 {}", *imported, Core::Paths::toDisplayText(source).toStdString()));
        emit messageForUser(GyanisScribe::Core::uiText("已导入 %1 项设置。这一页显示的还是导入前的值，请关掉设置窗口再重新打开；界面语言要重启智录才生效。").arg(*imported),
                            GyanisScribe::Ui::ToastKind::Success);
    }

    void MainWindow::exportPaletteToFile(const QString &filePath)
    {
        const auto palette = GyanisScribe::Annotation::annotationPalette(m_settings);
        if (!palette)
        {
            // 手改过的配置里那份坏名单：报原样原因，而不是拿内置那八格冒充「你存的色板」导出去
            Logger::warning(std::format("色板导出失败：{}", palette.error().message));
            emit messageForUser(QString::fromUtf8(palette.error().message.c_str()), GyanisScribe::Ui::ToastKind::Danger);
            return;
        }

        QFile file{filePath};
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            const auto reason = GyanisScribe::Core::uiText("色板没写进去：这个路径开不了。换一个能写的目录再试一次");
            Logger::warning(std::format("色板写不下去：{}（{}）", reason.toStdString(), filePath.toStdString()));
            emit messageForUser(reason, GyanisScribe::Ui::ToastKind::Danger);
            return;
        }
        const auto text = GyanisScribe::Annotation::paletteFileText(*palette);
        file.write(text.toUtf8());
        file.close();

        Logger::info(std::format("色板已导出：{} 格写到 {}", palette->size(), filePath.toStdString()));
        emit messageForUser(GyanisScribe::Core::uiText("色板已导出：%1 格").arg(static_cast<int>(palette->size())), GyanisScribe::Ui::ToastKind::Success);
    }

    void MainWindow::importPaletteFromFile(const QString &filePath)
    {
        QFile file{filePath};
        if (!file.open(QIODevice::ReadOnly))
        {
            const auto reason = GyanisScribe::Core::uiText("读不到这份色板文件：「%1」不存在或打不开。请确认路径，或重新导出一份").arg(filePath);
            Logger::warning(std::format("色板读不到：{}", filePath.toStdString()));
            emit messageForUser(reason, GyanisScribe::Ui::ToastKind::Danger);
            return;
        }
        const auto bytes  = file.readAll();
        const auto parsed = GyanisScribe::Annotation::parsePaletteFileText(QString::fromUtf8(bytes.constData(), static_cast<qsizetype>(bytes.size())));
        if (!parsed)
        {
            Logger::warning(std::format("色板导入被拒：{}", parsed.error().message));
            emit messageForUser(QString::fromUtf8(parsed.error().message.c_str()), GyanisScribe::Ui::ToastKind::Danger);
            return;
        }

        // 整份替换：追加会把两次导入变成两份重复色块；写坏的那一次保持旧名单，界面上不会出现「半份新色板」
        const auto written = m_settings.setValue(GyanisScribe::Settings::kAnnotationPalette, GyanisScribe::Annotation::paletteToTokens(*parsed));
        if (!written)
        {
            Logger::warning(std::format("色板没能写入：{}", written.error().message));
            emit messageForUser(QString::fromUtf8(written.error().message.c_str()), GyanisScribe::Ui::ToastKind::Danger);
            return;
        }
        m_settings.sync();
        Logger::info(std::format("色板已导入：{} 格来自 {}", parsed->size(), filePath.toStdString()));
        emit messageForUser(GyanisScribe::Core::uiText("色板已导入：%1 格。工具栏那一排在下一次打开标注窗时换成这一份。").arg(static_cast<int>(parsed->size())),
                            GyanisScribe::Ui::ToastKind::Success);
    }

    QWidget *MainWindow::buildTransferRow()
    {
        auto *row    = new QWidget{this};
        auto *layout = new QHBoxLayout{row};
        layout->setContentsMargins(0, 0, 0, 0);

        auto *exportButton = new QPushButton{GyanisScribe::Core::uiText("导出配置…"), row};
        m_icons.applyIcon(*exportButton, "Export");
        exportButton->setAccessibleName(GyanisScribe::Core::uiText("导出配置"));
        exportButton->setToolTip(GyanisScribe::Core::uiText("把当前全部设置写成一个 JSON 文件，换机器或备份时用"));

        auto *importButton = new QPushButton{GyanisScribe::Core::uiText("导入配置…"), row};
        m_icons.applyIcon(*importButton, "Import");
        importButton->setAccessibleName(GyanisScribe::Core::uiText("导入配置"));
        importButton->setToolTip(GyanisScribe::Core::uiText("从本程序导出的 JSON 文件读回设置；订阅配置的热键与主题立刻跟上新值"));

        // 对话框取消时 picked 是空串：那不是失败，一句话都不多说
        QObject::connect(exportButton, &QPushButton::clicked, row,
                         [this]
                         {
                             const auto suggested = GyanisScribe::Core::Paths::toDisplayText(Core::Paths::applicationDataDirectory() / "GyanisScribe-settings.json");
                             const auto picked =
                                     QFileDialog::getSaveFileName(this, GyanisScribe::Core::uiText("导出配置"), suggested, GyanisScribe::Core::uiText("配置文件 (*.json)"));
                             if (picked.isEmpty())
                             {
                                 return;
                             }
                             exportSettingsToFile(picked);
                         });
        QObject::connect(importButton, &QPushButton::clicked, row,
                         [this]
                         {
                             const auto picked =
                                     QFileDialog::getOpenFileName(this, GyanisScribe::Core::uiText("导入配置"), QDir::homePath(), GyanisScribe::Core::uiText("配置文件 (*.json)"));
                             if (picked.isEmpty())
                             {
                                 return;
                             }
                             importSettingsFromFile(picked);
                         });

        // 两个按钮要交给布局管：只做父子挂载而不 addWidget，这一行的 sizeHint 就是 0，
        // QFormLayout 会按标签的高度给这一行 15 像素，按钮被裁掉大半
        layout->addWidget(exportButton);
        layout->addWidget(importButton);
        layout->addStretch();
        return row;
    }

    QWidget *MainWindow::buildPage(const MainWindowPage page)
    {
        switch (page)
        {
            case MainWindowPage::General:
                return buildGeneralPage();
            case MainWindowPage::Capture:
                return buildCapturePage();
            case MainWindowPage::Annotation:
                return buildAnnotationPage();
            case MainWindowPage::Pin:
                return buildPinPage();
            case MainWindowPage::Record:
                return buildRecordPage();
            case MainWindowPage::Ocr:
                return buildOcrPage();
            case MainWindowPage::Hotkey:
                return buildHotkeyPage();
            case MainWindowPage::Theme:
                return buildThemePage();
            case MainWindowPage::About:
                return buildAboutPage();
            case MainWindowPage::History:
                return buildHistoryPage();
        }
        return nullptr;
    }

    QWidget *MainWindow::buildActionBar()
    {
        auto *bar = new QWidget{this};
        bar->setObjectName(QStringLiteral("actionBar"));
        auto *row = new QHBoxLayout{bar};
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(GyanisScribe::Ui::kSpacingSmall);

        auto *moreMenu = new QMenu{bar};
        for (const auto &entry: GyanisScribe::App::visibleTrayMenuEntries(m_capabilities))
        {
            const auto action = entry.action;
            if (!action)
            {
                // 表里的分隔线在这一条上没有对应物：分格由行距自己管
                continue;
            }
            if (*action == TrayAction::OpenSettings || *action == TrayAction::OpenAbout || *action == TrayAction::Quit)
            {
                // 这三条说的就是这扇窗口：设置与关于是它的内容，退出在没有托盘的机器上是「关掉窗子」
                continue;
            }
            if (entry.hotkeyKey.empty())
            {
                // 没配全局热键的那几条收进「更多」：主操作摆在明面，次要的也不许找不到
                auto *item = moreMenu->addAction(entry.label);
                QObject::connect(item, &QAction::triggered, this, [this, action] { emit actionRequested(*action); });
                continue;
            }
            auto *button = new QPushButton{entry.label, bar};
            m_icons.applyIcon(*button, entry.iconName);
            button->setToolTip(GyanisScribe::Core::uiText("与托盘菜单、全局热键走同一个出口"));
            QObject::connect(button, &QPushButton::clicked, this, [this, action] { emit actionRequested(*action); });
            row->addWidget(button);
        }

        auto *more = new QToolButton{bar};
        more->setText(GyanisScribe::Core::uiText("更多"));
        more->setPopupMode(QToolButton::InstantPopup);
        more->setMenu(moreMenu);
        row->addWidget(more);
        row->addStretch();
        return bar;
    }

    /**
     * @brief 开机自启动那一格：状态从系统里现读，失败就不给点并把原因写进提示
     * @details 不在配置里另存一份状态——两份真源迟早对不上（用户在系统「启动」设置里手动改过之后，
     *          配置里那个勾就会说谎）。读不到就问平台为什么不支持，把它的原话摆进提示里。
     */
    QCheckBox *makeAutostartCheck(Platform::IPlatformIntegration &integration)
    {
        auto *check = new QCheckBox;
        check->setText(GyanisScribe::Core::uiText("开机时自动启动"));
        check->setAccessibleName(GyanisScribe::Core::uiText("开机时自动启动"));

        const auto state = integration.autostartEnabled();
        if (!state)
        {
            check->setEnabled(false);
            check->setToolTip(QString::fromStdString(state.error().message));
            return check;
        }

        check->setChecked(*state);
        check->setToolTip(GyanisScribe::Core::uiText("开机时自动起这一份程序；状态读的是系统里的启动项，改完立即生效"));

        // 接线排在设初值之后：摆到位不等于用户改过，否则一开设置页就把状态写回一次
        QObject::connect(check, &QCheckBox::toggled, check,
                         [check, &integration](const bool enabled)
                         {
                             if (const auto applied = integration.setAutostart(enabled); !applied)
                             {
                                 Core::Logger::warning(std::format("开机启动项没能改：{}", applied.error().message));
                                 check->setToolTip(QString::fromStdString(applied.error().message));
                                 // 写失败就把勾摆回真实状态：挂着假勾，用户以为下次开机就会起来
                                 QSignalBlocker blocker{check};
                                 check->setChecked(!enabled);
                             }
                         });
        return check;
    }

    /**
     * @brief 「分组轮换顺序」那一格：逗号分隔的正整数组号，留空即按组号升序
     * @details 填歪的名单不写进配置：轮换顺序错了只是切组绕远，而把坏值存下去会让下一次启动也读不通，
     *          那时用户已经离开这一格了，说不清是谁改坏的。
     */
    QLineEdit *makeGroupRotationEdit(ISettingsStore &settings)
    {
        auto *edit = new QLineEdit;
        edit->setText(settings.stringListValue(GyanisScribe::Settings::kPinGroupRotationOrder, {}).join(QStringLiteral(", ")));
        edit->setPlaceholderText(GyanisScribe::Core::uiText("留空即按组号升序，例如 2, 1, 3"));
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Core::uiText("分组轮换顺序"));

        const auto explainHint = [edit](const QString &reason)
        { edit->setToolTip(reason.isEmpty() ? GyanisScribe::Core::uiText("切组按键走的那一圈按这里的顺序排，没写到的组补在末尾") : reason); };
        explainHint(QString{});

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [&settings, edit, explainHint]
                         {
                             const auto tokens = edit->text().split(QChar{','}, Qt::SkipEmptyParts);
                             const auto parsed = GyanisScribe::Pin::parseGroupRotation(tokens);
                             if (!parsed)
                             {
                                 explainHint(QString::fromUtf8(parsed.error().message));
                                 return;
                             }
                             QStringList canonical;
                             canonical.reserve(static_cast<int>(parsed->size()));
                             for (const auto group: *parsed)
                             {
                                 canonical << QString::number(group);
                             }
                             if (const auto written = settings.setValue(GyanisScribe::Settings::kPinGroupRotationOrder, canonical); !written)
                             {
                                 explainHint(QString::fromUtf8(written.error().message));
                                 return;
                             }
                             explainHint(QString{});
                         });
        return edit;
    }

    QWidget *MainWindow::buildGeneralPage()
    {
        auto *page = new QWidget{this};
        auto *form =
                makePageForm(page, GyanisScribe::Core::uiText("界面语言决定菜单、托盘提示与这些设置页的措辞，切换后要重启智录才生效。下面两格管历史留多少，填到 0 就是不限。"));
        form->addRow(GyanisScribe::Core::uiText("界面语言"), makeUiLanguageCombo(m_settings));
        // 保留策略的读端在 History::policyFromSettings，而那条负数提示写着「请在设置的通用页改回来」——
        // 这一页没有这两格时，那句提示就是把用户指到一个不存在的地方
        form->addRow(GyanisScribe::Core::uiText("历史最多留多少条"),
                     makeOptionalLimitSpin(m_settings, GyanisScribe::Settings::kHistoryMaximumEntries, GyanisScribe::Core::uiText("历史最多留多少条"),
                                           GyanisScribe::Core::uiText(" 条"), kMaximumEntriesLimit, GyanisScribe::History::kDefaultMaximumEntries));
        form->addRow(GyanisScribe::Core::uiText("历史最久留多少天"),
                     makeOptionalLimitSpin(m_settings, GyanisScribe::Settings::kHistoryRetentionDays, GyanisScribe::Core::uiText("历史最久留多少天"),
                                           GyanisScribe::Core::uiText(" 天"), kRetentionDaysLimit, GyanisScribe::History::kDefaultRetentionDays));
        form->addRow(GyanisScribe::Core::uiText("未保存的截图也进历史"),
                     makeSettingCheck(m_settings, GyanisScribe::Settings::kHistoryKeepUnsaved, false, GyanisScribe::Core::uiText("未保存的截图也进历史"),
                                      GyanisScribe::Core::uiText("未保存的截图也进历史"),
                                      QString::fromUtf8(
                                              GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8(
                                                                                   "勾上后每张截图都在程序数据目录里留一份像素，误触 Esc 丢掉的那一张也还在历史里（最多留 {} 张）"),
                                                                           static_cast<int>(GyanisScribe::History::kUnsavedArchiveCapacity))
                                                      .c_str())));
        form->addRow(GyanisScribe::Core::uiText("分组轮换顺序"), makeGroupRotationEdit(m_settings));
        form->addRow(GyanisScribe::Core::uiText("开机自启动"), makeAutostartCheck(m_integration));
        form->addRow(GyanisScribe::Core::uiText("备份与恢复"), buildTransferRow());
        return page;
    }

    /**
     * @brief 成品文件名模板那一格：留空即默认档，填歪的不写进配置
     * @details 填歪了还写进去，只会在下一次保存时静默回落到默认档，用户反倒以为模板生效了；
     *          这里把原因写进 tooltip 并保留输入框内容，让他能就地改。
     */
    QLineEdit *makeNameTemplateEdit(ISettingsStore &settings)
    {
        auto *edit = new QLineEdit;
        edit->setText(settings.stringValue(GyanisScribe::Settings::kSaveNameTemplate, QString{}));
        edit->setPlaceholderText(GyanisScribe::Core::Paths::defaultNameTemplate());
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Core::uiText("文件名模板"));

        const auto explainHint = [edit](const QString &reason)
        {
            edit->setToolTip(reason.isEmpty() ? GyanisScribe::Core::uiText("可用变量：%date%（日期）、%time%（时刻）、%ms%（毫秒）；留空表示用默认档。改动后从下一次保存起生效")
                                              : reason);
        };
        explainHint(QString{});

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [edit, &settings, explainHint]
                         {
                             const auto text = edit->text();
                             if (text.trimmed().isEmpty())
                             {
                                 // 留空是「用默认档」这一档，不是错误：清掉那一格即可
                                 static_cast<void>(settings.setValue(GyanisScribe::Settings::kSaveNameTemplate, QString{}));
                                 explainHint(QString{});
                                 return;
                             }

                             const auto parsed = GyanisScribe::Core::Paths::parseNameTemplate(text);
                             if (!parsed)
                             {
                                 explainHint(QString::fromStdString(parsed.error().message));
                                 return;
                             }
                             if (const auto written = settings.setValue(GyanisScribe::Settings::kSaveNameTemplate, *parsed); !written)
                             {
                                 Core::Logger::warning(std::format("文件名模板没能写进配置：{}", written.error().message));
                             }
                             explainHint(QString{});
                         });
        return edit;
    }

    /**
     * @brief 成品边框的宽度，物理像素
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     */
    QSpinBox *makeBorderWidthSpin(ISettingsStore &settings)
    {
        auto *spin = new QSpinBox;
        spin->setRange(GyanisScribe::Core::kMinimumOutputBorderPixels, GyanisScribe::Core::kMaximumOutputBorderPixels);
        spin->setSuffix(GyanisScribe::Core::uiText(" 像素"));
        spin->setAccessibleName(GyanisScribe::Core::uiText("边框宽度"));
        spin->setToolTip(GyanisScribe::Core::uiText("开着「成品加边框」时那圈框的宽度（物理像素）：只对离开程序的成品生效，贴到屏幕上那一张不加"));

        const auto title  = GyanisScribe::Core::uiText("边框宽度");
        const auto stored = settings.intValue(GyanisScribe::Settings::kOutputBorderWidth, GyanisScribe::Core::kOutputBorderPixels);
        const auto parsed = GyanisScribe::Core::parseOutputBorderPixels(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Core::kOutputBorderPixels));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kOutputBorderWidth, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 成品投影向外延伸的宽度，物理像素
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     */
    QSpinBox *makeShadowWidthSpin(ISettingsStore &settings)
    {
        auto *spin = new QSpinBox;
        spin->setRange(GyanisScribe::Core::kMinimumOutputShadowPixels, GyanisScribe::Core::kMaximumOutputShadowPixels);
        spin->setSuffix(GyanisScribe::Core::uiText(" 像素"));
        spin->setAccessibleName(GyanisScribe::Core::uiText("投影宽度"));
        spin->setToolTip(GyanisScribe::Core::uiText("开着「成品加投影」时投影向外铺多宽（物理像素）：宽度越大越柔，也越占地方"));

        const auto title  = GyanisScribe::Core::uiText("投影宽度");
        const auto stored = settings.intValue(GyanisScribe::Settings::kOutputShadowWidth, GyanisScribe::Core::kOutputShadowPixels);
        const auto parsed = GyanisScribe::Core::parseOutputShadowPixels(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Core::kOutputShadowPixels));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Settings::kOutputShadowWidth, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 成品边框的颜色：一段 #RRGGBB 文本
     * @param settings 配置存储
     * @return 归调用方所有的输入框
     * @details 与文件名模板同一套纪律：写法不对就不写进配置，把原因挂在 tooltip 上让他就地改——
     *          存进去只会在下一次输出时静默回落成中灰，那种「改了没生效」最难查。留空是默认档。
     */
    QLineEdit *makeBorderColorEdit(ISettingsStore &settings)
    {
        auto *edit = new QLineEdit;
        edit->setText(settings.stringValue(GyanisScribe::Settings::kOutputBorderColor, QString{}));
        edit->setPlaceholderText(GyanisScribe::Core::uiText("#808080（中灰，留空即这一档）"));
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Core::uiText("边框颜色"));

        const auto explainHint = [edit](const QString &reason)
        { edit->setToolTip(reason.isEmpty() ? GyanisScribe::Core::uiText("成品那圈边框的颜色，写 #RRGGBB；留空回到默认的中灰") : reason); };
        explainHint(QString{});

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [&settings, edit, explainHint]
                         {
                             const auto text   = edit->text();
                             const auto parsed = GyanisScribe::Core::parseOutputBorderColor(text);
                             if (!parsed)
                             {
                                 explainHint(QString::fromUtf8(parsed.error().message));
                                 return;
                             }
                             if (const auto written = settings.setValue(GyanisScribe::Settings::kOutputBorderColor, text.trimmed()); !written)
                             {
                                 explainHint(QString::fromUtf8(written.error().message));
                                 return;
                             }
                             explainHint(QString{});
                         });
        return edit;
    }

    /**
     * @brief 截图成功后起跑的那一条外部命令
     * @param settings 配置存储
     * @return 归调用方所有的输入框
     * @details 写入时不设校验：这一串对不对，只有到真的起跑那一刻才知道（程序在不在场、`%file%` 有没有成品可换），
     *          而拆不开或起不来都会把中文原因报出来并落日志。在这里拦只拦得住「看着像空的」那一格，
     *          反而会给用户「填了就该立刻报错」的错觉。
     */
    QLineEdit *makeExecCommandEdit(ISettingsStore &settings)
    {
        auto *edit = new QLineEdit;
        edit->setText(settings.stringValue(GyanisScribe::Settings::kCaptureExecCommand, QString{}));
        edit->setPlaceholderText(GyanisScribe::Core::uiText("留空即不跑；形如 上传工具 --path \"%file%\""));
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Core::uiText("截图后运行命令"));
        edit->setToolTip(GyanisScribe::Core::uiText("每次截图交付完之后起跑这一串，参数里的 %file% 换成这一轮落盘的成品路径。"
                                                    "这一轮没落成文件时（「截完之后」选了进标注窗等）带 %file% 的命令会被拒并说明原因；"
                                                    "命令行给了 --exec 时以那一条为准，这一格不参与那一次。"));

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [&settings, edit]
                         {
                             if (const auto written = settings.setValue(GyanisScribe::Settings::kCaptureExecCommand, edit->text().trimmed()); !written)
                             {
                                 Core::Logger::warning(std::format("截图后运行命令没能写入：{}", written.error().message));
                             }
                         });
        return edit;
    }

    QWidget *MainWindow::buildCapturePage()
    {
        auto *page = new QWidget{this};
        auto *form = makePageForm(page, GyanisScribe::Core::uiText("截图与标注成品落在这个目录里。取色复制出来的文本按下面的写法格式化。"));
        form->addRow(GyanisScribe::Core::uiText("截完之后"), makeCaptureIntentCombo(m_settings, m_icons));
        form->addRow(GyanisScribe::Core::uiText("延时截图"), makeCaptureDelaySpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("鼠标指针"), makeIncludeCursorCheck(m_settings));
        form->addRow(GyanisScribe::Core::uiText("圆角截图"),
                     makeSettingCheck(m_settings, Settings::kCaptureRoundCorners, Settings::kDefaultCaptureRoundCorners, GyanisScribe::Core::uiText("成品四角切圆"),
                                      GyanisScribe::Core::uiText("圆角截图"),
                                      GyanisScribe::Core::uiText("区域截图生效：把成品四角切圆，角上是透明像素；切多圆由下面那一格定；整屏、窗口与滚动长截图不带")));
        form->addRow(GyanisScribe::Core::uiText("圆角半径"), makeCornerRadiusSpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("自动保存"),
                     makeSettingCheck(m_settings, GyanisScribe::Settings::kCaptureAutoSave, false, GyanisScribe::Core::uiText("每张截图都先存一份进上面的目录"),
                                      GyanisScribe::Core::uiText("自动保存所有截图"),
                                      GyanisScribe::Core::uiText("截图完成时立即按上面的格式与目录存一份，不弹提示；「截完之后」已经选了「存成文件」或「快速保存」时不重复存")));
        form->addRow(GyanisScribe::Core::uiText("截图音效"),
                     makeSettingCheck(m_settings, GyanisScribe::Settings::kCaptureSound, false, GyanisScribe::Core::uiText("截图完成时响一声系统提示音"),
                                      GyanisScribe::Core::uiText("截图音效"), GyanisScribe::Core::uiText("用系统的提示音，不需要放音频文件")));
        form->addRow(GyanisScribe::Core::uiText("截图后运行命令"), makeExecCommandEdit(m_settings));
        form->addRow(GyanisScribe::Core::uiText("成品加边框"),
                     makeSettingCheck(m_settings, GyanisScribe::Settings::kOutputBorder, false, GyanisScribe::Core::uiText("出程序时围一圈中灰边框"),
                                      GyanisScribe::Core::uiText("成品加边框"),
                                      GyanisScribe::Core::uiText("存文件、交剪贴板与打印都加上；贴到屏幕上那一张不加——那是显示，不是输出")));
        form->addRow(GyanisScribe::Core::uiText("边框宽度"), makeBorderWidthSpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("边框颜色"), makeBorderColorEdit(m_settings));
        form->addRow(GyanisScribe::Core::uiText("成品加投影"),
                     makeSettingCheck(m_settings, GyanisScribe::Settings::kOutputShadow, false, GyanisScribe::Core::uiText("出程序时带一圈轻投影"),
                                      GyanisScribe::Core::uiText("成品加投影"), GyanisScribe::Core::uiText("与边框同一组：存文件、交剪贴板与打印都加上；存 JPG 时投影底下垫白")));
        form->addRow(GyanisScribe::Core::uiText("投影宽度"), makeShadowWidthSpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("截图与标注成品目录"),
                     makePathRow(m_settings, GyanisScribe::Settings::kPathScreenshot, GyanisScribe::Core::uiText("截图目录"), Core::Paths::defaultScreenshotDirectory(), m_icons));
        form->addRow(GyanisScribe::Core::uiText("保存格式"), makeImageFormatCombo(m_settings));
        form->addRow(GyanisScribe::Core::uiText("保存质量"), makeImageQualitySpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("文件名模板"), makeNameTemplateEdit(m_settings));
        form->addRow(GyanisScribe::Core::uiText("复制用的写法"), makeColorFormatCombo(m_settings));
        return page;
    }

    /**
     * @brief 自定义色板那一格：逗号分隔的 #RRGGBB 清单
     * @param settings 配置存储
     * @return 归调用方所有的输入框
     * @details 留空就是「不自定义了」：存一份空名单，读端回落到内置那八格——清空与填成坏值不是一回事，
     *          后者要当场点名并保住上一份好值（工具栏摆的是用户存的那排颜色，存歪着切过去会少格）。
     */
    QLineEdit *makePaletteEdit(ISettingsStore &settings)
    {
        auto *edit = new QLineEdit;
        edit->setText(settings.stringListValue(GyanisScribe::Settings::kAnnotationPalette, {}).join(QStringLiteral(", ")));
        edit->setPlaceholderText(GyanisScribe::Core::uiText("留空即内置那八格；形如 #FF0000, #00FF00"));
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Core::uiText("自定义色板"));

        const auto explainHint = [edit](const QString &reason)
        { edit->setToolTip(reason.isEmpty() ? GyanisScribe::Core::uiText("色板上摆的那几格，这里的顺序就是从左到右") : reason); };
        explainHint(QString{});

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [&settings, edit, explainHint]
                         {
                             const auto text = edit->text().trimmed();
                             if (text.isEmpty())
                             {
                                 if (const auto written = settings.setValue(GyanisScribe::Settings::kAnnotationPalette, QStringList{}); !written)
                                 {
                                     explainHint(QString::fromUtf8(written.error().message.c_str()));
                                     return;
                                 }
                                 explainHint(QString{});
                                 return;
                             }

                             QStringList tokens;
                             for (const auto &piece: text.split(QLatin1Char(',')))
                             {
                                 const auto one = piece.trimmed();
                                 if (!one.isEmpty())
                                 {
                                     tokens << one;
                                 }
                             }
                             const auto parsed = GyanisScribe::Annotation::paletteFromTokens(tokens);
                             if (!parsed)
                             {
                                 explainHint(QString::fromUtf8(parsed.error().message.c_str()));
                                 return;
                             }
                             if (const auto written = settings.setValue(GyanisScribe::Settings::kAnnotationPalette, GyanisScribe::Annotation::paletteToTokens(*parsed)); !written)
                             {
                                 explainHint(QString::fromUtf8(written.error().message.c_str()));
                                 return;
                             }
                             explainHint(QString{});
                         });
        return edit;
    }

    QWidget *MainWindow::buildPaletteTransferRow()
    {
        auto *row    = new QWidget{this};
        auto *layout = new QHBoxLayout{row};
        layout->setContentsMargins(0, 0, 0, 0);

        auto *exportButton = new QPushButton{GyanisScribe::Core::uiText("导出色板…"), row};
        m_icons.applyIcon(*exportButton, "Export");
        exportButton->setAccessibleName(GyanisScribe::Core::uiText("导出色板"));
        exportButton->setToolTip(GyanisScribe::Core::uiText("把现在用的那排颜色写成一个文本文件（一行一格 #RRGGBB），发给同事或换机器时用"));

        auto *importButton = new QPushButton{GyanisScribe::Core::uiText("导入色板…"), row};
        m_icons.applyIcon(*importButton, "Import");
        importButton->setAccessibleName(GyanisScribe::Core::uiText("导入色板"));
        importButton->setToolTip(GyanisScribe::Core::uiText("从一份色板文件整份替换当前这一排；下一次打开标注窗就是这一份"));

        // 对话框取消时 picked 是空串：那不是失败，一句话都不多说
        QObject::connect(exportButton, &QPushButton::clicked, row,
                         [this]
                         {
                             const auto suggested = GyanisScribe::Core::Paths::toDisplayText(Core::Paths::applicationDataDirectory() / "GyanisScribe-palette.txt");
                             const auto picked =
                                     QFileDialog::getSaveFileName(this, GyanisScribe::Core::uiText("导出色板"), suggested, GyanisScribe::Core::uiText("色板文件 (*.txt)"));
                             if (picked.isEmpty())
                             {
                                 return;
                             }
                             exportPaletteToFile(picked);
                         });
        QObject::connect(importButton, &QPushButton::clicked, row,
                         [this]
                         {
                             const auto picked =
                                     QFileDialog::getOpenFileName(this, GyanisScribe::Core::uiText("导入色板"), QDir::homePath(), GyanisScribe::Core::uiText("色板文件 (*.txt)"));
                             if (picked.isEmpty())
                             {
                                 return;
                             }
                             importPaletteFromFile(picked);
                         });

        layout->addWidget(exportButton);
        layout->addWidget(importButton);
        return row;
    }

    QWidget *MainWindow::buildAnnotationPage()
    {
        auto *page = new QWidget{this};
        auto *form = makePageForm(page, GyanisScribe::Core::uiText("新打开的标注窗按这两档起步，画过之后的改动会被工具栏记下来，下次仍以最近用的那一档开始。"
                                                                   "色板那一排也可以换成自己的一套：留空就是内置那八格，下面两个按钮负责把一份配色发给同事或收回来。"));
        form->addRow(GyanisScribe::Core::uiText("默认线宽"),
                     makeStepCombo(m_settings, GyanisScribe::Settings::kAnnotationDefaultWidth, GyanisScribe::Ui::kAnnotationWidths,
                                   GyanisScribe::Ui::kAnnotationWidths[GyanisScribe::Ui::kDefaultAnnotationWidthStep], GyanisScribe::Core::uiText("默认线宽")));
        form->addRow(GyanisScribe::Core::uiText("默认字号"),
                     makeStepCombo(m_settings, GyanisScribe::Settings::kAnnotationDefaultFontSize, GyanisScribe::Ui::kFontSizes,
                                   GyanisScribe::Ui::kFontSizes[GyanisScribe::Ui::kDefaultAnnotationFontSizeIndex], GyanisScribe::Core::uiText("默认字号")));
        form->addRow(GyanisScribe::Core::uiText("自定义色板"), makePaletteEdit(m_settings));
        form->addRow(GyanisScribe::Core::uiText("分享色板"), buildPaletteTransferRow());
        return page;
    }

    QWidget *MainWindow::buildPinPage()
    {
        auto *page = new QWidget{this};
        auto *form = makePageForm(page, GyanisScribe::Core::uiText("新贴到屏幕上的图按这个不透明度开始。滚轮调透明度只影响那一张贴图，不改这里。"));
        form->addRow(GyanisScribe::Core::uiText("新建贴图的默认不透明度"), makeOpacitySpin(m_settings));
        return page;
    }

    QWidget *MainWindow::buildRecordPage()
    {
        auto *page = new QWidget{this};
        auto *form =
                makePageForm(page, GyanisScribe::Core::uiText("录屏成品落在这个目录里。录制范围选「上次选区」或「当前窗口」时，画面里就只有那一片。存成 GIF "
                                                              "时帧率上限比 MP4 低（选了 GIF 那一格只到 24）。画面码率填 0 就交给编码器的质量档，给了具体数值就按那个码率控，GIF "
                                                              "用不上这一项。编码器可以钉住某一家，默认自动就是编码前逐个真探、挑第一个开得起来的。倒计时 0 表示立即开始。"
                                                              "取声那两格列不出设备时只剩「不取这一路」，把鼠标停在框上能看到原因；存成 GIF "
                                                              "时这两格会灰掉，那种文件带不了声音。这几项都从下一次开始录屏时生效。"));
        form->addRow(GyanisScribe::Core::uiText("录屏成品目录"),
                     makePathRow(m_settings, GyanisScribe::Settings::kPathRecording, GyanisScribe::Core::uiText("录屏目录"), Core::Paths::defaultRecordingDirectory(), m_icons));
        form->addRow(GyanisScribe::Core::uiText("录制范围"), makeRecordScopeCombo(m_settings, m_icons));
        auto *formatCombo = makeRecordFormatCombo(m_settings);
        form->addRow(GyanisScribe::Core::uiText("输出格式"), formatCombo);
        auto *frameRateSpin = makeFrameRateSpin(m_settings);
        form->addRow(GyanisScribe::Core::uiText("默认帧率"), frameRateSpin);
        form->addRow(GyanisScribe::Core::uiText("倒计时"), makeCountdownSpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("画面码率"), makeVideoBitrateSpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("视频编码器"), makeVideoEncoderCombo(m_settings, *m_videoEncoders));
        form->addRow(GyanisScribe::Core::uiText("光标高亮"), makeCursorHighlightCheck(m_settings, m_icons));
        auto *microphoneCombo  = makeAudioDeviceCombo(m_settings, GyanisScribe::Settings::kRecordAudioMicrophone, GyanisScribe::Core::uiText("麦克风设备"), *m_audioDevices);
        auto *systemSoundCombo = makeAudioDeviceCombo(m_settings, GyanisScribe::Settings::kRecordAudioSystemSound, GyanisScribe::Core::uiText("系统声音设备"), *m_audioDevices);
        form->addRow(GyanisScribe::Core::uiText("麦克风这一路"), microphoneCombo);
        form->addRow(GyanisScribe::Core::uiText("系统声音这一路"), systemSoundCombo);

        // 选到 GIF 就把取声那两格关掉、把帧率那一格的天花板收到 24：判据与 RecordingService 跳过取声
        // 用的是同一句，帧率上限与 validateRecorderConfig 同源（kMaximumGifFrameRate 一处真源）
        QObject::connect(formatCombo, &QComboBox::currentIndexChanged, formatCombo,
                         [this, formatCombo, frameRateSpin, microphoneCombo, systemSoundCombo](int)
                         {
                             applyAudioAvailability(*formatCombo, *microphoneCombo, *systemSoundCombo);
                             applyFrameRateAvailability(*formatCombo, *frameRateSpin, m_settings);
                         });
        applyAudioAvailability(*formatCombo, *microphoneCombo, *systemSoundCombo);
        applyFrameRateAvailability(*formatCombo, *frameRateSpin, m_settings);
        return page;
    }

    QWidget *MainWindow::buildOcrPage()
    {
        auto *page = new QWidget{this};
        auto *form = makePageForm(page, GyanisScribe::Core::uiText("认得出哪种文字取决于语言包。目录留空时按环境变量与程序数据目录找。"));
        form->addRow(GyanisScribe::Core::uiText("识别语言"), makeOcrLanguageCombo(m_settings));
        form->addRow(GyanisScribe::Core::uiText("识别语言包目录"), makePathRow(m_settings, GyanisScribe::Settings::kOcrTessDataDirectory, GyanisScribe::Core::uiText("语言包目录"),
                                                                               Core::Paths::applicationDataDirectory() / "tessdata", m_icons));
        return page;
    }

    QWidget *MainWindow::buildHotkeyPage()
    {
        auto *page = new QWidget{this};
        // 没有登记接口的那一种平台上，改组合键不会有任何效果：入口灰掉并当场说明，
        // 比让人反复改完再按一次、把「没反应」当成程序坏了要好
        // 一行一个动作；条数由这份表自己数出来：文案里那个数字与热键登记表必须是同一个源
        const auto rows = std::array{std::tuple{GyanisScribe::Settings::kHotkeyCapture, GyanisScribe::Core::uiText("截图"), QStringLiteral("Ctrl+Alt+A")},
                                     std::tuple{GyanisScribe::Settings::kHotkeyRecord, GyanisScribe::Core::uiText("录屏"), QStringLiteral("Ctrl+Alt+R")},
                                     std::tuple{GyanisScribe::Settings::kHotkeyOcr, GyanisScribe::Core::uiText("文字识别"), QStringLiteral("Ctrl+Alt+O")},
                                     std::tuple{GyanisScribe::Settings::kHotkeyPick, GyanisScribe::Core::uiText("屏幕取色"), QStringLiteral("Ctrl+Alt+P")},
                                     std::tuple{GyanisScribe::Settings::kHotkeyCaptureLong, GyanisScribe::Core::uiText("滚动长截图"), QStringLiteral("Ctrl+Alt+L")},
                                     std::tuple{GyanisScribe::Settings::kHotkeyQuickSave, GyanisScribe::Core::uiText("快速保存"), QStringLiteral("Ctrl+Alt+S")},
                                     std::tuple{GyanisScribe::Settings::kHotkeyTogglePins, GyanisScribe::Core::uiText("收起 / 摆回贴图"), QStringLiteral("Ctrl+Alt+H")},
                                     std::tuple{GyanisScribe::Settings::kHotkeyNextPinGroup, GyanisScribe::Core::uiText("切换贴图分组"), QStringLiteral("Ctrl+Alt+G")}};

        const auto intro = m_capabilities.globalHotkeys
                                   ? GyanisScribe::Core::uiText("%1 个全局热键随时可改，改完立即重新登记，不需要重启。").arg(static_cast<int>(rows.size()))
                                   : GyanisScribe::Core::uiText("这个平台没有登记全局热键的接口，下面 %1 项改了也不会生效。").arg(static_cast<int>(rows.size()));
        auto      *form  = makePageForm(page, intro);
        for (const auto [key, title, defaultText]: rows)
        {
            auto *edit = new GyanisScribe::Ui::HotkeyEdit{m_settings, page};
            edit->bindTo(key, title, defaultText);
            edit->setEnabled(m_capabilities.globalHotkeys);
            form->addRow(GyanisScribe::Core::uiText("%1 的全局热键").arg(title), edit);
        }
        return page;
    }

    QWidget *MainWindow::buildThemePage()
    {
        auto *page = new QWidget{this};
        auto *form = makePageForm(page, GyanisScribe::Core::uiText("配色方案即时生效。高对比度档是纯黑正文、纯白底与加粗边框。"));
        form->addRow(GyanisScribe::Core::uiText("配色方案"), makeThemeCombo(m_theme));
        return page;
    }

    QWidget *MainWindow::buildHistoryPage()
    {
        auto *page  = new QWidget{this};
        auto *inner = new QVBoxLayout{page};
        m_history   = new History::HistoryView{m_historyStore, History::policyFromSettings(m_settings), m_icons, page};
        inner->addWidget(m_history);

        // 三个出口原样转给控制器：主窗口不知道贴图与识别各自要做什么
        QObject::connect(m_history, &History::HistoryView::pinRequested, this, &MainWindow::historyPinRequested);
        QObject::connect(m_history, &History::HistoryView::ocrRequested, this, &MainWindow::historyOcrRequested);
        QObject::connect(m_history, &History::HistoryView::removeRequested, this, &MainWindow::historyRemoveRequested);
        QObject::connect(m_history, &History::HistoryView::openFolderRequested, this, &MainWindow::historyOpenFolderRequested);
        QObject::connect(m_history, &History::HistoryView::clearAllRequested, this, &MainWindow::historyClearRequested);
        QObject::connect(m_history, &History::HistoryView::messageForUser, this, &MainWindow::messageForUser);
        return page;
    }

    void MainWindow::refreshHistory()
    {
        if (m_history != nullptr)
        {
            m_history->refresh();
        }
    }

    QWidget *MainWindow::buildAboutPage()
    {
        auto *page   = new QWidget{this};
        auto *layout = new QVBoxLayout{page};

        auto *logo = new QLabel{page};
        logo->setPixmap(m_icons.pixmap("AppIcon", GyanisScribe::Ui::IconSize::ExtraLarge));
        logo->setAccessibleName(GyanisScribe::Core::uiText("智录图标"));

        auto *text = new QLabel{aboutText(QApplication::applicationVersion()), page};
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);

        // 检查更新：只查不换——下载与替换可执行体要先有签名与原子替换那一套，
        // 这里把「有没有新版」与发布页地址摆出来，下载交给用户自己在浏览器里点
        auto *checkButton = new QPushButton{GyanisScribe::Core::uiText("检查更新"), page};
        checkButton->setAccessibleName(GyanisScribe::Core::uiText("检查更新"));
        auto *status = new QLabel{page};
        status->setObjectName(QStringLiteral("pageHint"));
        status->setWordWrap(true);
        status->setTextInteractionFlags(Qt::TextBrowserInteraction);
        status->setOpenExternalLinks(false);
        QObject::connect(status, &QLabel::linkActivated, status, [](const QString &link) { QDesktopServices::openUrl(QUrl{link}); });

        m_updateChecker = std::make_unique<GyanisScribe::App::UpdateChecker>(this);
        QObject::connect(m_updateChecker.get(), &GyanisScribe::App::UpdateChecker::finished, status,
                         [checkButton, status](const GyanisScribe::App::UpdateVerdict &verdict)
                         {
                             checkButton->setEnabled(true);
                             status->setText(verdict.link.isEmpty() ? verdict.message : QStringLiteral("<a href=\"%1\">%2</a>").arg(verdict.link, verdict.message));
                         });
        QObject::connect(checkButton, &QPushButton::clicked, page,
                         [this, checkButton, status]
                         {
                             if (m_updateChecker->isRunning())
                             {
                                 // 已经在查：按钮这会儿本来就该是灰的，走到这里说明有两处入口，直接不理
                                 return;
                             }
                             checkButton->setEnabled(false);
                             status->setText(GyanisScribe::Core::uiText("正在检查…"));
                             m_updateChecker->check();
                         });

        layout->addWidget(logo);
        layout->addWidget(text);
        layout->addWidget(checkButton);
        layout->addWidget(status);
        layout->addStretch();
        return page;
    }

} // namespace GyanisScribe::App
