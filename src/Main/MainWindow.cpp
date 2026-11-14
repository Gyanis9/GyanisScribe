#include "App/CaptureIntent.h"
#include "App/CustomHotkeys.h"
#include "Main/MainWindow.h"
#include "App/TrayClick.h"
#include "App/TrayController.h"
#include "App/UpdateCheck.h"
#include "Core/Annotation/AnnotationPalette.h"
#include "Core/Capture/ScreenUtils.h"
#include "Core/Geometry/ContinuousCorner.h"
#include "Core/Image/ImageFormat.h"
#include "Base/Logger.h"
#include "Core/Image/OutputDecoration.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/History/HistoryRules.h"
#include "Gui/History/HistoryView.h"
#include "Gui/Memory/MemoryView.h"
#include "Core/History/UnsavedArchive.h"
#include "Plugins/OcrEngines/OcrEngineFactory.h"
#include "Core/Pick/ColorFormat.h"
#include "Core/Pick/PickGeometry.h"
#include "Core/Pin/PinLayout.h"
#include "Plugins/Recording/FfmpegAudioDevices.h"
#include "Plugins/Recording/FfmpegVideoEncoderEnumerator.h"
#include "Core/Record/RecorderConfig.h"
#include "Core/Rewind/RewindConfig.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Controls/AppShortcuts.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/CheckBox.h"
#include "Gui/Controls/ComboBox.h"
#include "Gui/Controls/LineEdit.h"
#include "Gui/Controls/ListWidget.h"
#include "Gui/Controls/Menu.h"
#include "Gui/Controls/ScrollBar.h"
#include "Gui/Controls/SpinBox.h"
#include "Gui/Controls/SurfaceWidget.h"
#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Controls/HotkeyEdit.h"
#include "Gui/Controls/IconLabel.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/Language.h"
#include "Gui/Theme/ThemeManager.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QScrollArea>
#include <QScreen>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

#include <filesystem>
#include <format>
#include <string>
#include <utility>
#include <vector>
namespace
{
    using GyanisScribe::Base::Expected;
    using GyanisScribe::Base::Logger;
    using GyanisScribe::Core::ISettingsStore;
    using GyanisScribe::Gui::AppLanguage;
    using GyanisScribe::Gui::IconProvider;
    using GyanisScribe::Gui::stepIndexOf;
    using GyanisScribe::Gui::ThemeKind;
    using GyanisScribe::Gui::ThemeManager;

    using GyanisScribe::Main::appLanguageAt;
    using GyanisScribe::Main::indexOfAppLanguage;
    using GyanisScribe::Main::indexOfThemeKind;
    using GyanisScribe::Main::LanguageChoice;
    using GyanisScribe::Main::MainWindowPage;
    using GyanisScribe::Main::PageChoice;
    using GyanisScribe::Main::ThemeChoice;
    using GyanisScribe::Main::themeKindAt;

    namespace Core = GyanisScribe::Core;

