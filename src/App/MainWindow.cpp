#include "App/CaptureIntent.h"
#include "App/MainWindow.h"
#include "App/TrayController.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "History/HistoryRules.h"
#include "History/HistoryView.h"
#include "Ocr/OcrEngineFactory.h"
#include "Pick/ColorFormat.h"
#include "Pin/PinLayout.h"
#include "Record/FfmpegAudioDevices.h"
#include "Record/RecorderConfig.h"
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
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
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
    QCheckBox *makeCursorHighlightCheck(ISettingsStore &settings)
    {
        auto *check = new QCheckBox;
        check->setText(GyanisScribe::Core::uiText("把光标位置圈在画面里"));
        check->setAccessibleName(GyanisScribe::Core::uiText("光标高亮"));
        check->setToolTip(GyanisScribe::Core::uiText("整屏与选区录屏生效；录单个窗口时目前还算不出窗口左上角的位置"));
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
    QComboBox *makeRecordScopeCombo(ISettingsStore &settings)
    {
        auto *combo = new QComboBox;
        for (const auto &choice: GyanisScribe::Record::kRecordScopeChoices)
        {
            combo->addItem(GyanisScribe::Core::uiText(choice.label.data()), tokenText(choice.token));
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
    QComboBox *makeCaptureIntentCombo(ISettingsStore &settings)
    {
        auto *combo = new QComboBox;
        for (const auto &choice: GyanisScribe::App::kCaptureIntentChoices)
        {
            combo->addItem(GyanisScribe::Core::uiText(choice.label.data()), tokenText(choice.token));
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
                           QWidget *parent) :
        QWidget(parent), m_settings(settings), m_theme(theme), m_icons(icons), m_capabilities(capabilities),
        m_audioDevices(std::make_unique<GyanisScribe::Record::FfmpegAudioDeviceEnumerator>()), m_historyStore(historyStore)
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
        form->addRow(GyanisScribe::Core::uiText("备份与恢复"), buildTransferRow());
        return page;
    }

    QWidget *MainWindow::buildCapturePage()
    {
        auto *page = new QWidget{this};
        auto *form = makePageForm(page, GyanisScribe::Core::uiText("截图与标注成品落在这个目录里。取色复制出来的文本按下面的写法格式化。"));
        form->addRow(GyanisScribe::Core::uiText("截完之后"), makeCaptureIntentCombo(m_settings));
        form->addRow(GyanisScribe::Core::uiText("延时截图"), makeCaptureDelaySpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("截图与标注成品目录"),
                     makePathRow(m_settings, GyanisScribe::Settings::kPathScreenshot, GyanisScribe::Core::uiText("截图目录"), Core::Paths::defaultScreenshotDirectory(), m_icons));
        form->addRow(GyanisScribe::Core::uiText("复制用的写法"), makeColorFormatCombo(m_settings));
        return page;
    }

    QWidget *MainWindow::buildAnnotationPage()
    {
        auto *page = new QWidget{this};
        auto *form = makePageForm(page, GyanisScribe::Core::uiText("新打开的标注窗按这两档起步。画过之后的改动会被工具栏记下来，下次仍以最近用的那一档开始。"));
        form->addRow(GyanisScribe::Core::uiText("默认线宽"),
                     makeStepCombo(m_settings, GyanisScribe::Settings::kAnnotationDefaultWidth, GyanisScribe::Ui::kAnnotationWidths,
                                   GyanisScribe::Ui::kAnnotationWidths[GyanisScribe::Ui::kDefaultAnnotationWidthStep], GyanisScribe::Core::uiText("默认线宽")));
        form->addRow(GyanisScribe::Core::uiText("默认字号"),
                     makeStepCombo(m_settings, GyanisScribe::Settings::kAnnotationDefaultFontSize, GyanisScribe::Ui::kFontSizes,
                                   GyanisScribe::Ui::kFontSizes[GyanisScribe::Ui::kDefaultAnnotationFontSizeIndex], GyanisScribe::Core::uiText("默认字号")));
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
        auto *form = makePageForm(page, GyanisScribe::Core::uiText("录屏成品落在这个目录里。录制范围选「上次选区」或「当前窗口」时，画面里就只有那一片。存成 GIF 时帧率上限比 MP4 "
                                                                   "低。画面码率填 0 就交给编码器的质量档，给了具体数值就按那个码率控，GIF "
                                                                   "用不上这一项。取声那两格列不出设备时只剩「不取这一路」，把鼠标停在框上能看到原因；存成 GIF "
                                                                   "时这两格会灰掉，那种文件带不了声音。这几项都从下一次开始录屏时生效。"));
        form->addRow(GyanisScribe::Core::uiText("录屏成品目录"),
                     makePathRow(m_settings, GyanisScribe::Settings::kPathRecording, GyanisScribe::Core::uiText("录屏目录"), Core::Paths::defaultRecordingDirectory(), m_icons));
        form->addRow(GyanisScribe::Core::uiText("录制范围"), makeRecordScopeCombo(m_settings));
        auto *formatCombo = makeRecordFormatCombo(m_settings);
        form->addRow(GyanisScribe::Core::uiText("输出格式"), formatCombo);
        form->addRow(GyanisScribe::Core::uiText("默认帧率"), makeFrameRateSpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("画面码率"), makeVideoBitrateSpin(m_settings));
        form->addRow(GyanisScribe::Core::uiText("光标高亮"), makeCursorHighlightCheck(m_settings));
        auto *microphoneCombo  = makeAudioDeviceCombo(m_settings, GyanisScribe::Settings::kRecordAudioMicrophone, GyanisScribe::Core::uiText("麦克风设备"), *m_audioDevices);
        auto *systemSoundCombo = makeAudioDeviceCombo(m_settings, GyanisScribe::Settings::kRecordAudioSystemSound, GyanisScribe::Core::uiText("系统声音设备"), *m_audioDevices);
        form->addRow(GyanisScribe::Core::uiText("麦克风这一路"), microphoneCombo);
        form->addRow(GyanisScribe::Core::uiText("系统声音这一路"), systemSoundCombo);

        // 选到 GIF 就把取声那两格关掉：判据与 RecordingService 跳过取声用的是同一句
        QObject::connect(formatCombo, &QComboBox::currentIndexChanged, formatCombo,
                         [formatCombo, microphoneCombo, systemSoundCombo](int) { applyAudioAvailability(*formatCombo, *microphoneCombo, *systemSoundCombo); });
        applyAudioAvailability(*formatCombo, *microphoneCombo, *systemSoundCombo);
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
        const auto intro = m_capabilities.globalHotkeys ? GyanisScribe::Core::uiText("四个全局热键随时可改，改完立即重新登记，不需要重启。")
                                                        : GyanisScribe::Core::uiText("这个平台没有登记全局热键的接口，下面四项改了也不会生效。");
        auto      *form  = makePageForm(page, intro);
        for (const auto [key, title, defaultText]: {std::tuple{GyanisScribe::Settings::kHotkeyCapture, GyanisScribe::Core::uiText("截图"), QStringLiteral("Ctrl+Alt+A")},
                                                    std::tuple{GyanisScribe::Settings::kHotkeyRecord, GyanisScribe::Core::uiText("录屏"), QStringLiteral("Ctrl+Alt+R")},
                                                    std::tuple{GyanisScribe::Settings::kHotkeyOcr, GyanisScribe::Core::uiText("文字识别"), QStringLiteral("Ctrl+Alt+O")},
                                                    std::tuple{GyanisScribe::Settings::kHotkeyPick, GyanisScribe::Core::uiText("屏幕取色"), QStringLiteral("Ctrl+Alt+P")}})
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

        layout->addWidget(logo);
        layout->addWidget(text);
        layout->addStretch();
        return page;
    }

} // namespace GyanisScribe::App