    /// 主窗口的全部页：序号、目标页、导航行标签与图标绑在同一张表上，表的顺序就是导航的顺序
    constexpr std::array<PageChoice, 11> kPages = {
            PageChoice{MainWindowPage::General, "通用", "Settings"},
            PageChoice{MainWindowPage::Capture, "截图", "Capture"},
            PageChoice{MainWindowPage::Annotation, "标注", "Annotation"},
            PageChoice{MainWindowPage::Pin, "贴图", "Pin"},
            PageChoice{MainWindowPage::Record, "录屏", "Record"},
            PageChoice{MainWindowPage::Ocr, "文字识别", "Ocr"},
            PageChoice{MainWindowPage::Hotkey, "快捷键", "Hotkey"},
            PageChoice{MainWindowPage::Theme, "主题", "Theme"},
            PageChoice{MainWindowPage::About, "关于", "About"},
            PageChoice{MainWindowPage::History, "历史", "History"},
            // 记忆库是「带识别的截图库」：用自己那张卡片+放大镜图标，与历史一眼分得开
            PageChoice{MainWindowPage::Memory, "记忆库", "Memory"},
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
    constexpr int kNavColumnWidth = 148;

    /**
     * @brief 一行设置字段该收到多宽
     * @param widget 表单右侧的字段控件
     * @return 数字框 140、下拉框 260、单行文本 340；其余行容器与勾选框按自身 sizeHint，最大不超过 340
     * @details 所有字段一律 340 会让「截图延时」这类三四个数字的格子占掉半条表单，右侧看上去就是一片空白。
     */
    int settingsFieldMaximumWidth(const QWidget &widget)
    {
        if (qobject_cast<const QAbstractSpinBox *>(&widget) != nullptr)
        {
            return GyanisScribe::Gui::kNumericFieldMaximumWidth;
        }
        if (qobject_cast<const QComboBox *>(&widget) != nullptr)
        {
            return GyanisScribe::Gui::kComboFieldMaximumWidth;
        }
        if (qobject_cast<const QLineEdit *>(&widget) != nullptr)
        {
            return GyanisScribe::Gui::kFieldMaximumWidth;
        }
        return std::min(widget.sizeHint().width(), GyanisScribe::Gui::kFieldMaximumWidth);
    }

    /// 导航行上记图标名的角色：换主题时按它重取图标，不用把 kPages 与行号再对一遍
    constexpr int kNavIconNameRole = Qt::UserRole + 1;

    /// 历史条数这一格能填到的最大值：再多就得靠搜索找，而截图量本身也撑不到这里
    constexpr int kMaximumEntriesLimit = 10000;

    /// 历史天数这一格能填到的最大值，按十年给：比这更久的留法就是「不限」那一档
    constexpr int kRetentionDaysLimit = 3650;

    /**
     * @brief 把一页设置裹进滚动区
     * @param page 已经搭好的那一页，所有权交给滚动区
     * @return 交给页容器的那一层滚动区
     * @details 页比窗口高时底下那一截要够得着：快捷键这两栏加起来四十来行，默认窗口高度摆不下，
     *          没有滚动区就是「看不见也改不到」，而且一声不吭。
     *          对象名是给样式表用的：#settingsScroll 把这一层与它的视口都设成透明，
     *          不然卡片四周会多出一圈与主题不合的底色。
     */
    QScrollArea *wrapSettingsPage(QWidget &page)
    {
        auto *scroll = new QScrollArea;
        scroll->setObjectName(QStringLiteral("settingsScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        // 横向不出杠：表单的宽度跟着窗口走，出杠说明是排版坏了而不是内容太长
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // 样式表不再给 QScrollBar 尺寸与颜色：两条滚动条在滚动区建好时就换成自绘件
        scroll->setVerticalScrollBar(new GyanisScribe::Gui::ScrollBar{Qt::Vertical, scroll});
        scroll->setHorizontalScrollBar(new GyanisScribe::Gui::ScrollBar{Qt::Horizontal, scroll});
        scroll->setWidget(&page);
        return scroll;
    }

    /**
     * @brief 一页设置的骨架：顶部一行说明文字加一张空表单
     * @param page 这一页的容器，版面由本函数接管
     * @param intro 已经过 uiText 的说明文字
     * @return 归 page 所有的表单，调用方往它 addRow
     * @details 每一类都得先有一句「这一类管什么、改了会怎样」：需求第五节要的是能自己上手的设置界面，
     *          只列控件名会把「改了不重启就不生效」这类关键信息藏起来。
     */
    /// 分组卡片挂回哪条内容列：后续分组靠这个属性找回同一列，不再往 makePageForm 传参
    constexpr auto kSettingsContentProperty = "settingsContentColumn";

    /// 一张分组卡片：卡体与其中的表单一起返回，调用方两样都要用
    struct SettingsSection
    {
        QWidget     *card = nullptr; ///< 带底色的圆角分组卡
        QFormLayout *form = nullptr; ///< 卡片内的表单
    };

    /**
     * @brief 在内容列里造一张分组卡片
     * @param parent 内容列
     * @param title 分组标题；空表示这一张不摆标题
     * @return 卡体与其中的表单
     * @details 分组用比页面卡浅一档的 mutedFill 区分层级，不额外描边：深色档那圈边会显脏
     */
    SettingsSection makeSettingsSection(QWidget *parent, const QString &title)
    {
        auto *card   = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Panel, GyanisScribe::Gui::SurfaceFillRole::MutedFill, parent};
        auto *layout = new QVBoxLayout{card};
        layout->setContentsMargins(GyanisScribe::Gui::kSpacingMedium, GyanisScribe::Gui::kSpacingMedium, GyanisScribe::Gui::kSpacingMedium,
                                   GyanisScribe::Gui::kSpacingMedium);
        layout->setSpacing(GyanisScribe::Gui::kSpacingSmall);
        if (!title.isEmpty())
        {
            auto *heading = new QLabel{title, card};
            heading->setObjectName(QStringLiteral("settingsSectionTitle"));
            GyanisScribe::Gui::applyTextRole(*heading, GyanisScribe::Gui::TextRole::Body);
            layout->addWidget(heading);
        }
        auto *form = new QFormLayout;
        // 双环要在输入框外占 6px：行距从 2px 提到 8px，环不会压在相邻行上
        form->setVerticalSpacing(GyanisScribe::Gui::kSpacingSmall);
        form->setHorizontalSpacing(GyanisScribe::Gui::kSpacingSmall);
        // 标签左对齐并与字段同一水平线：整列宽由最长标签实测，字段从这一列右缘起排；
        // 顶对齐会让单行字段看着下沉，竖直方向取居中
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        layout->addLayout(form);
        return SettingsSection{card, form};
    }

    QFormLayout *makePageForm(QWidget *page, const QString &intro, const QString &firstSectionTitle)
    {
        auto *layout = new QVBoxLayout{page};
        layout->setContentsMargins(GyanisScribe::Gui::kSpacingLarge, GyanisScribe::Gui::kSpacingLarge, GyanisScribe::Gui::kSpacingLarge, GyanisScribe::Gui::kSpacingLarge);
        layout->setSpacing(0);

        // 标题、说明与表单共用一条内容列，整列在卡片里居中：宽窗下不会全部挤在左边、右侧空一大片
        auto *content = new QWidget{page};
        content->setObjectName(QStringLiteral("settingsPageContent"));
        auto *contentLayout = new QVBoxLayout{content};
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(GyanisScribe::Gui::kSpacingMedium);

        auto *text = new QLabel{intro, content};
        text->setObjectName(QStringLiteral("pageHint"));
        text->setWordWrap(true);
        text->setMaximumWidth(GyanisScribe::Gui::kIntroMaximumWidth);
        // 说明行是次要文本，同时要让读屏软件把它当成这一段正文念出来
        text->setAccessibleName(GyanisScribe::Base::uiText("这一类的说明"));
        contentLayout->addWidget(text);

        const auto first = makeSettingsSection(content, firstSectionTitle);
        contentLayout->addWidget(first.card);
        first.form->setProperty(kSettingsContentProperty, QVariant::fromValue(static_cast<QObject *>(content)));

        layout->addWidget(content, 0, Qt::AlignHCenter);
        layout->addStretch();
        return first.form;
    }

    /**
     * @brief 在同一页追加一张分组卡片
     * @param current 当前分组里的表单（由 makePageForm 或本函数返回）
     * @param title 新分组的标题
     * @return 新分组里的表单；找回内容列失败时交回原表单，后续行仍落在上一组里
     */
    QFormLayout *addSettingsSection(QFormLayout *current, const QString &title)
    {
        if (current == nullptr)
        {
            return nullptr;
        }
        auto *content = qobject_cast<QWidget *>(current->property(kSettingsContentProperty).value<QObject *>());
        auto *contentLayout = content != nullptr ? qobject_cast<QVBoxLayout *>(content->layout()) : nullptr;
        if (contentLayout == nullptr)
        {
            return current;
        }
        const auto section = makeSettingsSection(content, title);
        contentLayout->addWidget(section.card);
        section.form->setProperty(kSettingsContentProperty, QVariant::fromValue(static_cast<QObject *>(content)));
        return section.form;
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
        // 深色窗口底与卡片同值后，圆角边界全靠这圈随主题的描边；浅色档同样收一圈更轻的分界
        if (auto *surface = dynamic_cast<GyanisScribe::Gui::SurfaceWidget *>(&page); surface != nullptr)
        {
            surface->setOutlineVisible(true);
        }

        // 表单页的标题并进居中内容列；历史与记忆库这类视图页没有内容列，仍摆在页顶
        QWidget     *headerParent = &page;
        QVBoxLayout *headerLayout = qobject_cast<QVBoxLayout *>(page.layout());
        if (auto *content = page.findChild<QWidget *>(QStringLiteral("settingsPageContent")); content != nullptr)
        {
            headerParent = content;
            headerLayout = qobject_cast<QVBoxLayout *>(content->layout());
        }
        if (headerLayout != nullptr)
        {
            auto *heading = new QLabel{title, headerParent};
            heading->setObjectName(QStringLiteral("pageTitle"));
            GyanisScribe::Gui::applyTextRole(*heading, GyanisScribe::Gui::TextRole::PageTitle);
            headerLayout->insertWidget(0, heading);
        }

        // 标签文字左对齐：字段列由最长的一条标签定起点，短标签自然留白，不再靠右对齐把每个词都推到同一右缘。
        // 字段宽度与内容列宽在 applySettingsFormMetrics 里按控件类型统一收
        // 一页可能切出好几张分组卡，每张卡里的标签都要左对齐
        for (auto *form: page.findChildren<QFormLayout *>())
        {
            for (int row = 0; row < form->rowCount(); ++row)
            {
                if (auto *label = form->itemAt(row, QFormLayout::LabelRole); label != nullptr && label->widget() != nullptr)
                {
                    if (auto *text = qobject_cast<QLabel *>(label->widget()); text != nullptr)
                    {
                        text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                    }
                }
            }
        }
    }

    /**
     * @brief 每页按本页最长的标签收标签列，并给字段列落上限
     * @param stack 页容器，每一页是包着卡片的滚动区
     * @return 收好之后最宽一页的内容宽度
     * @details 短页不替长页背留白：字段列紧挨本页最长标签的右缘，翻页时各页字段可以不在同一 x，
     *          但同一页内必须同线。字段先设上限再量页宽，最宽的那一页决定窗口最小宽。
     */
    int applySettingsFormMetrics(QStackedWidget &stack)
    {
        struct PageMetrics
        {
            QWidget             *page         = nullptr; ///< 这一页的卡片
            QWidget             *content      = nullptr; ///< 居中内容列；视图页没有
            int                  labelWidth   = 0;       ///< 本页标签列宽
            int                  numericWidth = 0;       ///< 本页数字框统一宽；没有该类时为 0
            int                  comboWidth   = 0;       ///< 本页下拉框统一宽；没有该类时为 0
            QList<QFormLayout *> forms;                  ///< 本页表单，通常一张
        };

        // 第一趟先量：所有表单页共用一个内容列宽，翻页时标题与说明不会左右跳
        std::vector<PageMetrics> pages;
        int                      maxContentWidth = GyanisScribe::Gui::kComboFieldMaximumWidth;
        for (int index = 0; index < stack.count(); ++index)
        {
            auto *scroll = qobject_cast<QScrollArea *>(stack.widget(index));
            auto *page   = scroll != nullptr ? scroll->widget() : stack.widget(index);
            if (page == nullptr)
            {
                continue;
            }
            auto      *content = page->findChild<QWidget *>(QStringLiteral("settingsPageContent"));
            const auto forms = page->findChildren<QFormLayout *>();
            if (forms.isEmpty())
            {
                // 关于页有内容列但没有表单；历史与记忆库是视图页，两者都没有，不参与尺寸计算
                if (content != nullptr)
                {
                    pages.push_back(PageMetrics{.page = page, .content = content});
                }
                continue;
            }

            // 每页各按本页最长的一条标签收列：短页不替长页背那块留白，字段紧挨标签列的右缘
            int labelWidth = 0;
            for (auto *form: forms)
            {
                for (int row = 0; row < form->rowCount(); ++row)
                {
                    const auto *label = form->itemAt(row, QFormLayout::LabelRole);
                    if (label != nullptr && label->widget() != nullptr)
                    {
                        labelWidth = std::max(labelWidth, label->widget()->sizeHint().width());
                    }
                }
            }

            // 字段按类型实测：数字框与下拉各自在本页取统一宽，长文本类才占满 340。
            // 短选项的下拉与三四位数的数字框不再被字段列拉长，右侧的空白随之消失
            int  numericHint  = 0;
            int  comboHint    = 0;
            bool hasWideField = false;
            for (auto *form: forms)
            {
                for (int row = 0; row < form->rowCount(); ++row)
                {
                    const auto *field = form->itemAt(row, QFormLayout::FieldRole);
                    if (field == nullptr || field->widget() == nullptr)
                    {
                        continue;
                    }
                    const auto *widget = field->widget();
                    if (qobject_cast<const QAbstractSpinBox *>(widget) != nullptr)
                    {
                        numericHint = std::max(numericHint, widget->sizeHint().width());
                    }
                    else if (qobject_cast<const QComboBox *>(widget) != nullptr)
                    {
                        comboHint = std::max(comboHint, widget->sizeHint().width());
                    }
                    else if (qobject_cast<const QCheckBox *>(widget) == nullptr)
                    {
                        hasWideField = true;
                    }
                }
            }
            const int numericWidth = numericHint > 0 ? std::clamp(numericHint, GyanisScribe::Gui::kNumericFieldMinimumWidth, GyanisScribe::Gui::kNumericFieldMaximumWidth) : 0;
            const int comboWidth   = comboHint > 0 ? std::clamp(comboHint, GyanisScribe::Gui::kComboFieldMinimumWidth, GyanisScribe::Gui::kComboFieldMaximumWidth) : 0;
            const int fieldWidth   = std::max({numericWidth, comboWidth, hasWideField ? GyanisScribe::Gui::kFieldMaximumWidth : 0});
            maxContentWidth = std::max(maxContentWidth, labelWidth + GyanisScribe::Gui::kSpacingSmall + fieldWidth);
            pages.push_back(PageMetrics{.page = page, .content = content, .labelWidth = labelWidth, .numericWidth = numericWidth, .comboWidth = comboWidth, .forms = forms});
        }

        // 第二趟统一落：内容列同宽并居中；表单字段各自收窄；说明行跟着内容列宽换行
        int windowContentWidth = 0;
        for (auto &metrics: pages)
        {
            for (auto *form: metrics.forms)
            {
                for (int row = 0; row < form->rowCount(); ++row)
                {
                    if (auto *label = form->itemAt(row, QFormLayout::LabelRole); label != nullptr && label->widget() != nullptr)
                    {
                        label->widget()->setFixedWidth(metrics.labelWidth);
                    }
                    auto *field  = form->itemAt(row, QFormLayout::FieldRole);
                    auto *widget = field != nullptr ? field->widget() : nullptr;
                    if (widget == nullptr)
                    {
                        continue;
                    }
                    // 同类控件在本页统一宽度：固定宽之后表单不会再把它们拉满字段列
                    if (qobject_cast<QAbstractSpinBox *>(widget) != nullptr && metrics.numericWidth > 0)
                    {
                        widget->setFixedWidth(metrics.numericWidth);
                    }
                    else if (qobject_cast<QComboBox *>(widget) != nullptr && metrics.comboWidth > 0)
                    {
                        widget->setFixedWidth(metrics.comboWidth);
                    }
                    else
                    {
                        widget->setMaximumWidth(settingsFieldMaximumWidth(*widget));
                    }
                }
            }
            if (metrics.content != nullptr)
            {
                metrics.content->setFixedWidth(maxContentWidth);
            }
            for (auto *hint: metrics.page->findChildren<QLabel *>())
            {
                if (hint->objectName() == QStringLiteral("pageHint"))
                {
                    hint->setMaximumWidth(std::min(maxContentWidth, GyanisScribe::Gui::kIntroMaximumWidth));
                }
            }
            if (!metrics.forms.isEmpty())
            {
                windowContentWidth = std::max({windowContentWidth, maxContentWidth + 2 * GyanisScribe::Gui::kSpacingMedium, metrics.page->sizeHint().width()});
            }
        }
        return windowContentWidth;
    }

    /**
     * @brief 设置页的行标签：有图标的用它，没有的仍传纯文字
     * @param icons 图标来源
     * @param theme 主题来源，换肤时按名字重取
     * @param name 图标名
     * @param text 已经过 uiText 的标签文字
     * @return 归表单所有的行标签
     */
    QWidget *iconRow(GyanisScribe::Gui::IconProvider &icons, GyanisScribe::Gui::ThemeManager &theme, std::string_view name, const QString &text)
    {
        return new GyanisScribe::Gui::IconLabel{icons, theme, name, text};
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
    QWidget *makePathRow(GyanisScribe::Core::ISettingsStore &settings, const std::string_view key, const QString &title, const std::filesystem::path &fallback,
                         GyanisScribe::Gui::IconProvider &icons)
    {
        auto *row    = new QWidget;
        auto *layout = new QHBoxLayout{row};
        // 与 buildTransferRow / buildPaletteTransferRow 同一条纪律：行容器自己不吃布局默认边距，
        // 否则输入框左缘比字段列右移一截，三处路径行看起来各偏各的
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(GyanisScribe::Gui::kSpacingSmall);
        auto      *editor  = new GyanisScribe::Gui::LineEdit{row};
        const auto stored  = settings.stringValue(key, QString{});
        const auto display = stored.isEmpty() ? GyanisScribe::Base::Paths::toDisplayText(fallback) : stored;
        editor->setText(display);
        // setText 会把光标放到末尾，长路径因此只露出最后半截；路径要从盘符/根目录读起
        editor->setCursorPosition(0);
        editor->setClearButtonEnabled(false);
        editor->setAccessibleName(title);
        editor->setToolTip(GyanisScribe::Base::uiText("%1。留空表示使用默认目录。").arg(title));

        auto *browse = new GyanisScribe::Gui::Button{GyanisScribe::Base::uiText("浏览…"), row};
        icons.applyIcon(*browse, "Folder");
        browse->setAccessibleName(GyanisScribe::Base::uiText("选择%1的目录").arg(title));

        layout->addWidget(editor, 1);
        layout->addWidget(browse);

        QObject::connect(browse, &QPushButton::clicked, row,
                         // row 按值抓：它比这一行活得久，按引用抓会留下一个指回局部指针的引用
                         [row, editor, title]
                         {
                             const auto picked = QFileDialog::getExistingDirectory(row, GyanisScribe::Base::uiText("选择%1").arg(title), editor->text());
                             if (!picked.isEmpty())
                             {
                                 editor->setText(picked);
                             }
                         });

        // 失焦即写：设置页没有「保存」按钮，改完就看到效果是需求第五节的要求
        QObject::connect(editor, &QLineEdit::editingFinished, row,
                         // fallback 按值抓：调用点给的多是临时 path，按引用抓会在这一句执行时读已销毁的路径
                         [&settings, editor, key, title, fallback]
                         {
                             const auto text = editor->text().trimmed();
                             if (text.isEmpty())
                             {
                                 // 清空表示回到默认：空 QVariant 是「把这一项删掉」，写空串会让读取端每次都按「填坏了」记一条警告
                                 if (auto written = settings.setValue(key, QVariant{}); !written)
                                 {
                                     Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                                 }
                                 editor->setText(GyanisScribe::Base::Paths::toDisplayText(fallback));
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
    QSpinBox *makeOptionalLimitSpin(GyanisScribe::Core::ISettingsStore &settings, const std::string_view key, const QString &title, const QString &suffix, const int maximum,
                                    const int fallback)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(0, maximum);
        spin->setSpecialValueText(GyanisScribe::Base::uiText("不限"));
        spin->setSuffix(suffix);
        spin->setAccessibleName(title);
        // 配置里的负数会被范围夹到 0 那一档，也就是「不限」——与 GyanisScribe::Core::policyFromSettings 的读法同解
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
     * @param steps 阶梯表，取值只能是 GyanisScribe::Gui::kAnnotationWidths 或 GyanisScribe::Gui::kFontSizes
     * @param fallback 配置里没有、或存的值不在阶梯里时的回落档
     * @param title 中文名，用作无障碍名称与日志里的线索
     * @return 归调用方所有的下拉框，由 addRow 挂上父对象
     */
    template<std::size_t Size>
    QComboBox *makeStepCombo(GyanisScribe::Core::ISettingsStore &settings, const std::string_view key, const std::array<int, Size> &steps, const int fallback, const QString &title)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
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
    QCheckBox *makeCursorHighlightCheck(GyanisScribe::Core::ISettingsStore &settings, const GyanisScribe::Gui::IconProvider &icons)
    {
        auto *check = new GyanisScribe::Gui::CheckBox;
        check->setIcon(icons.icon("CursorHighlight"));
        check->setText(GyanisScribe::Base::uiText("在录制画面中标记光标位置"));
        check->setAccessibleName(GyanisScribe::Base::uiText("光标高亮"));
        check->setToolTip(GyanisScribe::Base::uiText("整屏、区域与窗口录屏均生效；录制窗口时按平台提供的客户区左上角定位，无法获取该位置时不绘制标记。"));
        check->setChecked(settings.boolValue(GyanisScribe::Core::kRecordCursorHighlight, false));

        // 接线排在设初值之后：摆到位不等于用户改过，否则一开设置页就把默认值写进配置
        QObject::connect(check, &QCheckBox::toggled, check,
                         [&settings](bool checked)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kRecordCursorHighlight, checked); !written)
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
    QCheckBox *makeIncludeCursorCheck(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *check = new GyanisScribe::Gui::CheckBox;
        check->setText(GyanisScribe::Base::uiText("在截图中绘制"));
        check->setAccessibleName(GyanisScribe::Base::uiText("含鼠标指针"));
        check->setToolTip(
                GyanisScribe::Base::uiText("整屏与区域截图生效；滚动长截图按帧拼接，绘制指针会留下重复影像，因此不应用。非 Windows 平台无法获取系统指针形状，启用后也不会显示。"));
        check->setChecked(settings.boolValue(GyanisScribe::Core::kCaptureIncludeCursor, GyanisScribe::Core::kDefaultCaptureIncludeCursor));

        // 接线排在设初值之后：摆到位不等于用户改过，否则一开设置页就把默认值写进配置
        QObject::connect(check, &QCheckBox::toggled, check,
                         [&settings](bool checked)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kCaptureIncludeCursor, checked); !written)
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
    QCheckBox *makeSettingCheck(GyanisScribe::Core::ISettingsStore &settings, const std::string_view key, const bool defaultOn, const QString &label, const QString &accessibleName,
                                const QString &tooltip)
    {
        auto *check = new GyanisScribe::Gui::CheckBox;
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
     * @brief 界面语言下拉：档位读写都走 Gui/Theme/Language 的词表
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     */
    QComboBox *makeUiLanguageCombo(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        for (const auto &choice: kLanguageChoices)
        {
            combo->addItem(GyanisScribe::Base::uiText(choice.label));
        }
        combo->setAccessibleName(GyanisScribe::Base::uiText("界面语言"));
        combo->setToolTip(GyanisScribe::Base::uiText("切换语言需重启智录后生效。"));

        const auto stored = settings.stringValue(GyanisScribe::Core::kUiLanguage, QString::fromUtf8(std::string{GyanisScribe::Gui::appLanguageText(AppLanguage::Chinese)}));
        if (const auto parsed = GyanisScribe::Gui::parseAppLanguage(stored.toStdString()); parsed)
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
                             const auto token = QString::fromUtf8(std::string{GyanisScribe::Gui::appLanguageText(*choice)});
                             if (auto written = settings.setValue(GyanisScribe::Core::kUiLanguage, token); !written)
                             {
                                 Logger::warning(std::format("界面语言没能保存：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }

    /**
     * @brief 主题档位对应的图标名：与 kThemeChoices 一一对应
     * @param kind 档位
     * @return 该档位在资源里的图标名
     */
    std::string_view themeIconName(ThemeKind kind)
    {
        switch (kind)
        {
            case ThemeKind::Light:
                return "ThemeLight";
            case ThemeKind::Dark:
                return "ThemeDark";
            case ThemeKind::System:
                return "ThemeSystem";
            case ThemeKind::HighContrast:
                return "ThemeHighContrast";
        }
        return "ThemeLight";
    }

    /**
     * @brief 配色方案下拉：标签与档位都来自 themeChoices()
     * @param theme 主题管理器，选中即时生效
     * @param icons 图标供给器，每一档前面挂它自己的那枚图标
     * @return 归调用方所有的下拉框
     * @details 四档的字形各不相同（太阳、月亮、半明半暗的屏幕、实心半圆），
     *          逐项挂图比逐条读四个字的档名更快认出现在是哪一档。
     */
    QComboBox *makeThemeCombo(GyanisScribe::Gui::ThemeManager &theme, GyanisScribe::Gui::IconProvider &icons)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        for (const auto &choice: kThemeChoices)
        {
            combo->addItem(icons.icon(themeIconName(choice.kind)), GyanisScribe::Base::uiText(choice.label));
        }
        combo->setAccessibleName(GyanisScribe::Base::uiText("主题"));
        combo->setCurrentIndex(indexOfThemeKind(theme.requestedKind()));
        // QIcon 是取色那一刻的快照：换肤之后按同一张表再取一遍，
        // 否则深色档会留着浅色档那份深描边，四枚图标在深底上等于看不见
        QObject::connect(&theme, &GyanisScribe::Gui::ThemeManager::themeChanged, combo,
                         [&icons, combo]
                         {
                             int index = 0;
                             for (const auto &choice: kThemeChoices)
                             {
                                 combo->setItemIcon(index, icons.icon(themeIconName(choice.kind)));
                                 ++index;
                             }
                         });
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
    QComboBox *makeOcrLanguageCombo(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        combo->addItems(GyanisScribe::Core::supportedOcrLanguageTags());
        combo->setAccessibleName(GyanisScribe::Base::uiText("识别语言"));
        const auto stored = settings.stringValue(GyanisScribe::Core::kOcrLanguage, QString::fromUtf8(GyanisScribe::Core::kDefaultOcrLanguage));
        if (const int index = combo->findText(stored); index >= 0)
        {
            combo->setCurrentIndex(index);
        }
        QObject::connect(combo, &QComboBox::currentTextChanged, combo,
                         [&settings, combo](const QString &text)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kOcrLanguage, text); !written)
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
    QComboBox *makeColorFormatCombo(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        combo->addItems(GyanisScribe::Core::colorFormatLabels());
        combo->setAccessibleName(GyanisScribe::Base::uiText("颜色复制格式"));
        const auto stored = GyanisScribe::Core::parseColorFormat(settings.stringValue(GyanisScribe::Core::kPickFormat, QStringLiteral("Hex")).toStdString());
        combo->setCurrentIndex(GyanisScribe::Core::indexOfColorFormat(stored ? *stored : GyanisScribe::Core::ColorFormat::Hex));
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings](int index)
                         {
                             const auto format = GyanisScribe::Core::colorFormatAt(index);
                             if (!format)
                             {
                                 return;
                             }
                             const auto token = QString::fromUtf8(GyanisScribe::Core::colorFormatToken(*format).data(),
                                                                  static_cast<qsizetype>(GyanisScribe::Core::colorFormatToken(*format).size()));
                             if (auto written = settings.setValue(GyanisScribe::Core::kPickFormat, token); !written)
                             {
                                 Logger::warning(std::format("颜色写法没能保存：{}", written.error().message));
                             }
                         });
        return combo;
    }

    /**
     * @brief 放大镜视野下拉：四档都来自 Pick 那张表，标记与界面名同源
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     * @details 这一档截图遮罩与屏幕取色共用：两处原本是同一枚放大镜的两个化身，
     *          各存一份就会出现「遮罩里关掉了、取色时还挡着」。
     */
    QComboBox *makeMagnifierViewCombo(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        for (const auto view: GyanisScribe::Core::magnifierViews())
        {
            combo->addItem(GyanisScribe::Core::magnifierViewLabel(view));
        }
        combo->setAccessibleName(GyanisScribe::Base::uiText("放大镜视野"));
        combo->setToolTip(GyanisScribe::Base::uiText("选择“不显示”仅隐藏像素放大图，光标处的坐标与颜色读数仍会保留。"));
        std::string complaint;
        const auto  stored =
                GyanisScribe::Core::magnifierViewOrDefault(settings.stringValue(GyanisScribe::Core::kCaptureMagnifierView, QStringLiteral("narrow")).toStdString(), complaint);
        if (!complaint.empty())
        {
            // 下拉框按顶上那一档摆，配置里那串原文不动：静默改写等于把用户填坏的证据抹掉
            Logger::warning(std::format("放大镜视野读不出：{}，下拉框按窄档摆", complaint));
        }
        const auto views = GyanisScribe::Core::magnifierViews();
        const auto at    = std::ranges::find(views, stored);
        combo->setCurrentIndex(at == views.end() ? 0 : static_cast<int>(std::distance(views.begin(), at)));
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings](const int index)
                         {
                             const auto views = GyanisScribe::Core::magnifierViews();
                             if (index < 0 || static_cast<std::size_t>(index) >= views.size())
                             {
                                 return;
                             }
                             const auto token = QString::fromUtf8(GyanisScribe::Core::magnifierViewToken(views[static_cast<std::size_t>(index)]).data());
                             if (auto written = settings.setValue(GyanisScribe::Core::kCaptureMagnifierView, token); !written)
                             {
                                 Logger::warning(std::format("放大镜视野没能保存：{}", written.error().message));
                             }
                         });
        return combo;
    }

    /**
     * @brief 遮罩上那几处数字按哪种像素量的一格
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     * @details 这一档管三处：遮罩上的尺寸读数、`V` 复制出去的那一串、`S` 尺寸框里填进去的那组数。
     *          只接一处就会出现「看到的与复制的不是同一组数」，而那只在高 DPI 屏上才看得出来。
     */
    QComboBox *makeSizeUnitCombo(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto                                              combo = new GyanisScribe::Gui::ComboBox;
        const std::array<GyanisScribe::Core::SizeUnit, 2> units{GyanisScribe::Core::SizeUnit::Logical, GyanisScribe::Core::SizeUnit::Physical};
        for (const auto unit: units)
        {
            combo->addItem(GyanisScribe::Core::sizeUnitLabel(unit));
        }
        combo->setAccessibleName(GyanisScribe::Base::uiText("尺寸单位"));
        combo->setToolTip(GyanisScribe::Base::uiText("高 DPI 屏幕上两种单位相差一个缩放倍率；此设置同时影响遮罩读数、复制尺寸与尺寸输入框。"));
        std::string complaint;
        const auto  stored = GyanisScribe::Core::sizeUnitOrDefault(settings.stringValue(GyanisScribe::Core::kCaptureSizeUnit, QStringLiteral("dip")).toStdString(), complaint);
        if (!complaint.empty())
        {
            Logger::warning(std::format("尺寸单位读不出：{}，下拉框按逻辑像素摆", complaint));
        }
        const auto at = std::ranges::find(units, stored);
        combo->setCurrentIndex(at == units.end() ? 0 : static_cast<int>(std::distance(units.begin(), at)));
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, units](const int index)
                         {
                             if (index < 0 || static_cast<std::size_t>(index) >= units.size())
                             {
                                 return;
                             }
                             const auto token = QString::fromUtf8(GyanisScribe::Core::sizeUnitToken(units[static_cast<std::size_t>(index)]).data());
                             if (auto written = settings.setValue(GyanisScribe::Core::kCaptureSizeUnit, token); !written)
                             {
                                 Logger::warning(std::format("尺寸单位没能保存：{}", written.error().message));
                             }
                         });
        return combo;
    }

    /**
     * @brief 托盘上那一下鼠标做什么的一格
     * @param settings 配置存储
     * @param role 左键单击还是中键单击
     * @param capabilities 平台能力表：这台机器做不出的动作不列进清单
     * @return 归调用方所有的下拉框
     * @details 清单前两格是「什么都不做」与「弹出托盘菜单」，其后照托盘菜单的顺序排，
     *          标签与菜单同源——同一件事在两处各写一遍名字，改一处就会分家。
     *          读坏了不替用户改写配置：下拉框按默认那一档摆，日志点名那串原文。
     */
    QComboBox *makeTrayClickCombo(GyanisScribe::Core::ISettingsStore &settings, const GyanisScribe::App::TrayClickRole role,
                                  const GyanisScribe::Platform::PlatformCapabilities &capabilities)
    {
        auto        combo   = new GyanisScribe::Gui::ComboBox;
        const auto &info    = GyanisScribe::App::trayClickRoleInfo(role);
        const auto  choices = GyanisScribe::App::trayClickChoices(capabilities);
        std::string complaint;
        const auto  decision = GyanisScribe::App::resolveTrayClick(settings, role, complaint);
        for (const auto &choice: choices)
        {
            combo->addItem(choice.label);
        }
        combo->setAccessibleName(GyanisScribe::Base::uiText(info.label));
        combo->setToolTip(GyanisScribe::Base::uiText("右键单击由系统弹出菜单；双击会在第一次松开时计为一次单击。本项仅控制左键或中键单击。"));
        if (!complaint.empty())
        {
            Logger::warning(std::format("托盘点击读不出：{}，下拉框按默认那一档摆", complaint));
        }
        const auto at = std::ranges::find(choices, decision.token, &GyanisScribe::App::TrayClickChoice::token);
        combo->setCurrentIndex(at == choices.end() ? 0 : static_cast<int>(std::distance(choices.begin(), at)));
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, key = std::string{info.key}, choices](const int index)
                         {
                             if (index < 0 || static_cast<std::size_t>(index) >= choices.size())
                             {
                                 return;
                             }
                             // 键是这一档自己的那一格，值才是选中的标记：拿标记当键写下去，配置里就多一条没人读的行
                             if (auto written = settings.setValue(key, QString::fromUtf8(choices[static_cast<std::size_t>(index)].token.data())); !written)
                             {
                                 Logger::warning(std::format("托盘点击没能保存：{}", written.error().message));
                             }
                         });
        return combo;
    }

    /**
     * @brief 新建贴图的默认不透明度（百分比）
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     */
    /**
     * @brief 贴图的吸附距离（逻辑像素）
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     * @details 0 是一档有意义的设置（不吸附），因此这里不能像「历史留多少条」那样把最小值显示成「不限」。
     */
    QSpinBox *makeEdgeSnapDistanceSpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(GyanisScribe::Core::kEdgeSnapDistanceMinimum, GyanisScribe::Core::kEdgeSnapDistanceMaximum);
        spin->setSuffix(GyanisScribe::Base::uiText(" 像素"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("吸附距离"));
        spin->setToolTip(GyanisScribe::Base::uiText("按住 Shift 拖动对齐与贴近屏幕边缘时共用的距离；0 表示不吸附。"));

        const auto title  = GyanisScribe::Base::uiText("吸附距离");
        const auto stored = settings.intValue(GyanisScribe::Core::kPinEdgeSnapDistance, GyanisScribe::Core::kEdgeSnapTolerance);
        spin->setValue(shownValueForSetting(title, stored, GyanisScribe::Core::parseEdgeSnapDistance(stored), GyanisScribe::Core::kEdgeSnapTolerance));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kPinEdgeSnapDistance, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 新建贴图的默认不透明度（百分比）
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     */
    QSpinBox *makeOpacitySpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        // 下限取 1：0% 的贴图在屏幕上看不见却还挡鼠标，那种「贴图功能坏了」的现象最难查
        spin->setRange(1, 100);
        spin->setSuffix(QStringLiteral(" %"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("默认不透明度"));

        const auto title  = GyanisScribe::Base::uiText("贴图默认不透明度");
        const auto stored = settings.intValue(GyanisScribe::Core::kPinDefaultOpacity, GyanisScribe::Core::kDefaultOpacityPercent);
        const auto parsed = GyanisScribe::Core::parseOpacityPercentSetting(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Core::kDefaultOpacityPercent));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kPinDefaultOpacity, value); !written)
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
    /**
     * @brief 「最长录多久」那一格（秒）
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     * @details 范围与 `parseMaximumDurationSetting` 同解（1 到两小时）：摆得出去的值得是开始那一刻收得下的值。
     *          步长给一分钟——这一格是按分钟想的（默认十分钟），但配置与校验都按秒，换算只在这一处步长里。
     */
    QSpinBox *makeMaximumDurationSpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(1, GyanisScribe::Core::kMaximumDurationSeconds);
        spin->setSingleStep(60);
        spin->setSuffix(GyanisScribe::Base::uiText(" 秒"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("最长录制时长"));
        spin->setToolTip(GyanisScribe::Base::uiText("录制达到此时长后自动结束并保存；默认 600 秒（10 分钟），上限 7200 秒（2 小时）。"));

        const auto title  = GyanisScribe::Base::uiText("最长录制时长");
        const auto stored = settings.intValue(GyanisScribe::Core::kRecordMaximumDuration, GyanisScribe::Core::kDefaultMaximumDurationSeconds);
        spin->setValue(shownValueForSetting(title, stored, GyanisScribe::Core::parseMaximumDurationSetting(stored), GyanisScribe::Core::kDefaultMaximumDurationSeconds));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kRecordMaximumDuration, value); !written)
                             {
                                 Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return spin;
    }

    QSpinBox *makeFrameRateSpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(1, GyanisScribe::Core::kMaximumFrameRate);
        spin->setSuffix(GyanisScribe::Base::uiText(" 帧/秒"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("默认帧率"));

        const auto title  = GyanisScribe::Base::uiText("录屏默认帧率");
        const auto stored = settings.intValue(GyanisScribe::Core::kRecordDefaultFrameRate, GyanisScribe::Core::kDefaultFrameRate);
        const auto parsed = GyanisScribe::Core::parseFrameRateSetting(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Core::kDefaultFrameRate));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kRecordDefaultFrameRate, value); !written)
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
    QSpinBox *makeCaptureDelaySpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(0, GyanisScribe::App::kMaximumCaptureDelaySeconds);
        spin->setSuffix(GyanisScribe::Base::uiText(" 秒"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("截图延时"));
        spin->setToolTip(GyanisScribe::Base::uiText("按下截图后等待此时间再开始，用于捕获菜单、悬停提示等短暂画面；0 表示不延时。"));

        const auto title  = GyanisScribe::Base::uiText("截图延时");
        const auto stored = settings.intValue(GyanisScribe::Core::kCaptureDelaySeconds, 0);
        const auto parsed = GyanisScribe::App::parseCaptureDelay(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, 0));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kCaptureDelaySeconds, value); !written)
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
    QSpinBox *makeCornerRadiusSpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(GyanisScribe::Core::kCaptureCornerRadiusMinimum, GyanisScribe::Core::kCaptureCornerRadiusMaximum);
        spin->setSuffix(GyanisScribe::Base::uiText(" 像素"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("圆角半径"));
        spin->setToolTip(GyanisScribe::Base::uiText("启用“圆角处理”时四角的半径；仅对区域截图生效，整屏、窗口与滚动长截图不应用。"));

        const auto title  = GyanisScribe::Base::uiText("圆角半径");
        const auto stored = settings.intValue(GyanisScribe::Core::kCaptureCornerRadius, GyanisScribe::Core::kCaptureCornerRadiusDefault);
        const auto parsed = GyanisScribe::Core::parseCaptureCornerRadius(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Core::kCaptureCornerRadiusDefault));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kCaptureCornerRadius, value); !written)
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
    QSpinBox *makeCountdownSpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(0, GyanisScribe::Core::kMaximumCountdownSeconds);
        spin->setSuffix(GyanisScribe::Base::uiText(" 秒"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("录屏倒计时"));
        spin->setToolTip(GyanisScribe::Base::uiText("按下录屏快捷键后倒计时此时长再开始录制，便于准备画面；0 表示立即开始。"));

        const auto title  = GyanisScribe::Base::uiText("录屏倒计时");
        const auto stored = settings.intValue(GyanisScribe::Core::kRecordCountdownSeconds, GyanisScribe::Core::kDefaultCountdownSeconds);
        const auto parsed = GyanisScribe::Core::parseCountdownSetting(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Core::kDefaultCountdownSeconds));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kRecordCountdownSeconds, value); !written)
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
    QSpinBox *makeVideoBitrateSpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(GyanisScribe::Core::kDefaultVideoBitrateKbps, GyanisScribe::Core::kMaximumVideoBitrateKbps);
        spin->setSingleStep(500);
        spin->setSuffix(GyanisScribe::Base::uiText(" kbps"));
        spin->setSpecialValueText(GyanisScribe::Base::uiText("按质量优先"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("画面码率"));

        const auto title  = GyanisScribe::Base::uiText("画面码率");
        const auto stored = settings.intValue(GyanisScribe::Core::kRecordVideoBitrate, GyanisScribe::Core::kDefaultVideoBitrateKbps);
        const auto parsed = GyanisScribe::Core::parseVideoBitrateSetting(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Core::kDefaultVideoBitrateKbps));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kRecordVideoBitrate, value); !written)
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
     * @brief 录屏范围下拉：档位、配置里的取值与设置页标签都取自 GyanisScribe::Core::kRecordScopeChoices
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     * @details 初值按 RecorderConfig 的 mode 初值回落，而不是在这里另写一档：
     *          两处不同解时，用户没动过这一格却录到与界面显示不符的画面。
     */
    QComboBox *makeRecordScopeCombo(GyanisScribe::Core::ISettingsStore &settings, const GyanisScribe::Gui::IconProvider &icons)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        for (const auto &choice: GyanisScribe::Core::kRecordScopeChoices)
        {
            // 图标名与标签同在词表里：三档录法的字形不一样，扫一眼就分得出，不必逐条读字
            combo->addItem(icons.icon(choice.iconName), GyanisScribe::Base::uiText(choice.label), tokenText(choice.token));
        }
        combo->setAccessibleName(GyanisScribe::Base::uiText("录制范围"));
        combo->setToolTip(GyanisScribe::Base::uiText("“上次选区”使用最近一次截图的选区；“当前窗口”录制开始时最前面的窗口。修改后从下次录屏起生效。"));

        const auto defaultScope = GyanisScribe::Core::RecorderConfig{}.mode;
        const auto stored       = settings.stringValue(GyanisScribe::Core::kRecordScope, tokenText(GyanisScribe::Core::recordScopeText(defaultScope)));
        const auto parsed       = GyanisScribe::Core::parseRecordScope(stored);
        if (!parsed)
        {
            // 读不出时下拉框显示默认档，但原因要落进日志：只把界面悄悄拨回整屏，用户会以为是自己选错的
            Logger::warning(std::format("设置页读不出录制范围：{}，下拉框按默认档显示", parsed.error().message));
        }
        const auto chosen = parsed ? *parsed : defaultScope;
        combo->setCurrentIndex(combo->findData(tokenText(GyanisScribe::Core::recordScopeText(chosen))));

        // 接线排在设初值之后：否则「把界面摆到位」这一步会被当成用户改动写回配置
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo](int)
                         {
                             const auto token = combo->currentData().toString();
                             if (auto written = settings.setValue(GyanisScribe::Core::kRecordScope, token); !written)
                             {
                                 Logger::warning(std::format("录制范围没能写入：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }

    /**
     * @brief 「截完之后做什么」的下拉框：档位、配置里的取值与标签都取自 GyanisScribe::App::kCaptureIntentChoices
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     * @details 初值按词表第一档回落，而不是在这里另写一档：两处不同解时，用户没动过这一格，
     *          按下热键走的却与界面显示的不是同一步。
     */
    QComboBox *makeCaptureIntentCombo(GyanisScribe::Core::ISettingsStore &settings, const GyanisScribe::Gui::IconProvider &icons)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        for (const auto &choice: GyanisScribe::App::kCaptureIntentChoices)
        {
            // 与录制范围那格同一条形状：图标名跟着词表走，界面只负责取
            combo->addItem(icons.icon(choice.iconName), GyanisScribe::Base::uiText(choice.label), tokenText(choice.token));
        }
        combo->setAccessibleName(GyanisScribe::Base::uiText("截图完成后"));
        combo->setToolTip(GyanisScribe::Base::uiText("选择“复制到剪贴板”“保存到文件”“贴到屏幕”时不会打开标注窗口；修改后从下次截图起生效。"));

        const auto stored =
                settings.stringValue(GyanisScribe::Core::kCaptureAfterAction, tokenText(GyanisScribe::App::captureIntentText(GyanisScribe::App::defaultCaptureIntent())));
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
                             if (auto written = settings.setValue(GyanisScribe::Core::kCaptureAfterAction, token); !written)
                             {
                                 Logger::warning(std::format("「截完之后」没能写入：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }
    /**
     * @brief 成品图片存成什么格式的下拉：档位、token 与后缀都在 GyanisScribe::Core::kImageFormatChoices 里
     * @param settings 配置存储
     * @return 归调用方所有的下拉框，由 addRow 挂上父对象
     */
    QComboBox *makeImageFormatCombo(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        // 只摆这台机器真写得出来的那几档：Qt 的图像编码器是插件，摆一档写不出的就是让用户白失败一次
        for (const auto format: GyanisScribe::Core::writableImageFormats())
        {
            const auto &choice = *std::ranges::find_if(GyanisScribe::Core::kImageFormatChoices, [format](const auto &row) { return row.format == format; });
            combo->addItem(GyanisScribe::Base::uiText(choice.label), tokenText(choice.token));
        }
        combo->setAccessibleName(GyanisScribe::Base::uiText("保存格式"));
        combo->setToolTip(GyanisScribe::Base::uiText("“保存到文件”与贴图右键“保存”按此格式写入文件；PNG 为无损格式，JPG 与 WebP 适合压缩照片类截图。"));

        const auto stored = settings.stringValue(GyanisScribe::Core::kSaveImageFormat, tokenText(GyanisScribe::Core::imageFormatToken(GyanisScribe::Core::ImageFormat::Png)));
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
                             if (auto written = settings.setValue(GyanisScribe::Core::kSaveImageFormat, token); !written)
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
    QSpinBox *makeImageQualitySpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(GyanisScribe::Core::kMinimumImageQuality, GyanisScribe::Core::kMaximumImageQuality);
        spin->setAccessibleName(GyanisScribe::Base::uiText("保存质量"));
        spin->setToolTip(GyanisScribe::Base::uiText("仅 JPG 与 WebP 使用此设置，PNG 为无损格式；数值越大越清晰，文件也越大。"));

        const auto stored = settings.intValue(GyanisScribe::Core::kSaveImageQuality, GyanisScribe::Core::kDefaultImageQuality);
        const auto parsed = GyanisScribe::Core::parseImageQuality(stored);
        if (!parsed)
        {
            Logger::warning(std::format("设置页读不出「保存质量」：{}，这一格按 {} 显示", parsed.error().message, GyanisScribe::Core::kDefaultImageQuality));
        }
        spin->setValue(parsed.value_or(GyanisScribe::Core::kDefaultImageQuality));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kSaveImageQuality, value); !written)
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
    QComboBox *makeAudioDeviceCombo(GyanisScribe::Core::ISettingsStore &settings, const std::string_view key, const QString &title,
                                    GyanisScribe::Plugins::FfmpegAudioDeviceEnumerator &enumerator)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        combo->addItem(GyanisScribe::Base::uiText("不录制"), QString());
        combo->setAccessibleName(title);

        const auto stored = settings.stringValue(key, QString());
        QObject::connect(&enumerator, &GyanisScribe::Plugins::FfmpegAudioDeviceEnumerator::updated, combo,
                         [combo, stored, title](const QVector<GyanisScribe::Plugins::AudioInputDevice> &devices)
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
        QObject::connect(&enumerator, &GyanisScribe::Plugins::FfmpegAudioDeviceEnumerator::failed, combo,
                         [combo, title](const GyanisScribe::Base::Error &error)
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
    QComboBox *makeVideoEncoderCombo(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Plugins::FfmpegVideoEncoderEnumerator &enumerator)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        combo->addItem(GyanisScribe::Base::uiText("自动（逐项检测）"), QString::fromUtf8(std::string{GyanisScribe::Core::kRecordVideoEncoderAutoToken}));
        combo->setAccessibleName(GyanisScribe::Base::uiText("视频编码器"));
        combo->setToolTip(GyanisScribe::Base::uiText("列出本机最近检测可用的编码器；自动模式会在每次编码前按偏好顺序逐项检测。"));

        const auto stored = settings.stringValue(GyanisScribe::Core::kRecordVideoEncoder, QString::fromUtf8(std::string{GyanisScribe::Core::kRecordVideoEncoderAutoToken}));
        QObject::connect(&enumerator, &GyanisScribe::Plugins::FfmpegVideoEncoderEnumerator::updated, combo,
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
        QObject::connect(&enumerator, &GyanisScribe::Plugins::FfmpegVideoEncoderEnumerator::failed, combo,
                         [combo](const GyanisScribe::Base::Error &error)
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
                             if (auto written = settings.setValue(GyanisScribe::Core::kRecordVideoEncoder, combo->currentData().toString()); !written)
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
     * @details GIF 的上限比 MP4 低（阈值单源：GyanisScribe::Core::kMaximumGifFrameRate，与 validateRecorderConfig 同解）：
     *          选了 GIF 就把这一格的上限收到 24，不让用户配出一个到录制那一刻才被拒的组合。
     *          显示的夹取不写回配置——切回 MP4 要还回用户原来填的那一档；配置里那份仍由
     *          validateRecorderConfig 在开录前把关（手改配置绕过界面时它兜着）。
     */
    void applyFrameRateAvailability(const QComboBox &formatCombo, QSpinBox &frameRateSpin, GyanisScribe::Core::ISettingsStore &settings)
    {
        const auto parsed = GyanisScribe::Core::parseRecordFormat(formatCombo.currentData().toString());
        const auto chosen = parsed ? *parsed : GyanisScribe::Core::RecorderConfig{}.format;
        const auto gif    = chosen == GyanisScribe::Core::OutputFormat::Gif;

        const auto maximum = gif ? GyanisScribe::Core::kMaximumGifFrameRate : GyanisScribe::Core::kMaximumFrameRate;
        const auto stored  = settings.intValue(GyanisScribe::Core::kRecordDefaultFrameRate, GyanisScribe::Core::kDefaultFrameRate);

        const QSignalBlocker blocker{frameRateSpin};
        frameRateSpin.setMaximum(maximum);
        frameRateSpin.setToolTip(gif ? GyanisScribe::Base::uiText("GIF 最高 24 帧/秒；更高帧率肉眼难以分辨，且会显著增加调色板处理量。切回 MP4 后恢复原有帧率。")
                                     : GyanisScribe::Base::uiText("录屏的目标帧率；GIF 格式上限低于 MP4（24 帧/秒）。"));
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
        const auto parsed       = GyanisScribe::Core::parseRecordFormat(formatCombo.currentData().toString());
        const auto chosen       = parsed ? *parsed : GyanisScribe::Core::RecorderConfig{}.format;
        const auto carriesAudio = GyanisScribe::Core::outputFormatCarriesAudio(chosen);
        microphoneCombo.setEnabled(carriesAudio);
        systemSoundCombo.setEnabled(carriesAudio);
    }

    /**
     * @brief 录屏输出格式下拉：档位、配置里的取值与设置页标签都取自 GyanisScribe::Core::kRecordFormatChoices
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     * @details 格式定错比范围定错更贵：帧已经抓完才按错的容器压出去，整段成品作废。
     *          因此界面这一格读不出时只回落显示，开始录屏那一头是直接拒绝的。
     */
    QComboBox *makeRecordFormatCombo(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        for (const auto &choice: GyanisScribe::Core::kRecordFormatChoices)
        {
            combo->addItem(GyanisScribe::Base::uiText(choice.label), tokenText(choice.token));
        }
        combo->setAccessibleName(GyanisScribe::Base::uiText("输出格式"));
        // 上限那一个数取自 kMaximumGifFrameRate：写死了就会在阈值调那天变成一句假话
        combo->setToolTip(GyanisScribe::Base::uiText("GIF 使用两遍调色板，帧率上限为 %1 帧/秒，低于 MP4；MP4 适合较长片段。修改后从下次录屏起生效。")
                                  .arg(GyanisScribe::Core::kMaximumGifFrameRate));

        const auto defaultFormat = GyanisScribe::Core::RecorderConfig{}.format;
        const auto stored        = settings.stringValue(GyanisScribe::Core::kRecordFormat, tokenText(GyanisScribe::Core::recordFormatText(defaultFormat)));
        const auto parsed        = GyanisScribe::Core::parseRecordFormat(stored);
        if (!parsed)
        {
            Logger::warning(std::format("设置页读不出输出格式：{}，下拉框按默认档显示", parsed.error().message));
        }
        const auto chosen = parsed ? *parsed : defaultFormat;
        combo->setCurrentIndex(combo->findData(tokenText(GyanisScribe::Core::recordFormatText(chosen))));

        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo](int)
                         {
                             const auto token = combo->currentData().toString();
                             if (auto written = settings.setValue(GyanisScribe::Core::kRecordFormat, token); !written)
                             {
                                 Logger::warning(std::format("输出格式没能写入：{}（当前选的是 {}）", written.error().message, combo->currentText().toStdString()));
                             }
                         });
        return combo;
    }

    /**
     * @brief 回溯缓冲的开启开关，带一次隐私确认
     * @param settings 配置存储
     * @return 归调用方所有的复选框
     * @details 打开与关闭不是对称的两步：打开后屏幕画面会留在内存里，所以先把边界摆给用户看，
     *          只有点「是」才写 true；点「否」把勾收回去，配置一个字都不动。关闭正相反，立刻写 false，
     *          协调器收到变更后清空缓冲并停止抓帧。
     */
    QCheckBox *makeRewindEnabledCheck(Core::ISettingsStore &settings)
    {
        auto *check = new GyanisScribe::Gui::CheckBox;
        check->setObjectName(QStringLiteral("rewindEnabled"));
        check->setText(GyanisScribe::Base::uiText("启用（仅使用内存）"));
        check->setAccessibleName(GyanisScribe::Base::uiText("启用回溯缓冲"));
        check->setToolTip(GyanisScribe::Base::uiText("仅保存在内存中，不写入磁盘、不上传；锁屏或退出程序时清空。"));
        check->setChecked(settings.boolValue(Core::kRewindEnabled, false));

        QObject::connect(check, &QCheckBox::toggled, check,
                         [&settings, check](const bool checked)
                         {
                             if (checked)
                             {
                                 // 分段查表再用换行拼起来：把 \n 写进 uiText 的字面量时，目录里存的是转义文本，运行期查不中
                                 const QString privacy = GyanisScribe::Base::uiText("将最近 10 秒的屏幕画面保存在内存中。") + QLatin1Char('\n') +
                                                         GyanisScribe::Base::uiText("不写入磁盘、不上传、不录制声音。") + QLatin1Char('\n') +
                                                         GyanisScribe::Base::uiText("锁屏或退出程序时清空。") + QLatin1Char('\n') +
                                                         GyanisScribe::Base::uiText("可排除密码管理器等程序。") + QStringLiteral("\n\n") +
                                                         GyanisScribe::Base::uiText("确定启用吗？");
                                 const auto    answer =
                                         QMessageBox::question(check, GyanisScribe::Base::uiText("启用回溯缓冲"), privacy, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                                 if (answer != QMessageBox::Yes)
                                 {
                                     const QSignalBlocker blocker(check);
                                     check->setChecked(false);
                                     return;
                                 }
                             }
                             if (auto written = settings.setValue(Core::kRewindEnabled, checked); !written)
                             {
                                 Logger::warning(std::format("回溯开关没能写入：{}", written.error().message));
                             }
                         });
        return check;
    }

    /**
     * @brief 回溯缓冲保留多久（秒）
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     * @details 范围取 Core 里与读端共用的那两个常量：界面能给出的值，协调器都必须收得下。
     */
    QSpinBox *makeRewindBufferSecondsSpin(Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setObjectName(QStringLiteral("rewindBufferSeconds"));
        spin->setRange(Core::kMinimumRewindBufferSeconds, Core::kMaximumRewindBufferSeconds);
        spin->setSuffix(GyanisScribe::Base::uiText(" 秒"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("缓冲保留时长"));
        spin->setValue(settings.intValue(Core::kRewindBufferSeconds, Core::kDefaultRewindBufferSeconds));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings](const int value)
                         {
                             if (auto written = settings.setValue(Core::kRewindBufferSeconds, value); !written)
                             {
                                 Logger::warning(std::format("回溯时长没能写入：{}", written.error().message));
                             }
                         });
        return spin;
    }

    /**
     * @brief 回溯缓冲的目标帧率
     * @param settings 配置存储
     * @return 归调用方所有的下拉框
     * @details 只摆三档：回溯是回看，过密的帧只会更快吃满固定的内存预算。
     */
    QComboBox *makeRewindFrameRateCombo(Core::ISettingsStore &settings)
    {
        auto *combo = new GyanisScribe::Gui::ComboBox;
        combo->setObjectName(QStringLiteral("rewindFrameRate"));
        combo->setAccessibleName(GyanisScribe::Base::uiText("缓冲帧率"));
        for (const int rate: {Core::kMinimumRewindFrameRate, Core::kDefaultRewindFrameRate, Core::kMaximumRewindFrameRate})
        {
            combo->addItem(QString::number(rate), rate);
        }

        const int stored = settings.intValue(Core::kRewindFrameRate, Core::kDefaultRewindFrameRate);
        const int index  = combo->findData(stored);
        combo->setCurrentIndex(index >= 0 ? index : combo->findData(Core::kDefaultRewindFrameRate));

        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [&settings, combo]
                         {
                             if (auto written = settings.setValue(Core::kRewindFrameRate, combo->currentData()); !written)
                             {
                                 Logger::warning(std::format("回溯帧率没能写入：{}", written.error().message));
                             }
                         });
        return combo;
    }

    /**
     * @brief 回溯缓冲的排除程序名单
     * @param settings 配置存储
     * @return 归调用方所有的输入框
     * @details 写入前先按协调器读配置用的同一个解析函数校验：非法项不落盘并当场说明正确写法，
     *          否则协调器下一次启动会因这份名单拒绝启动，而用户只在日志里看得到原因。
     */
    QLineEdit *makeRewindExclusionEdit(Core::ISettingsStore &settings)
    {
        auto *edit = new GyanisScribe::Gui::LineEdit;
        edit->setObjectName(QStringLiteral("rewindExcludeProcesses"));
        edit->setPlaceholderText(GyanisScribe::Base::uiText("keepass.exe, 1password.exe"));
        edit->setAccessibleName(GyanisScribe::Base::uiText("排除的程序"));
        edit->setToolTip(GyanisScribe::Base::uiText("以逗号分隔的可执行文件名；名单中的程序有可见窗口时暂停抓取画面。"));
        edit->setText(settings.stringValue(Core::kRewindExcludeProcesses, QString{}));

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [&settings, edit]
                         {
                             const auto parsed = Core::parseRewindExclusions(edit->text());
                             if (!parsed)
                             {
                                 QMessageBox::warning(edit, GyanisScribe::Base::uiText("排除名单无效"), QString::fromStdString(parsed.error().message));
                                 return;
                             }
                             if (auto written = settings.setValue(Core::kRewindExcludeProcesses, edit->text()); !written)
                             {
                                 Logger::warning(std::format("回溯排除名单没能写入：{}", written.error().message));
                             }
                         });
        return edit;
    }
} // namespace

namespace GyanisScribe::Main
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
        // 整段是一条字面量：翻译目录按整段查，拆成相邻字面量会让目录里只收到第一片。
        // 正文是富文本：产品名与版本各占一行，链接由显示它的标签接 linkActivated 打开
        return GyanisScribe::Base::uiText("<b style=\"font-size:15pt\">智录 GyanisScribe</b><br>"
                                          "版本 %1<br>"
                                          "<br>"
                                          "截图、标注、贴图、取色、录屏与文字识别的桌面效率工具。<br>"
                                          "界面与图标全部为本项目原创；本软件免费供任何人使用。<br>"
                                          "<br>"
                                          "仓库：<a href=\"https://github.com/Gyanis9/GyanisScribe\">github.com/Gyanis9/GyanisScribe</a><br>"
                                          "第三方组件与其许可证见仓库 docs/ThirdPartyLicenses.md")
                .arg(version);
    }

    QString updateStatusHtml(const GyanisScribe::App::UpdateVerdict &verdict)
    {
        const auto message = verdict.message.toHtmlEscaped();
        const QUrl link{verdict.link};
        const auto scheme = link.scheme();
        if (!link.isValid() || link.host().isEmpty() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")))
        {
            // 不是能交给系统浏览器的地址就当纯文本：硬套一个 <a> 只会多出一个点了没反应的链接
            return message;
        }
        return QStringLiteral("<a href=\"%1\">%2</a>").arg(verdict.link.toHtmlEscaped(), message);
    }

    MainWindow::~MainWindow()
    {
        // 退出前把几何存一份：下次开在同一块屏、同一尺寸上。
        // 存成 Base64 文本而不是 QByteArray：配置导出走 JSON，二进制载荷过不了那一道
        const auto saved = m_settings.setValue(Core::kMainWindowGeometry, QString::fromLatin1(saveGeometry().toBase64()));
        if (!saved)
        {
            Logger::warning(std::format("主窗口几何没能存下：{}", saved.error().message));
        }
    }

    MainWindow::MainWindow(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Gui::ThemeManager &theme, GyanisScribe::Gui::IconProvider &icons,
                           GyanisScribe::Core::HistoryStore &historyStore, GyanisScribe::Core::MemoryIndex &memoryIndex, GyanisScribe::Platform::PlatformCapabilities capabilities,
                           GyanisScribe::Platform::IPlatformIntegration &integration, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_theme(theme), m_icons(icons), m_capabilities(capabilities),
        m_audioDevices(std::make_unique<GyanisScribe::Plugins::FfmpegAudioDeviceEnumerator>()),
        m_videoEncoders(std::make_unique<GyanisScribe::Plugins::FfmpegVideoEncoderEnumerator>()), m_integration(integration), m_historyStore(historyStore),
        m_memoryIndex(memoryIndex)
    {
        setWindowTitle(GyanisScribe::Base::uiText("智录 设置"));
        setWindowIcon(icons.brandIcon());
        // 根容器要自己铺底：样式表只给具名的根与卡片上色，不铺裸 QWidget
        setObjectName(QStringLiteral("appRoot"));
        setAttribute(Qt::WA_StyledBackground, true);

        // 根是竖的两段：上面一条常设动作条，下面才是「导航 | 页」这两列。
        // 动作条是给「没有托盘、热键也没登记上」的那一类机器留的常设入口，
        // 也是有托盘时不用先去右下角翻菜单的那条 shortcut。
        auto *root = new QVBoxLayout{this};
        root->setContentsMargins(GyanisScribe::Gui::kSpacingMedium, GyanisScribe::Gui::kSpacingMedium, GyanisScribe::Gui::kSpacingMedium, GyanisScribe::Gui::kSpacingMedium);
        root->setSpacing(GyanisScribe::Gui::kSpacingSmall);

        auto *layout = new QHBoxLayout;
        layout->setSpacing(GyanisScribe::Gui::kSpacingMedium);
        auto *nav = new Gui::ListWidget{this};
        // 导航压在设置页的面板上，容器底不铺：铺了就是多一层与卡片同色的方块
        nav->setSurfaceVisible(false);
        m_nav   = nav;
        m_stack = new QStackedWidget{this};
        m_nav->setObjectName(QStringLiteral("settingsNav"));
        m_nav->setIconSize(QSize(16, 16));
        m_nav->setFixedWidth(kNavColumnWidth);
        m_nav->setAccessibleName(GyanisScribe::Base::uiText("设置导航"));
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
            decoratePage(*content, GyanisScribe::Base::uiText(choice.label));
            auto *entry = new QListWidgetItem{m_icons.icon(choice.icon), GyanisScribe::Base::uiText(choice.label)};
            // 图标名留在行上：QIcon 是取色那一刻的快照，换主题时要按这个名字再取一次
            entry->setData(kNavIconNameRole, QString::fromUtf8(choice.icon));
            m_nav->addItem(entry);
            // 每一页都裹一层滚动区：快捷键那一页现在有四十来行，默认窗口高度摆不下——
            // 不裹就是底下那一截永远够不着，而且一声不吭
            auto *scroll = wrapSettingsPage(*content);
            // Tab 会停在滚动区上（键一停就能用方向键/PageDown 滚页），读屏也该报出这是哪一页
            scroll->setAccessibleName(GyanisScribe::Base::uiText(choice.label));
            m_stack->addWidget(scroll);
        }

        // 换主题时重取图标：样式表与调色板都会自己跟上，只有已经发出去的 QIcon 需要按名字再取一次
        QObject::connect(&m_theme, &GyanisScribe::Gui::ThemeManager::themeChanged, this,
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
        // 导航行停到记忆库也算「进了这一页」：托盘、导航与 open() 三条路都要走到清理与重读，
        // 不能只有托盘那条经过控制器；行号与页的对应关系仍只认 pageChoices()
        QObject::connect(m_nav, &QListWidget::currentRowChanged, this,
                         [this](const int row)
                         {
                             if (row == indexOfMainWindowPage(MainWindowPage::Memory))
                             {
                                 emit memoryPageShown();
                             }
                         });
        m_nav->setCurrentRow(0);

        // 标签列宽与字段上限先落到位，再量最宽一页：窗口最小宽必须装得下那一页，
        // 否则横向滚动条关着，右侧控件会被直接裁掉而且没有任何提示
        const int contentWidth = applySettingsFormMetrics(*m_stack);
        const int chromeWidth  = kNavColumnWidth + GyanisScribe::Gui::kSpacingMedium + 2 * GyanisScribe::Gui::kSpacingMedium + GyanisScribe::Gui::kSpacingLarge;
        m_settingsMinimumWidth = std::max(GyanisScribe::Gui::kSettingsWindowMinimumWidth, contentWidth + chromeWidth);

        layout->addWidget(m_nav);
        layout->addWidget(m_stack, 1);
        m_actionBar = buildActionBar();
        root->addWidget(m_actionBar);
        root->addLayout(layout);
        setMinimumSize(m_settingsMinimumWidth, GyanisScribe::Gui::kSettingsWindowMinimumHeight);
        resize(std::max(GyanisScribe::Gui::kSettingsWindowDefaultWidth, minimumWidth()), GyanisScribe::Gui::kSettingsWindowDefaultHeight);

        // 上次退出时的几何：没存过或存坏了就停在上面这档默认尺寸。
        // restoreGeometry 自己会把越界窗口挪回某块屏的可见区，换了屏也不会开在看不见的地方
        if (const auto stored = QByteArray::fromBase64(m_settings.stringValue(Core::kMainWindowGeometry, QString{}).toLatin1()); !stored.isEmpty())
        {
            restoreGeometry(stored);
        }
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
        const bool rowChanged = m_nav->currentRow() != index;
        m_nav->setCurrentRow(index);
        m_stack->setCurrentIndex(index);
        // 切行那一次已经由导航行报过进入这一页；停在原地再打开同一页时那一路不发信号，这里补上，
        // 于是一次打开只报一次
        if (!rowChanged && page == MainWindowPage::Memory)
        {
            emit memoryPageShown();
        }
        show();
        raise();
        activateWindow();
    }

    void MainWindow::setTrayMenuAvailable(const bool available)
    {
        if (m_actionBar != nullptr)
        {
            m_actionBar->setVisible(!available);
            updateMinimumWindowWidth();
        }
    }

    void MainWindow::updateMinimumWindowWidth()
    {
        int requiredWidth = m_settingsMinimumWidth;
        if (m_actionBar != nullptr && !m_actionBar->isHidden())
        {
            requiredWidth = std::max(requiredWidth, m_actionBar->sizeHint().width() + 2 * GyanisScribe::Gui::kSpacingMedium);
        }
        requiredWidth = std::max(requiredWidth, GyanisScribe::Gui::kSettingsWindowMinimumWidth);
        setMinimumWidth(requiredWidth);
        if (width() < requiredWidth)
        {
            resize(requiredWidth, height());
        }
    }

    void MainWindow::exportSettingsToFile(const QString &filePath)
    {
        // 界面里的路径串换回路径要走 wstring：Windows 上 narrow 那一串按本地 ANSI 码页解释，
        // 中文目录名会被读成另一条不存在的路（GyanisScribe::Base::Paths::fromDisplayText 是同一条规则的唯一出处）
        const auto target   = GyanisScribe::Base::Paths::fromDisplayText(filePath);
        const auto exported = m_settings.exportToFile(target);
        if (!exported)
        {
            Logger::warning(std::format("配置导出失败：{}", exported.error().message));
            emit messageForUser(QString::fromUtf8(exported.error().message.c_str()), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        // 条数从存储现取：导出的就是这一份的全部键，界面这层不再数一遍别的口径
        const auto count = static_cast<int>(m_settings.allKeys().size());
        Logger::info(std::format("配置导出完成：{} 项写到 {}", count, GyanisScribe::Base::Paths::toDisplayText(target).toStdString()));
        emit messageForUser(GyanisScribe::Base::uiText("已导出 %1 项设置").arg(count), GyanisScribe::Gui::ToastKind::Success);
    }

    void MainWindow::importSettingsFromFile(const QString &filePath)
    {
        const auto source   = GyanisScribe::Base::Paths::fromDisplayText(filePath);
        const auto imported = m_settings.importFromFile(source);
        if (!imported)
        {
            Logger::warning(std::format("配置导入失败：{}", imported.error().message));
            emit messageForUser(QString::fromUtf8(imported.error().message.c_str()), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        // 逐项走的仍是 setValue，订阅配置的模块（热键、主题）已经收到通知；这里只把改动刷到盘上
        m_settings.sync();
        Logger::info(std::format("配置导入完成：{} 项来自 {}", *imported, GyanisScribe::Base::Paths::toDisplayText(source).toStdString()));
        emit messageForUser(GyanisScribe::Base::uiText("已导入 %1 项设置。当前页面仍显示导入前的值，请关闭并重新打开设置窗口；界面语言需重启智录后生效。").arg(*imported),
                            GyanisScribe::Gui::ToastKind::Success);
    }

    void MainWindow::exportPaletteToFile(const QString &filePath)
    {
        const auto palette = GyanisScribe::Core::annotationPalette(m_settings);
        if (!palette)
        {
            // 手改过的配置里那份坏名单：报原样原因，而不是拿内置那八格冒充「你存的色板」导出去
            Logger::warning(std::format("色板导出失败：{}", palette.error().message));
            emit messageForUser(QString::fromUtf8(palette.error().message.c_str()), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        QFile file{filePath};
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            const auto reason = GyanisScribe::Base::uiText("无法写入色板文件：目标路径不可写。请选择可写目录后重试。");
            Logger::warning(std::format("色板写不下去：{}（{}）", reason.toStdString(), filePath.toStdString()));
            emit messageForUser(reason, GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        const auto text = GyanisScribe::Core::paletteFileText(*palette);
        file.write(text.toUtf8());
        file.close();

        Logger::info(std::format("色板已导出：{} 格写到 {}", palette->size(), filePath.toStdString()));
        emit messageForUser(GyanisScribe::Base::uiText("已导出色板：%1 种颜色").arg(static_cast<int>(palette->size())), GyanisScribe::Gui::ToastKind::Success);
    }

    void MainWindow::importPaletteFromFile(const QString &filePath)
    {
        QFile file{filePath};
        if (!file.open(QIODevice::ReadOnly))
        {
            const auto reason = GyanisScribe::Base::uiText("无法读取色板文件：路径“%1”不存在或无法打开。请确认路径，或重新导出。").arg(filePath);
            Logger::warning(std::format("色板读不到：{}", filePath.toStdString()));
            emit messageForUser(reason, GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        const auto bytes  = file.readAll();
        const auto parsed = GyanisScribe::Core::parsePaletteFileText(QString::fromUtf8(bytes.constData(), static_cast<qsizetype>(bytes.size())));
        if (!parsed)
        {
            Logger::warning(std::format("色板导入被拒：{}", parsed.error().message));
            emit messageForUser(QString::fromUtf8(parsed.error().message.c_str()), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        // 整份替换：追加会把两次导入变成两份重复色块；写坏的那一次保持旧名单，界面上不会出现「半份新色板」
        const auto written = m_settings.setValue(GyanisScribe::Core::kAnnotationPalette, GyanisScribe::Core::paletteToTokens(*parsed));
        if (!written)
        {
            Logger::warning(std::format("色板没能写入：{}", written.error().message));
            emit messageForUser(QString::fromUtf8(written.error().message.c_str()), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        m_settings.sync();
        Logger::info(std::format("色板已导入：{} 格来自 {}", parsed->size(), filePath.toStdString()));
        emit messageForUser(GyanisScribe::Base::uiText("已导入色板：%1 种颜色。工具栏将在下次打开标注窗口时使用新色板。").arg(static_cast<int>(parsed->size())),
                            GyanisScribe::Gui::ToastKind::Success);
    }

    QWidget *MainWindow::buildTransferRow()
    {
        auto *row    = new QWidget{this};
        auto *layout = new QHBoxLayout{row};
        layout->setContentsMargins(0, 0, 0, 0);

        auto *exportButton = new GyanisScribe::Gui::Button{GyanisScribe::Base::uiText("导出配置…"), row};
        m_icons.applyIcon(*exportButton, "Export");
        exportButton->setAccessibleName(GyanisScribe::Base::uiText("导出配置"));
        exportButton->setToolTip(GyanisScribe::Base::uiText("将当前全部设置写入一个 JSON 文件，用于迁移或备份。"));

        auto *importButton = new GyanisScribe::Gui::Button{GyanisScribe::Base::uiText("导入配置…"), row};
        m_icons.applyIcon(*importButton, "Import");
        importButton->setAccessibleName(GyanisScribe::Base::uiText("导入配置"));
        importButton->setToolTip(GyanisScribe::Base::uiText("从本程序导出的 JSON 文件恢复设置；热键与主题等订阅配置的模块会立即应用新值。"));

        // 对话框取消时 picked 是空串：那不是失败，一句话都不多说
        QObject::connect(exportButton, &QPushButton::clicked, row,
                         [this]
                         {
                             const auto suggested = GyanisScribe::Base::Paths::toDisplayText(GyanisScribe::Base::Paths::applicationDataDirectory() / "GyanisScribe-settings.json");
                             const auto picked =
                                     QFileDialog::getSaveFileName(this, GyanisScribe::Base::uiText("导出配置"), suggested, GyanisScribe::Base::uiText("配置文件 (*.json)"));
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
                                     QFileDialog::getOpenFileName(this, GyanisScribe::Base::uiText("导入配置"), QDir::homePath(), GyanisScribe::Base::uiText("配置文件 (*.json)"));
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
            case MainWindowPage::Memory:
                return buildMemoryPage();
        }
        return nullptr;
    }

    std::vector<QString> screenCaptureLabels(const QStringList &screenNames)
    {
        // 单屏上没有「另一块」可点：这一组只在多屏时出现，否则就是给每台机器都多摆一条同义项
        std::vector<QString> labels;
        if (screenNames.size() <= 1)
        {
            return labels;
        }
        labels.reserve(static_cast<std::size_t>(screenNames.size()));
        for (const auto &name: screenNames)
        {
            labels.push_back(GyanisScribe::Base::uiText("整屏截图：%1").arg(name));
        }
        return labels;
    }

    QWidget *MainWindow::buildActionBar()
    {
        auto *bar = new QWidget{this};
        bar->setObjectName(QStringLiteral("actionBar"));
        auto *row = new QHBoxLayout{bar};
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(GyanisScribe::Gui::kSpacingSmall);

        auto *moreMenu = new GyanisScribe::Gui::Menu{bar};
        auto  menuFont = moreMenu->font();
        menuFont.setPointSize(GyanisScribe::Gui::kMenuTextPointSize);
        moreMenu->setFont(menuFont);
        using GyanisScribe::App::TrayMenuPlacement;
        for (const auto &entry: GyanisScribe::App::visibleTrayMenuEntries(m_capabilities))
        {
            const auto action = entry.action;
            if (!action)
            {
                // 表里的分隔线在这一条上没有对应物：分格由行距自己管
                continue;
            }
            if (*action == GyanisScribe::App::TrayAction::OpenSettings || *action == GyanisScribe::App::TrayAction::OpenAbout || *action == GyanisScribe::App::TrayAction::Quit)
            {
                // 这三条说的就是这扇窗口：设置与关于是它的内容，退出在没有托盘的机器上是「关掉窗子」
                continue;
            }
            if (entry.placement == TrayMenuPlacement::More)
            {
                // 托盘里收进「更多」的那批，在无托盘的动作条上也落在同一个子菜单
                auto *item = moreMenu->addAction(entry.label);
                if (!entry.iconName.empty())
                {
                    item->setIcon(m_icons.icon(entry.iconName));
                }
                QObject::connect(item, &QAction::triggered, this, [this, action] { emit actionRequested(*action); });
                continue;
            }
            auto *button = new GyanisScribe::Gui::Button{entry.label, bar};
            m_icons.applyIcon(*button, entry.iconName);
            button->setAccessibleName(entry.label);
            button->setToolTip(entry.hotkeyKey.empty() ? GyanisScribe::Base::uiText("与托盘菜单执行相同操作。")
                                                        : GyanisScribe::Base::uiText("与托盘菜单及全局快捷键执行相同操作。"));
            QObject::connect(button, &QPushButton::clicked, this, [this, action] { emit actionRequested(*action); });
            row->addWidget(button);
        }

        // 多屏时给「整屏截图」补一份屏幕清单：这一组服务的是没有托盘的机器（托盘在场时动作条整条收起，
        // 逐屏截图改走托盘的子菜单）；托盘装不上时，它就是点名另一块屏的入口
        QStringList screenNames;
        for (const auto *screen: QGuiApplication::screens())
        {
            screenNames.append(screen->name());
        }
        const auto screenLabels = screenCaptureLabels(screenNames);
        if (!screenLabels.empty())
        {
            moreMenu->addSeparator();
            for (std::size_t index = 0; index < screenLabels.size(); ++index)
            {
                const QString &name = screenNames[static_cast<qsizetype>(index)];
                auto          *item = moreMenu->addAction(screenLabels[index]);
                QObject::connect(item, &QAction::triggered, this, [this, name] { emit captureScreenRequested(name); });
            }
        }

        auto *more = new GyanisScribe::Gui::ToolButton{bar};
        more->setText(GyanisScribe::Base::uiText("更多"));
        more->setAccessibleName(GyanisScribe::Base::uiText("更多"));
        more->setPopupMode(QToolButton::InstantPopup);
        more->setMenu(moreMenu);
        m_icons.applyIcon(*more, "More");
        row->addWidget(more);
        row->addStretch();
        return bar;
    }

    /**
     * @brief 开机自启动那一格：状态从系统里现读，失败就不给点并把原因写进提示
     * @details 不在配置里另存一份状态——两份真源迟早对不上（用户在系统「启动」设置里手动改过之后，
     *          配置里那个勾就会说谎）。读不到就问平台为什么不支持，把它的原话摆进提示里。
     */
    QCheckBox *makeAutostartCheck(GyanisScribe::Platform::IPlatformIntegration &integration)
    {
        auto *check = new GyanisScribe::Gui::CheckBox;
        check->setText(GyanisScribe::Base::uiText("登录系统后自动启动本程序"));
        check->setAccessibleName(GyanisScribe::Base::uiText("开机时自动启动"));

        const auto state = integration.autostartEnabled();
        if (!state)
        {
            check->setEnabled(false);
            check->setToolTip(QString::fromStdString(state.error().message));
            return check;
        }

        check->setChecked(*state);
        check->setToolTip(GyanisScribe::Base::uiText("登录系统后自动启动本程序；状态读取系统启动项，修改后立即生效。"));

        // 接线排在设初值之后：摆到位不等于用户改过，否则一开设置页就把状态写回一次
        QObject::connect(check, &QCheckBox::toggled, check,
                         [check, &integration](const bool enabled)
                         {
                             if (const auto applied = integration.setAutostart(enabled); !applied)
                             {
                                 GyanisScribe::Base::Logger::warning(std::format("开机启动项没能改：{}", applied.error().message));
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
    /**
     * @brief 「不参与窗口检测的程序」那一格：逗号分隔的可执行文件名
     * @param settings 配置存储
     * @return 归调用方所有的输入框
     * @details 这一格没有「填坏」的面：认不出的名字只是排不掉任何窗口，原文照存；
     *          大小写在解析那一层统一（见 `GyanisScribe::Core::parseProcessExcludes`），这里不做改写。
     */
    QLineEdit *makeDetectionExcludesEdit(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *edit = new GyanisScribe::Gui::LineEdit;
        edit->setText(settings.stringValue(GyanisScribe::Core::kCaptureDetectionExcludes, QString{}));
        edit->setPlaceholderText(GyanisScribe::Base::uiText("留空表示不排除任何程序，例如 chrome.exe, cmd.exe"));
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Base::uiText("窗口检测排除程序"));
        edit->setToolTip(GyanisScribe::Base::uiText("列出的程序在悬停识别窗口时会被跳过，适用于常驻桌面的工具栏或笔记窗口。"));

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [&settings, edit]
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kCaptureDetectionExcludes, edit->text()); !written)
                             {
                                 Logger::warning(std::format("不参与窗口检测的程序没能保存：{}", written.error().message));
                             }
                         });
        return edit;
    }

    QLineEdit *makeGroupRotationEdit(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *edit = new GyanisScribe::Gui::LineEdit;
        edit->setText(settings.stringListValue(GyanisScribe::Core::kPinGroupRotationOrder, {}).join(QStringLiteral(", ")));
        edit->setPlaceholderText(GyanisScribe::Base::uiText("留空表示按组号升序，例如 2, 1, 3"));
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Base::uiText("分组轮换顺序"));

        const auto explainHint = [edit](const QString &reason)
        { edit->setToolTip(reason.isEmpty() ? GyanisScribe::Base::uiText("切换分组时按此顺序轮换；未列出的分组排在末尾。") : reason); };
        explainHint(QString{});

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [&settings, edit, explainHint]
                         {
                             const auto tokens = edit->text().split(QChar{','}, Qt::SkipEmptyParts);
                             const auto parsed = GyanisScribe::Core::parseGroupRotation(tokens);
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
                             if (const auto written = settings.setValue(GyanisScribe::Core::kPinGroupRotationOrder, canonical); !written)
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
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *form =
                makePageForm(page, GyanisScribe::Base::uiText("界面语言决定菜单、托盘提示与设置页文案，切换后需重启智录生效。历史记录条数上限与保留天数设为 0 时表示不限制。"),
                             GyanisScribe::Base::uiText("界面与语言"));
        form->addRow(GyanisScribe::Base::uiText("界面语言"), makeUiLanguageCombo(m_settings));
        form = addSettingsSection(form, GyanisScribe::Base::uiText("历史记录"));
        // 保留策略的读端在 GyanisScribe::Core::policyFromSettings，而那条负数提示写着「请在设置的通用页改回来」——
        // 这一页没有这两格时，那句提示就是把用户指到一个不存在的地方
        form->addRow(GyanisScribe::Base::uiText("历史记录条数上限"),
                     makeOptionalLimitSpin(m_settings, GyanisScribe::Core::kHistoryMaximumEntries, GyanisScribe::Base::uiText("历史记录条数上限"),
                                           GyanisScribe::Base::uiText(" 条"), kMaximumEntriesLimit, GyanisScribe::Core::kDefaultMaximumEntries));
        form->addRow(GyanisScribe::Base::uiText("历史记录保留天数"),
                     makeOptionalLimitSpin(m_settings, GyanisScribe::Core::kHistoryRetentionDays, GyanisScribe::Base::uiText("历史记录保留天数"), GyanisScribe::Base::uiText(" 天"),
                                           kRetentionDaysLimit, GyanisScribe::Core::kDefaultRetentionDays));
        form->addRow(GyanisScribe::Base::uiText("保留未保存的截图"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kHistoryKeepUnsaved, false, GyanisScribe::Base::uiText("存入历史记录"),
                                      GyanisScribe::Base::uiText("保留未保存的截图"),
                                      QString::fromUtf8(
                                              GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8(
                                                                                   "启用后，每张截图都会在程序数据目录保留一份像素副本；因按 Esc 取消而丢弃的截图也会进入历史记录（最多保留 {} 张）"),
                                                                           static_cast<int>(GyanisScribe::Core::kUnsavedArchiveCapacity))
                                                      .c_str())));
        // 记忆库的功能开关摆在历史那几格旁边：两者都是「截过的图去哪了」。
        // 它只是功能开关，不是整页禁用——用 QWidget::setEnabled 会把开回来的入口一起灰掉
        m_memoryToggle = new GyanisScribe::Gui::CheckBox;
        m_memoryToggle->setText(GyanisScribe::Base::uiText("仅为手动截图建立索引"));
        m_memoryToggle->setAccessibleName(GyanisScribe::Base::uiText("截图记忆"));
        m_memoryToggle->setToolTip(GyanisScribe::Base::uiText("启用后，仅将手动截图的文字与来源写入本机索引，不会上传。可随时关闭；已有索引仍可搜索，也可清空。"));
        m_memoryToggle->setChecked(m_memoryEnabled);
        QObject::connect(m_memoryToggle, &QCheckBox::toggled, this, &MainWindow::memoryEnabledToggled);
        form->addRow(GyanisScribe::Base::uiText("截图记忆"), m_memoryToggle);
        form = addSettingsSection(form, GyanisScribe::Base::uiText("鼠标与行为"));
        form->addRow(GyanisScribe::Base::uiText("分组轮换顺序"), makeGroupRotationEdit(m_settings));
        // 托盘那两下：这一族动作在菜单、热键与鼠标点击三处都走同一个出口，这里只多两个入口而不多一份实现
        for (const auto &role: GyanisScribe::App::trayClickRoles())
        {
            form->addRow(GyanisScribe::Base::uiText(role.label), makeTrayClickCombo(m_settings, role.role, m_capabilities));
        }
        form->addRow(GyanisScribe::Base::uiText("开机自启动"), makeAutostartCheck(m_integration));
        form->addRow(GyanisScribe::Base::uiText("步骤截图"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kStepsAutoRedact, false, GyanisScribe::Base::uiText("录制结束后自动打码"),
                                      GyanisScribe::Base::uiText("自动打码步骤截图"),
                                      GyanisScribe::Base::uiText("录制结束后自动检查并打码本次的步骤截图；默认关闭，关闭时可在审核窗里自行删改。")));
        form = addSettingsSection(form, GyanisScribe::Base::uiText("数据与备份"));
        form->addRow(GyanisScribe::Base::uiText("备份与恢复"), buildTransferRow());
        return page;
    }

    /**
     * @brief 成品文件名模板那一格：留空即默认档，填歪的不写进配置
     * @details 填歪了还写进去，只会在下一次保存时静默回落到默认档，用户反倒以为模板生效了；
     *          这里把原因写进 tooltip 并保留输入框内容，让他能就地改。
     */
    QLineEdit *makeNameTemplateEdit(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *edit = new GyanisScribe::Gui::LineEdit;
        edit->setText(settings.stringValue(GyanisScribe::Core::kSaveNameTemplate, QString{}));
        edit->setPlaceholderText(GyanisScribe::Base::Paths::defaultNameTemplate());
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Base::uiText("文件名模板"));

        const auto explainHint = [edit](const QString &reason)
        {
            edit->setToolTip(reason.isEmpty() ? GyanisScribe::Base::uiText("可用变量：%date%（日期）、%time%（时刻）、%ms%（毫秒）、%app%（应用名）、%title%（窗口标题）、%monitor%（屏幕名）。留空表示使用默认模板；修改后从下次保存起生效。")
                                              : reason);
        };
        explainHint(QString{});

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [edit, &settings, explainHint]
                         {
                             const auto text = edit->text();
                             if (text.trimmed().isEmpty())
                             {
                                 // 留空是「用默认档」这一档，不是错误：清掉那一格即可；
                                 // 写不进去还是要说一声，否则用户以为恢了默认、实际那格没动
                                 if (const auto stored = settings.setValue(GyanisScribe::Core::kSaveNameTemplate, QString{}); !stored)
                                 {
                                     GyanisScribe::Base::Logger::warning(std::format("文件名模板没能清回默认档：{}", stored.error().message));
                                 }
                                 explainHint(QString{});
                                 return;
                             }

                             const auto parsed = GyanisScribe::Base::Paths::parseNameTemplate(text);
                             if (!parsed)
                             {
                                 explainHint(QString::fromStdString(parsed.error().message));
                                 return;
                             }
                             if (const auto written = settings.setValue(GyanisScribe::Core::kSaveNameTemplate, *parsed); !written)
                             {
                                 GyanisScribe::Base::Logger::warning(std::format("文件名模板没能写进配置：{}", written.error().message));
                             }
                             explainHint(QString{});
                         });
        return edit;
    }

    /**
     * @brief 文件名模板那一格：输入框下面跟一行按固定示例预演的成品名
     * @param settings 配置存储
     * @return 归调用方所有的行控件
     * @details 预演只读输入框当前文本，不写配置；留空按默认档预演（与实际保存同一档），
     *          模板写歪时预览标签说「不可用」，具体原因仍挂在输入框的 tooltip 上。
     *          示例值固定写死，不取当前前台窗口或时钟，用例与截图里的预演才是同一个结果。
     */
    QWidget *makeNameTemplateRow(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *row    = new QWidget;
        auto *layout = new QVBoxLayout{row};
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(GyanisScribe::Gui::kSpacingTiny);

        auto *edit = makeNameTemplateEdit(settings);
        layout->addWidget(edit);

        auto *preview = new QLabel{row};
        preview->setObjectName(QStringLiteral("nameTemplatePreview"));
        preview->setAccessibleName(GyanisScribe::Base::uiText("文件名模板预览"));
        preview->setWordWrap(true);
        layout->addWidget(preview);

        const GyanisScribe::Base::NameTemplateContext context{
                GyanisScribe::Base::uiText("示例应用"),
                GyanisScribe::Base::uiText("示例窗口"),
                GyanisScribe::Base::uiText("屏幕 1"),
        };
        const QDateTime moment{QDate{2026, 11, 9}, QTime{14, 3, 0, 123}};
        const auto      refreshPreview = [preview, context, moment](const QString &text)
        {
            const auto productName = GyanisScribe::Base::Paths::previewProductName(text, moment, context);
            if (!productName)
            {
                preview->setText(GyanisScribe::Base::uiText("预览：模板不可用"));
                return;
            }
            const auto formatted = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("预览：{}.png"), productName->toStdString());
            preview->setText(QString::fromUtf8(formatted.c_str()));
        };
        QObject::connect(edit, &QLineEdit::textChanged, preview, refreshPreview);
        refreshPreview(edit->text());
        return row;
    }

    /**
     * @brief 成品边框的宽度，物理像素
     * @param settings 配置存储
     * @return 归调用方所有的数字框
     */
    QSpinBox *makeBorderWidthSpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(GyanisScribe::Core::kMinimumOutputBorderPixels, GyanisScribe::Core::kMaximumOutputBorderPixels);
        spin->setSuffix(GyanisScribe::Base::uiText(" 像素"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("边框宽度"));
        spin->setToolTip(GyanisScribe::Base::uiText("启用“输出边框”时边框的宽度（物理像素）；仅对输出文件生效，贴到屏幕的贴图不添加。"));

        const auto title  = GyanisScribe::Base::uiText("边框宽度");
        const auto stored = settings.intValue(GyanisScribe::Core::kOutputBorderWidth, GyanisScribe::Core::kOutputBorderPixels);
        const auto parsed = GyanisScribe::Core::parseOutputBorderPixels(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Core::kOutputBorderPixels));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kOutputBorderWidth, value); !written)
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
    QSpinBox *makeShadowWidthSpin(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *spin = new GyanisScribe::Gui::SpinBox;
        spin->setRange(GyanisScribe::Core::kMinimumOutputShadowPixels, GyanisScribe::Core::kMaximumOutputShadowPixels);
        spin->setSuffix(GyanisScribe::Base::uiText(" 像素"));
        spin->setAccessibleName(GyanisScribe::Base::uiText("投影宽度"));
        spin->setToolTip(GyanisScribe::Base::uiText("启用“输出投影”时投影向外扩展的宽度（物理像素）；宽度越大越柔和，占用空间也越大。"));

        const auto title  = GyanisScribe::Base::uiText("投影宽度");
        const auto stored = settings.intValue(GyanisScribe::Core::kOutputShadowWidth, GyanisScribe::Core::kOutputShadowPixels);
        const auto parsed = GyanisScribe::Core::parseOutputShadowPixels(stored);
        spin->setValue(shownValueForSetting(title, stored, parsed, GyanisScribe::Core::kOutputShadowPixels));

        QObject::connect(spin, &QSpinBox::valueChanged, spin,
                         [&settings, title](int value)
                         {
                             if (auto written = settings.setValue(GyanisScribe::Core::kOutputShadowWidth, value); !written)
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
    QLineEdit *makeBorderColorEdit(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *edit = new GyanisScribe::Gui::LineEdit;
        edit->setText(settings.stringValue(GyanisScribe::Core::kOutputBorderColor, QString{}));
        edit->setPlaceholderText(GyanisScribe::Base::uiText("#808080（中灰色，留空即为默认）"));
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Base::uiText("边框颜色"));

        const auto explainHint = [edit](const QString &reason)
        { edit->setToolTip(reason.isEmpty() ? GyanisScribe::Base::uiText("输出图片边框的颜色，格式为 #RRGGBB；留空使用默认中灰色。") : reason); };
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
                             if (const auto written = settings.setValue(GyanisScribe::Core::kOutputBorderColor, text.trimmed()); !written)
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
    QLineEdit *makeExecCommandEdit(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *edit = new GyanisScribe::Gui::LineEdit;
        edit->setText(settings.stringValue(GyanisScribe::Core::kCaptureExecCommand, QString{}));
        edit->setPlaceholderText(GyanisScribe::Base::uiText("留空表示不执行；例如：上传工具 --path \"%file%\""));
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Base::uiText("截图后执行的命令"));
        edit->setToolTip(GyanisScribe::Base::uiText("每次截图交付后执行此命令，参数中的 %file% 会替换为本轮输出文件路径。"
                                                    "本轮未生成文件时（例如“截图完成后”选择在标注窗口中打开），带 %file% 的命令会被拒绝并说明原因；"
                                                    "命令行提供 --exec 时以该命令为准。"));

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [&settings, edit]
                         {
                             if (const auto written = settings.setValue(GyanisScribe::Core::kCaptureExecCommand, edit->text().trimmed()); !written)
                             {
                                 GyanisScribe::Base::Logger::warning(std::format("截图后运行命令没能写入：{}", written.error().message));
                             }
                         });
        return edit;
    }

    QWidget *MainWindow::buildCapturePage()
    {
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *form = makePageForm(page, GyanisScribe::Base::uiText("截图与标注输出文件保存在指定目录。取色复制的文本按下方格式生成。"),
                             GyanisScribe::Base::uiText("截图行为"));
        form->addRow(GyanisScribe::Base::uiText("截图完成后"), makeCaptureIntentCombo(m_settings, m_icons));
        form->addRow(GyanisScribe::Base::uiText("显示截图信息面板"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kCaptureShowInfoPanel, false,
                                      GyanisScribe::Base::uiText("在标注窗口旁显示"),
                                      GyanisScribe::Base::uiText("显示截图信息面板"),
                                      GyanisScribe::Base::uiText("启用后，每次打开标注窗口都会在工具栏旁显示信息面板，包含应用、窗口标题、屏幕与捕获时间。"
                                                                 "默认关闭；面板仅在本机显示，不写入历史或日志。")));
        form->addRow(GyanisScribe::Base::uiText("截图延时"), makeCaptureDelaySpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("鼠标指针"), makeIncludeCursorCheck(m_settings));
        form->addRow(GyanisScribe::Base::uiText("自动保存"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kCaptureAutoSave, false, GyanisScribe::Base::uiText("每张截图均保存到输出目录"),
                                      GyanisScribe::Base::uiText("自动保存所有截图"),
                                      GyanisScribe::Base::uiText("截图完成后立即按上方格式与目录保存一份，不显示提示；若“截图完成后”已选择“保存到文件”或“保存并复制”，则不会重复保存。")));
        form->addRow(GyanisScribe::Base::uiText("截图音效"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kCaptureSound, false, GyanisScribe::Base::uiText("截图完成时播放系统提示音"),
                                      GyanisScribe::Base::uiText("截图音效"), GyanisScribe::Base::uiText("使用系统提示音，无需提供音频文件。")));
        form->addRow(GyanisScribe::Base::uiText("截图后执行的命令"), makeExecCommandEdit(m_settings));
        form = addSettingsSection(form, GyanisScribe::Base::uiText("圆角与输出边框"));
        form->addRow(GyanisScribe::Base::uiText("圆角处理"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kCaptureRoundCorners, GyanisScribe::Core::kDefaultCaptureRoundCorners,
                                      GyanisScribe::Base::uiText("将输出图片四角裁剪为圆角"), GyanisScribe::Base::uiText("圆角处理"),
                                      GyanisScribe::Base::uiText("仅对区域截图生效：输出图片四角为透明像素，半径由下方设置决定；整屏、窗口与滚动长截图不应用圆角。")));
        form->addRow(GyanisScribe::Base::uiText("圆角半径"), makeCornerRadiusSpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("输出边框"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kOutputBorder, false, GyanisScribe::Base::uiText("为输出图片添加中灰色边框"),
                                      GyanisScribe::Base::uiText("输出边框"),
                                      GyanisScribe::Base::uiText("保存文件、复制到剪贴板与打印时均添加边框；贴到屏幕的贴图不添加，因为那是显示而非输出。")));
        form->addRow(GyanisScribe::Base::uiText("边框宽度"), makeBorderWidthSpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("边框颜色"), makeBorderColorEdit(m_settings));
        form->addRow(GyanisScribe::Base::uiText("输出投影"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kOutputShadow, false, GyanisScribe::Base::uiText("为输出图片添加轻投影"),
                                      GyanisScribe::Base::uiText("输出投影"), GyanisScribe::Base::uiText("与边框同组生效：保存文件、复制到剪贴板与打印时均添加；保存为 JPG 时会在投影下方垫白色背景。")));
        form->addRow(GyanisScribe::Base::uiText("投影宽度"), makeShadowWidthSpin(m_settings));
        form = addSettingsSection(form, GyanisScribe::Base::uiText("保存与命名"));
        form->addRow(GyanisScribe::Base::uiText("截图与标注输出目录"), makePathRow(m_settings, GyanisScribe::Core::kPathScreenshot, GyanisScribe::Base::uiText("截图输出目录"),
                                                                                   GyanisScribe::Base::Paths::defaultScreenshotDirectory(), m_icons));
        form->addRow(GyanisScribe::Base::uiText("保存格式"), makeImageFormatCombo(m_settings));
        form->addRow(GyanisScribe::Base::uiText("保存质量"), makeImageQualitySpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("文件名模板"), makeNameTemplateRow(m_settings));
        form = addSettingsSection(form, GyanisScribe::Base::uiText("读数与辅助显示"));
        form->addRow(GyanisScribe::Base::uiText("颜色复制格式"), makeColorFormatCombo(m_settings));
        form->addRow(GyanisScribe::Base::uiText("放大镜视野"), makeMagnifierViewCombo(m_settings));
        form->addRow(GyanisScribe::Base::uiText("尺寸单位"), makeSizeUnitCombo(m_settings));
        form->addRow(GyanisScribe::Base::uiText("窗口检测排除程序"), makeDetectionExcludesEdit(m_settings));
        form->addRow(GyanisScribe::Base::uiText("十六进制字母大小写"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kPickHexUppercase, true, GyanisScribe::Base::uiText("使用大写字母"),
                                      GyanisScribe::Base::uiText("十六进制字母大小写"),
                                      GyanisScribe::Base::uiText("关闭后输出为 #3c78c8 形式；仅影响十六进制写法，其它格式不受影响。")));
        form = addSettingsSection(form, GyanisScribe::Base::uiText("回溯缓冲"));
        form->addRow(GyanisScribe::Base::uiText("回溯缓冲"), makeRewindEnabledCheck(m_settings));
        form->addRow(GyanisScribe::Base::uiText("缓冲保留时长"), makeRewindBufferSecondsSpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("缓冲帧率"), makeRewindFrameRateCombo(m_settings));
        form->addRow(GyanisScribe::Base::uiText("排除的程序"), makeRewindExclusionEdit(m_settings));

        auto *rewindNote = new QLabel{GyanisScribe::Base::uiText("仅保存在内存中，不写入磁盘、不上传；锁屏或退出程序时清空。"), page};
        rewindNote->setObjectName(QStringLiteral("rewindPrivacyNote"));
        rewindNote->setWordWrap(true);
        form->addRow(rewindNote);
        return page;
    }

    /**
     * @brief 自定义色板那一格：逗号分隔的 #RRGGBB 清单
     * @param settings 配置存储
     * @return 归调用方所有的输入框
     * @details 留空就是「不自定义了」：存一份空名单，读端回落到内置那八格——清空与填成坏值不是一回事，
     *          后者要当场点名并保住上一份好值（工具栏摆的是用户存的那排颜色，存歪着切过去会少格）。
     */
    QLineEdit *makePaletteEdit(GyanisScribe::Core::ISettingsStore &settings)
    {
        auto *edit = new GyanisScribe::Gui::LineEdit;
        edit->setText(settings.stringListValue(GyanisScribe::Core::kAnnotationPalette, {}).join(QStringLiteral(", ")));
        edit->setPlaceholderText(GyanisScribe::Base::uiText("留空表示使用内置八色；例如 #FF0000, #00FF00"));
        edit->setClearButtonEnabled(true);
        edit->setAccessibleName(GyanisScribe::Base::uiText("自定义色板"));

        const auto explainHint = [edit](const QString &reason)
        { edit->setToolTip(reason.isEmpty() ? GyanisScribe::Base::uiText("色板显示的色块；此处顺序即从左到右的排列顺序。") : reason); };
        explainHint(QString{});

        QObject::connect(edit, &QLineEdit::editingFinished, edit,
                         [&settings, edit, explainHint]
                         {
                             const auto text = edit->text().trimmed();
                             if (text.isEmpty())
                             {
                                 if (const auto written = settings.setValue(GyanisScribe::Core::kAnnotationPalette, QStringList{}); !written)
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
                             const auto parsed = GyanisScribe::Core::paletteFromTokens(tokens);
                             if (!parsed)
                             {
                                 explainHint(QString::fromUtf8(parsed.error().message.c_str()));
                                 return;
                             }
                             if (const auto written = settings.setValue(GyanisScribe::Core::kAnnotationPalette, GyanisScribe::Core::paletteToTokens(*parsed)); !written)
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

        auto *exportButton = new GyanisScribe::Gui::Button{GyanisScribe::Base::uiText("导出色板…"), row};
        m_icons.applyIcon(*exportButton, "Export");
        exportButton->setAccessibleName(GyanisScribe::Base::uiText("导出色板"));
        exportButton->setToolTip(GyanisScribe::Base::uiText("将当前色板写入一个文本文件（每行一个 #RRGGBB），用于分享或迁移。"));

        auto *importButton = new GyanisScribe::Gui::Button{GyanisScribe::Base::uiText("导入色板…"), row};
        m_icons.applyIcon(*importButton, "Import");
        importButton->setAccessibleName(GyanisScribe::Base::uiText("导入色板"));
        importButton->setToolTip(GyanisScribe::Base::uiText("从色板文件替换当前色板；下次打开标注窗口时使用新色板。"));

        // 对话框取消时 picked 是空串：那不是失败，一句话都不多说
        QObject::connect(exportButton, &QPushButton::clicked, row,
                         [this]
                         {
                             const auto suggested = GyanisScribe::Base::Paths::toDisplayText(GyanisScribe::Base::Paths::applicationDataDirectory() / "GyanisScribe-palette.txt");
                             const auto picked =
                                     QFileDialog::getSaveFileName(this, GyanisScribe::Base::uiText("导出色板"), suggested, GyanisScribe::Base::uiText("色板文件 (*.txt)"));
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
                                     QFileDialog::getOpenFileName(this, GyanisScribe::Base::uiText("导入色板"), QDir::homePath(), GyanisScribe::Base::uiText("色板文件 (*.txt)"));
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
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *form = makePageForm(page, GyanisScribe::Base::uiText("新建标注窗口使用以下默认线宽与字号；之后的修改会由工具栏记录，下次以最近使用的档位打开。"
                                                                   "自定义色板留空时使用内置八色；下方按钮可导出或导入整套配色。"),
                             GyanisScribe::Base::uiText("默认样式"));
        form->addRow(GyanisScribe::Base::uiText("默认线宽"),
                     makeStepCombo(m_settings, GyanisScribe::Core::kAnnotationDefaultWidth, GyanisScribe::Gui::kAnnotationWidths,
                                   GyanisScribe::Gui::kAnnotationWidths[GyanisScribe::Gui::kDefaultAnnotationWidthStep], GyanisScribe::Base::uiText("默认线宽")));
        form->addRow(GyanisScribe::Base::uiText("默认字号"),
                     makeStepCombo(m_settings, GyanisScribe::Core::kAnnotationDefaultFontSize, GyanisScribe::Gui::kFontSizes,
                                   GyanisScribe::Gui::kFontSizes[GyanisScribe::Gui::kDefaultAnnotationFontSizeIndex], GyanisScribe::Base::uiText("默认字号")));
        form = addSettingsSection(form, GyanisScribe::Base::uiText("色板"));
        form->addRow(GyanisScribe::Base::uiText("自定义色板"), makePaletteEdit(m_settings));
        form->addRow(GyanisScribe::Base::uiText("色板导入与导出"), buildPaletteTransferRow());
        form->addRow(GyanisScribe::Base::uiText("空白画布背景"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kAnnotationBlankCanvasTransparent, false, GyanisScribe::Base::uiText("使用透明背景"),
                                      GyanisScribe::Base::uiText("空白画布背景"),
                                      GyanisScribe::Base::uiText("透明区域在画布上以棋盘格显示；保存为 PNG/WebP 时保留透明，保存为 JPG 时会先垫白色背景。")));
        return page;
    }

    QWidget *MainWindow::buildPinPage()
    {
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *form = makePageForm(page, GyanisScribe::Base::uiText("新建贴图使用此默认不透明度。滚轮调整仅影响当前贴图，不修改此设置。"),
                             GyanisScribe::Base::uiText("默认外观"));
        form->addRow(GyanisScribe::Base::uiText("新贴图默认不透明度"), makeOpacitySpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("吸附距离"), makeEdgeSnapDistanceSpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("禁止拖边调整大小"),
                     makeSettingCheck(m_settings, GyanisScribe::Core::kPinDisableFrameResize, false, GyanisScribe::Base::uiText("禁止拖动边缘或手柄"),
                                      GyanisScribe::Base::uiText("禁止拖边调整大小"), GyanisScribe::Base::uiText("仅关闭调整尺寸的交互点；整张移动、滚轮缩放与右键菜单不受影响。")));
        return page;
    }

    QWidget *MainWindow::buildRecordPage()
    {
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *form =
                makePageForm(page, GyanisScribe::Base::uiText("录屏输出文件保存在指定目录。录制范围选择“上次选区”或“当前窗口”时，仅录制对应区域。"
                                                              "保存为 GIF 时帧率上限低于 MP4（GIF 最高 24 帧/秒）。画面码率设为 0 时由编码器按质量优先处理；"
                                                              "填写具体数值时按该码率控制，GIF 不使用此项。编码器可固定为某个可用项；自动模式会在编码前逐项检测并选择第一个可用项。"
                                                              "录制倒计时为 0 时立即开始。音频设备无法列出时下拉框仅保留“不录制”，悬停可查看原因；"
                                                              "保存为 GIF 时音频选项会禁用，因为该格式不支持声音。以上设置均从下次录屏开始生效。"),
                             GyanisScribe::Base::uiText("输出与范围"));
        form->addRow(GyanisScribe::Base::uiText("录屏输出目录"), makePathRow(m_settings, GyanisScribe::Core::kPathRecording, GyanisScribe::Base::uiText("录屏输出目录"),
                                                                             GyanisScribe::Base::Paths::defaultRecordingDirectory(), m_icons));
        form->addRow(GyanisScribe::Base::uiText("录制范围"), makeRecordScopeCombo(m_settings, m_icons));
        auto *formatCombo = makeRecordFormatCombo(m_settings);
        form->addRow(GyanisScribe::Base::uiText("输出格式"), formatCombo);
        auto *frameRateSpin = makeFrameRateSpin(m_settings);
        form->addRow(GyanisScribe::Base::uiText("默认帧率"), frameRateSpin);
        form->addRow(iconRow(m_icons, m_theme, "Countdown", GyanisScribe::Base::uiText("录制倒计时")), makeCountdownSpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("最长录制时长"), makeMaximumDurationSpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("画面码率"), makeVideoBitrateSpin(m_settings));
        form->addRow(GyanisScribe::Base::uiText("视频编码器"), makeVideoEncoderCombo(m_settings, *m_videoEncoders));
        form = addSettingsSection(form, GyanisScribe::Base::uiText("光标高亮"));
        form->addRow(GyanisScribe::Base::uiText("光标高亮"), makeCursorHighlightCheck(m_settings, m_icons));
        auto *microphoneCombo  = makeAudioDeviceCombo(m_settings, GyanisScribe::Core::kRecordAudioMicrophone, GyanisScribe::Base::uiText("麦克风设备"), *m_audioDevices);
        auto *systemSoundCombo = makeAudioDeviceCombo(m_settings, GyanisScribe::Core::kRecordAudioSystemSound, GyanisScribe::Base::uiText("系统声音设备"), *m_audioDevices);
        form = addSettingsSection(form, GyanisScribe::Base::uiText("音频"));
        form->addRow(iconRow(m_icons, m_theme, "Microphone", GyanisScribe::Base::uiText("麦克风")), microphoneCombo);
        form->addRow(iconRow(m_icons, m_theme, "SystemAudio", GyanisScribe::Base::uiText("系统声音")), systemSoundCombo);

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
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *form = makePageForm(page, GyanisScribe::Base::uiText("可识别的文字取决于语言包。目录留空时按环境变量与程序数据目录查找。"),
                             GyanisScribe::Base::uiText("识别设置"));
        form->addRow(iconRow(m_icons, m_theme, "OcrLanguage", GyanisScribe::Base::uiText("识别语言")), makeOcrLanguageCombo(m_settings));
        form->addRow(GyanisScribe::Base::uiText("语言包目录"), makePathRow(m_settings, GyanisScribe::Core::kOcrTessDataDirectory, GyanisScribe::Base::uiText("语言包目录"),
                                                                               GyanisScribe::Base::Paths::applicationDataDirectory() / "tessdata", m_icons));
        return page;
    }

    QWidget *MainWindow::buildHotkeyPage()
    {
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        // 没有登记接口的那一种平台上，改组合键不会有任何效果：入口灰掉并当场说明，
        // 比让人反复改完再按一次、把「没反应」当成程序坏了要好
        // 一行一个动作；条数由这份表自己数出来：文案里那个数字与热键登记表必须是同一个源
        const auto rows = std::array{std::tuple{GyanisScribe::Core::kHotkeyCapture, GyanisScribe::Base::uiText("截图"), QStringLiteral("Ctrl+Alt+A")},
                                     std::tuple{GyanisScribe::Core::kHotkeyRecord, GyanisScribe::Base::uiText("录屏"), QStringLiteral("Ctrl+Alt+R")},
                                     std::tuple{GyanisScribe::Core::kHotkeyOcr, GyanisScribe::Base::uiText("文字识别"), QStringLiteral("Ctrl+Alt+O")},
                                     std::tuple{GyanisScribe::Core::kHotkeyPick, GyanisScribe::Base::uiText("屏幕取色"), QStringLiteral("Ctrl+Alt+P")},
                                     std::tuple{GyanisScribe::Core::kHotkeyCaptureLong, GyanisScribe::Base::uiText("滚动长截图"), QStringLiteral("Ctrl+Alt+L")},
                                     std::tuple{GyanisScribe::Core::kHotkeySceneClear, GyanisScribe::Base::uiText("清场截图"), QStringLiteral("Ctrl+Alt+X")},
                                     std::tuple{GyanisScribe::Core::kHotkeyQuickSave, GyanisScribe::Base::uiText("快速保存"), QStringLiteral("Ctrl+Alt+S")},
                                     std::tuple{GyanisScribe::Core::kHotkeyTogglePins, GyanisScribe::Base::uiText("收起 / 显示全部贴图"), QStringLiteral("Ctrl+Alt+H")},
                                     std::tuple{GyanisScribe::Core::kHotkeyNextPinGroup, GyanisScribe::Base::uiText("切换到下一组贴图"), QStringLiteral("Ctrl+Alt+G")},
                                     std::tuple{GyanisScribe::Core::kHotkeyRewind, GyanisScribe::Base::uiText("回溯缓冲"), QStringLiteral("Ctrl+Alt+Z")},
                                     std::tuple{GyanisScribe::Core::kHotkeySteps, GyanisScribe::Base::uiText("步骤记录"), QStringLiteral("Ctrl+Alt+T")},
                                     std::tuple{GyanisScribe::Core::kHotkeyPickElement, GyanisScribe::Base::uiText("拾取界面元素"), QStringLiteral("Ctrl+Alt+E")}};

        const auto intro =
                m_capabilities.globalHotkeys
                        ? GyanisScribe::Base::uiText("上方 %1 项为固定动作，各有一个默认快捷键；随后 %2 项功能可另行绑定，留空表示不绑定。修改后立即重新注册，无需重启。"
                                                     "底部一栏为应用内快捷键，仅在智录窗口内生效。")
                                  .arg(static_cast<int>(rows.size()))
                                  .arg(static_cast<int>(GyanisScribe::App::bindableActions().size()))
                        : GyanisScribe::Base::uiText("当前平台不提供全局快捷键注册接口，上方 %1 项修改后不会生效。"
                                                     "底部一栏为应用内快捷键，不受此能力限制，照常生效。")
                                  .arg(static_cast<int>(rows.size() + GyanisScribe::App::bindableActions().size()));
        auto *form = makePageForm(page, intro, GyanisScribe::Base::uiText("快捷键一览"));
        for (const auto &[key, title, defaultText]: rows)
        {
            auto *edit = new GyanisScribe::Gui::HotkeyEdit{m_settings, page};
            edit->bindTo(key, title, defaultText, GyanisScribe::Gui::HotkeyEditScope::Global);
            edit->setEnabled(m_capabilities.globalHotkeys);
            form->addRow(GyanisScribe::Base::uiText("%1 的全局热键").arg(title), edit);
        }

        // 固定这几行之外的功能：一格一签，可绑可不绑，「取消绑定」把那一项从配置里删掉
        for (const auto &entry: GyanisScribe::App::bindableActions())
        {
            const auto label  = GyanisScribe::App::bindableActionLabel(entry.action);
            auto      *row    = new QWidget{page};
            auto      *layout = new QHBoxLayout{row};
            layout->setContentsMargins(0, 0, 0, 0);

            auto *edit = new GyanisScribe::Gui::HotkeyEdit{m_settings, row};
            edit->bindTo(GyanisScribe::App::customHotkeyKey(entry.token).toStdString(), label, QString{}, GyanisScribe::Gui::HotkeyEditScope::Global);
            edit->setEnabled(m_capabilities.globalHotkeys);

            auto *clear = new GyanisScribe::Gui::Button{GyanisScribe::Base::uiText("取消绑定"), row};
            // 名字带上功能：十行都叫「取消绑定」时，读屏与判据都分不清按下的是哪一行
            clear->setAccessibleName(GyanisScribe::Base::uiText("取消 %1 的绑定").arg(label));
            clear->setToolTip(GyanisScribe::Base::uiText("清除此绑定后，该快捷键不再用于此功能。"));
            clear->setEnabled(m_capabilities.globalHotkeys);
            QObject::connect(clear, &QPushButton::clicked, clear, [edit] { edit->clearBinding(); });

            layout->addWidget(edit, 1);
            layout->addWidget(clear);
            form->addRow(GyanisScribe::Base::uiText("「%1」的全局快捷键").arg(label), row);
        }

        // 单键那一档单独一行，摆在全局热键段的末尾：它放开的是上面那些行的校验，
        // 与界面按键无关（界面按键本来就允许裸键）。
        // 勾选框只写短名（长句当标签时 QCheckBox 不会折行，右缘直接把句尾裁掉），
        // 长句改成下面一行会折行的说明；tooltip 原样保留
        auto *simpleRow    = new QWidget;
        auto *simpleLayout = new QVBoxLayout{simpleRow};
        simpleLayout->setContentsMargins(0, 0, 0, 0);
        simpleLayout->setSpacing(GyanisScribe::Gui::kSpacingTiny);
        const auto simpleLabel = GyanisScribe::Base::uiText("允许单键作为全局热键");
        auto      *simple      = makeSettingCheck(m_settings, GyanisScribe::Core::kAllowSimpleGlobalHotkeys, false, simpleLabel, simpleLabel,
                                                  GyanisScribe::Base::uiText("默认关闭。启用后，例如将 `R` 绑定为截图：在其他程序中按下 R 也会触发截图，"
                                                                             "而该程序无法接收此按键。仅在确需将某个按键专用于智录时启用。"));
        simple->setEnabled(m_capabilities.globalHotkeys);
        simpleLayout->addWidget(simple);
        auto *simpleHint = new QLabel{GyanisScribe::Base::uiText("允许将字母、数字、标点、方向键以及 Esc、Enter 等单键用作全局快捷键"), simpleRow};
        simpleHint->setObjectName(QStringLiteral("pageHint"));
        simpleHint->setWordWrap(true);
        simpleHint->setMaximumWidth(GyanisScribe::Gui::kIntroMaximumWidth);
        // 表单行高按 sizeHint 给：不带这一档时窄窗下只分到一行，折行后的第二行被裁掉
        simpleHint->setMinimumHeight(simpleHint->fontMetrics().lineSpacing() * 2);
        simpleLayout->addWidget(simpleHint);
        form->addRow(GyanisScribe::Base::uiText("单键全局快捷键"), simpleRow);
        // 界面里的按键：这一批不吃平台能力位——没有全局登记口也照样在自己的窗口里认，
        // 所以它们既不灰也不进上面那句「改了也不会生效」的计数里
        auto *inAppTitle = new QLabel{GyanisScribe::Base::uiText("应用内快捷键"), page};
        inAppTitle->setObjectName(QStringLiteral("pageHint"));
        inAppTitle->setAccessibleName(GyanisScribe::Base::uiText("应用内快捷键分组的标题"));
        form->addRow(inAppTitle);
        std::string_view lastFamily;
        for (const auto &entry: GyanisScribe::Gui::appShortcutEntries())
        {
            // 按族分小标题：二十五行平铺在一起，读的人分不清哪颗键归哪个窗——同名不同窗是常态
            const auto family = GyanisScribe::Gui::appShortcutFamilyName(entry.token);
            if (family != lastFamily)
            {
                lastFamily    = family;
                auto *heading = new QLabel{GyanisScribe::Base::uiText(family), page};
                heading->setAccessibleName(GyanisScribe::Base::uiText("应用内快捷键的分组标题"));
                form->addRow(heading);
            }

            const auto label  = GyanisScribe::Base::uiText(entry.label);
            auto      *row    = new QWidget{page};
            auto      *layout = new QHBoxLayout{row};
            layout->setContentsMargins(0, 0, 0, 0);

            const auto defaultKey = QString::fromUtf8(entry.defaultKey.data(), static_cast<int>(entry.defaultKey.size()));

            auto *edit = new GyanisScribe::Gui::HotkeyEdit{m_settings, row};
            edit->bindTo(GyanisScribe::Gui::appShortcutKey(entry.token).toStdString(), label, defaultKey, GyanisScribe::Gui::HotkeyEditScope::InWindow);

            auto *reset = new GyanisScribe::Gui::Button{GyanisScribe::Base::uiText("恢复默认"), row};
            // 名字带上那一档：二十五行都叫「恢复默认」时，读屏与判据都分不清按下的是哪一行
            reset->setAccessibleName(GyanisScribe::Base::uiText("将「%1」的快捷键恢复为默认值").arg(label));
            reset->setToolTip(GyanisScribe::Base::uiText("清除此设置后，该快捷键恢复为默认的 %1。").arg(defaultKey));
            QObject::connect(reset, &QPushButton::clicked, reset, [edit] { edit->clearBinding(); });

            layout->addWidget(edit, 1);
            layout->addWidget(reset);
            form->addRow(GyanisScribe::Base::uiText("「%1」的应用内快捷键").arg(label), row);
        }
        return page;
    }

    QWidget *MainWindow::buildThemePage()
    {
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *form = makePageForm(page, GyanisScribe::Base::uiText("配色方案立即生效。高对比度模式使用纯黑文字、纯白背景与加粗边框。"),
                             GyanisScribe::Base::uiText("界面主题"));
        form->addRow(GyanisScribe::Base::uiText("配色方案"), makeThemeCombo(m_theme, m_icons));
        return page;
    }

    QWidget *MainWindow::buildHistoryPage()
    {
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *inner = new QVBoxLayout{page};
        m_history   = new GyanisScribe::Gui::HistoryView{m_historyStore, GyanisScribe::Core::policyFromSettings(m_settings), m_icons, page};
        inner->addWidget(m_history);

        // 三个出口原样转给控制器：主窗口不知道贴图与识别各自要做什么
        QObject::connect(m_history, &GyanisScribe::Gui::HistoryView::pinRequested, this, &MainWindow::historyPinRequested);
        QObject::connect(m_history, &GyanisScribe::Gui::HistoryView::ocrRequested, this, &MainWindow::historyOcrRequested);
        QObject::connect(m_history, &GyanisScribe::Gui::HistoryView::removeRequested, this, &MainWindow::historyRemoveRequested);
        QObject::connect(m_history, &GyanisScribe::Gui::HistoryView::openFolderRequested, this, &MainWindow::historyOpenFolderRequested);
        QObject::connect(m_history, &GyanisScribe::Gui::HistoryView::clearAllRequested, this, &MainWindow::historyClearRequested);
        QObject::connect(m_history, &GyanisScribe::Gui::HistoryView::messageForUser, this, &MainWindow::messageForUser);

        // 通用页改完条数或天数后，这一页此前只认构造时那份策略快照：改这两个键就当场换策略并按新策略重读。
        // 隐藏着时这一步只读清单摆行，缩略图等翻到这一页再解
        const auto maximumEntriesKey = QString::fromUtf8(GyanisScribe::Core::kHistoryMaximumEntries.data(), static_cast<int>(GyanisScribe::Core::kHistoryMaximumEntries.size()));
        const auto retentionDaysKey  = QString::fromUtf8(GyanisScribe::Core::kHistoryRetentionDays.data(), static_cast<int>(GyanisScribe::Core::kHistoryRetentionDays.size()));
        QObject::connect(&m_settings, &GyanisScribe::Core::ISettingsStore::valueChanged, this,
                         [this, maximumEntriesKey, retentionDaysKey](const QString &key)
                         {
                             if (m_history == nullptr || (key != maximumEntriesKey && key != retentionDaysKey))
                             {
                                 return;
                             }
                             m_history->setPolicy(GyanisScribe::Core::policyFromSettings(m_settings));
                             m_history->refresh();
                         });
        return page;
    }

    QWidget *MainWindow::buildMemoryPage()
    {
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *inner = new QVBoxLayout{page};
        m_memory    = new GyanisScribe::Gui::MemoryView{m_memoryIndex, m_icons, page};
        inner->addWidget(m_memory);

        // 这一页的出口原样转给控制器：贴图、识别、导入与索引维护各自怎么做，主窗口不知道也不该知道
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::pinRequested, this, &MainWindow::memoryPinRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::openFileRequested, this, &MainWindow::memoryOpenFileRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::openFolderRequested, this, &MainWindow::memoryOpenFolderRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::retryOcrRequested, this, &MainWindow::memoryRetryOcrRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::removeIndexRequested, this, &MainWindow::memoryRemoveRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::importRequested, this, &MainWindow::memoryImportRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::clearIndexRequested, this, &MainWindow::memoryClearRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::excludeProcessesChanged, this, &MainWindow::memoryExcludeRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::enableRequested, this, &MainWindow::memoryEnableRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::rebuildRequested, this, &MainWindow::memoryRebuildRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::retryPendingRequested, this, &MainWindow::memoryRetryPendingRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::importCancelRequested, this, &MainWindow::memoryImportCancelRequested);
        QObject::connect(m_memory, &GyanisScribe::Gui::MemoryView::messageForUser, this, &MainWindow::messageForUser);
        return page;
    }

    void MainWindow::refreshMemory()
    {
        if (m_memory != nullptr)
        {
            m_memory->refresh();
        }
    }

    void MainWindow::setMemoryEnabled(const bool enabled)
    {
        m_memoryEnabled = enabled;
        if (m_memoryToggle != nullptr)
        {
            // 回写勾选不是用户动作：不压住信号会让控制器收到自己造成的回声
            const QSignalBlocker blocker{m_memoryToggle};
            m_memoryToggle->setChecked(enabled);
        }
        if (m_memory != nullptr)
        {
            m_memory->setMemoryEnabled(enabled);
        }
    }

    void MainWindow::setMemoryOcrAvailability(const bool available, const QString &reason)
    {
        if (m_memory != nullptr)
        {
            m_memory->setOcrAvailability(available, reason);
        }
    }

    void MainWindow::setMemoryDatabaseError(const QString &reason)
    {
        if (m_memory != nullptr)
        {
            m_memory->setDatabaseError(reason);
        }
    }

    void MainWindow::setMemoryImportProgress(const Core::MemoryImportProgress &progress)
    {
        if (m_memory != nullptr)
        {
            m_memory->setImportProgress(progress);
        }
    }

    void MainWindow::hideMemoryImportProgress()
    {
        if (m_memory != nullptr)
        {
            m_memory->hideImportProgress();
        }
    }

    void MainWindow::setMemoryExcludedProcesses(QStringList processes)
    {
        if (m_memory != nullptr)
        {
            m_memory->setExcludedProcesses(std::move(processes));
        }
    }

    void MainWindow::refreshHistory()
    {
        if (m_history != nullptr)
        {
            // 策略可能刚在通用页改过：先换再读，否则列表还按旧的条数与天数裁剪
            m_history->setPolicy(GyanisScribe::Core::policyFromSettings(m_settings));
            m_history->refresh();
        }
    }

    QWidget *MainWindow::buildAboutPage()
    {
        auto *page = new GyanisScribe::Gui::SurfaceWidget{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, this};
        page->setHighContrastBorder(true);
        auto *layout = new QVBoxLayout{page};
        // 与表单页同一条内容网格：同样的居中内容列，翻到这一页正文不左跳
        layout->setContentsMargins(GyanisScribe::Gui::kSpacingMedium, GyanisScribe::Gui::kSpacingMedium, GyanisScribe::Gui::kSpacingMedium, GyanisScribe::Gui::kSpacingMedium);
        layout->setSpacing(0);

        auto *content = new QWidget{page};
        content->setObjectName(QStringLiteral("settingsPageContent"));
        auto *contentLayout = new QVBoxLayout{content};
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(GyanisScribe::Gui::kSpacingSmall);

        // 底板与品牌砖拉开一档明度：深色主题的卡片 #17191E 与砖底 #151515 几乎同值，没有这层垫底砖形会整片消失。
        // 位图按所有屏幕里最高的缩放比取，窗口挪到 200% 屏上也不用换图。
        qreal highestRatio = 1.0;
        for (const auto *screen: QGuiApplication::screens())
        {
            if (screen->devicePixelRatio() > highestRatio)
            {
                highestRatio = screen->devicePixelRatio();
            }
        }
        auto *logo = new GyanisScribe::Gui::SurfaceLabel{GyanisScribe::Gui::SurfaceFillRole::ControlFill, GyanisScribe::Core::CornerRole::Card, page};
        logo->setObjectName(QStringLiteral("brandPlate"));
        logo->setFixedSize(80, 80);
        logo->setContentsMargins(8, 8, 8, 8);
        logo->setAlignment(Qt::AlignCenter);
        logo->setPixmap(m_icons.brandPixmap(64, highestRatio));
        logo->setAccessibleName(GyanisScribe::Base::uiText("智录图标"));

        auto *text = new QLabel{aboutText(QApplication::applicationVersion()), page};
        text->setAccessibleName(GyanisScribe::Base::uiText("关于智录"));
        text->setWordWrap(true);
        // 富文本里的仓库链接要可点也可选中复制：链接不直接开，交给 linkActivated 走系统浏览器。
        // 焦点链不收这一格：键盘档链接导航会把 Tab 吞进文档里（焦点停在标签上不走），
        // 而地址本身以明文摆着、选中复制与鼠标点击都不受影响
        text->setTextInteractionFlags(Qt::TextBrowserInteraction);
        text->setFocusPolicy(Qt::NoFocus);
        QObject::connect(text, &QLabel::linkActivated, text, [](const QString &link) { QDesktopServices::openUrl(QUrl{link}); });

        // 检查更新：只查不换——下载与替换可执行体要先有签名与原子替换那一套，
        // 这里把「有没有新版」与发布页地址摆出来，下载交给用户自己在浏览器里点
        auto *checkButton = new GyanisScribe::Gui::Button{GyanisScribe::Base::uiText("检查更新"), page};
        checkButton->setAccessibleName(GyanisScribe::Base::uiText("检查更新"));
        auto *status = new QLabel{page};
        status->setObjectName(QStringLiteral("pageHint"));
        status->setAccessibleName(GyanisScribe::Base::uiText("检查更新"));
        status->setWordWrap(true);
        status->setTextInteractionFlags(Qt::TextBrowserInteraction);
        status->setOpenExternalLinks(false);
        QObject::connect(status, &QLabel::linkActivated, status, [](const QString &link) { QDesktopServices::openUrl(QUrl{link}); });

        m_updateChecker = std::make_unique<GyanisScribe::App::UpdateChecker>(this);
        QObject::connect(m_updateChecker.get(), &GyanisScribe::App::UpdateChecker::finished, status,
                         [checkButton, status](const GyanisScribe::App::UpdateVerdict &verdict)
                         {
                             checkButton->setEnabled(true);
                             status->setText(updateStatusHtml(verdict));
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
                             status->setText(GyanisScribe::Base::uiText("正在检查…"));
                             m_updateChecker->check();
                         });

        contentLayout->addWidget(logo);
        contentLayout->addWidget(text);
        // 左对齐到正文左缘：整宽按钮会把标签居中，看着像一条悬空的字
        contentLayout->addWidget(checkButton, 0, Qt::AlignLeft);
        contentLayout->addWidget(status);
        layout->addWidget(content, 0, Qt::AlignHCenter);
        layout->addStretch();
        return page;
    }

} // namespace GyanisScribe::Main
