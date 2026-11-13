#include "Gui/Controls/HotkeyEdit.h"

#include "Base/Logger.h"
#include "Core/Settings/SettingsKeys.h"
#include "Base/Text.h"
#include "Gui/Controls/SurfaceWidget.h"
#include "Gui/Theme/DesignTokens.h"

#include <QEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QVBoxLayout>

#include <string>

namespace GyanisScribe::Gui
{
    std::optional<Qt::Key> editableKeyFromEvent(const int keyCode)
    {
        // 修饰键本身不是「按键本体」：只按到 Ctrl 时继续等下一个键，不能把半截组合写进配置
        switch (keyCode)
        {
            case Qt::Key_Control:
            case Qt::Key_Shift:
            case Qt::Key_Alt:
            case Qt::Key_AltGr:
            case Qt::Key_Meta:
            case Qt::Key_CapsLock:
            case Qt::Key_NumLock:
            case Qt::Key_ScrollLock:
                return {};
            default:
                break;
        }

        // 小键盘数字归到主键盘数字：同一颗键在两台机器上不该存成两种写法。
        // 摘位而不是相减：KeypadModifier 是一个位，减法在别的组合位上会算错
        const auto unshifted = static_cast<int>(static_cast<unsigned>(keyCode) & ~static_cast<unsigned>(Qt::KeypadModifier));
        if (unshifted >= Qt::Key_0 && unshifted <= Qt::Key_9)
        {
            return static_cast<Qt::Key>(unshifted);
        }
        return static_cast<Qt::Key>(keyCode);
    }

    HotkeyEdit::HotkeyEdit(Core::ISettingsStore &settings, QWidget *parent) : QWidget(parent), m_settings(settings)
    {
        auto *layout = new QVBoxLayout{this};
        layout->setContentsMargins(0, 0, 0, 0);

        m_display = new GyanisScribe::Gui::SurfaceLabel{GyanisScribe::Gui::SurfaceFillRole::ControlFill, GyanisScribe::Core::CornerRole::Control, this};
        m_display->setObjectName(QStringLiteral("hotkeyDisplay"));
        m_display->setFrameShape(QFrame::NoFrame);
        m_display->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_display->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_display->setMinimumWidth(0);
        m_display->setHighContrastBorder(true);
        m_display->setText(GyanisScribe::Base::uiText("点击后按下快捷键"));
        layout->addWidget(m_display);
        refreshDisplayFill();

        setFocusPolicy(Qt::ClickFocus);
    }

    void HotkeyEdit::bindTo(const std::string_view key, const QString &title, const QString &defaultText, const HotkeyEditScope scope)
    {
        m_key         = std::string{key};
        m_scope       = scope;
        m_defaultText = defaultText;
        // 无障碍名称与 tooltip 都写中文名：只有「Ctrl+Alt+A」这块文本时，读屏说不清它管哪个动作
        setAccessibleName(title);
        m_display->setAccessibleName(title);
        // 两档的作用域各有各的规矩，提示要写清这一格能不能只按一颗键：
        // 界面按键里那颗裸键是合法值，用户在提示的误导下会以为「按了没反应」是控件坏了
        setToolTip(scope == HotkeyEditScope::Global ? GyanisScribe::Base::uiText("%1 的全局快捷键。点击此输入框后按下组合键即可替换，按 Esc 取消。").arg(title)
                                                    : GyanisScribe::Base::uiText("%1 的应用内快捷键。点击此输入框后按下所需按键；允许使用单键；按 Esc 取消。").arg(title));

        // 外部改了同一项（恢复的旧组合写回、导入配置）时跟着变：设置页开着的时候本来就该实时刷新
        QObject::connect(&m_settings, &Core::ISettingsStore::valueChanged, this,
                         [this](const QString &changedKey)
                         {
                             if (changedKey != QString::fromStdString(m_key))
                             {
                                 return;
                             }
                             m_displayText = m_settings.stringValue(m_key, m_defaultText);
                             if (!m_recording)
                             {
                                 m_display->setText(m_displayText);
                             }
                         });

        const auto stored = m_settings.stringValue(key, defaultText);
        m_displayText     = stored;
        m_display->setText(stored);
    }

    std::string_view HotkeyEdit::boundKey() const noexcept
    {
        return m_key;
    }


    void HotkeyEdit::clearBinding()
    {
        // 空 QVariant 是「把这一项删掉」：存一个空串的话，每次读它都算「填的是空文本」并记一条警告
        if (auto written = m_settings.setValue(m_key, QVariant{}); !written)
        {
            Base::Logger::warning(std::format("取消热键绑定没能写入：{}", written.error().message));
            showPrompt(GyanisScribe::Base::uiText("写入失败"));
            return;
        }
        // 删键之后按同一口径回读：界面按键那一档要落回默认那颗键，不能留一块空白
        m_displayText = m_settings.stringValue(m_key, m_defaultText);
        m_display->setText(m_displayText);
    }


    Base::Expected<void> HotkeyEdit::applyCombination(const Core::HotkeyCombination &combination)
    {
        // 两档的松紧不同：全局那一档不许裸键（会把别的程序的按键吞掉），界面那一档只看按键本体
        // 「允许单键当全局热键」那一格现读：把它写进本控件就得处理「设置改了控件还停在旧口径」
        const auto allowBareKeys = m_settings.boolValue(GyanisScribe::Core::kAllowSimpleGlobalHotkeys, false);
        const auto usable        = m_scope == HotkeyEditScope::InWindow ? combination.checkUsableInWindow() : combination.checkUsableGlobally(allowBareKeys);
        if (!usable)
        {
            // 不合格就什么都不写：半截组合落进配置，重启后热键直接登记不上，比现在改不动更难查。
            // 显示也回到绑定值：把被拒的组合留在框里，看起来像是已经改成了那一颗
            cancelRecording();
            return std::unexpected(usable.error());
        }

        const auto text = combination.toText();
        if (auto written = m_settings.setValue(m_key, text); !written)
        {
            // 写不进去同样回到绑定值：框里留着「写入失败」或那颗新组合，都会让人以为这一项已经改了
            cancelRecording();
            return written;
        }
        // setValue 同步发 valueChanged：管理器登记失败会把旧组合写回配置，监听器已经先把显示改成它。
        // 这里必须回读配置，不能直接拿刚提交的新文本覆盖：那会把被拒的组合重新写回界面
        const auto stored = m_settings.stringValue(m_key, m_defaultText);
        m_displayText     = stored;
        m_display->setText(stored);
        return {};
    }

    void HotkeyEdit::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(event);
            return;
        }
        // 进入录制态并抢焦点：焦点不在本控件上时 keyPressEvent 根本不会送过来
        setRecordingState(true);
        setFocus(Qt::MouseFocusReason);
        showPrompt(GyanisScribe::Base::uiText("请按下快捷键（Esc 取消）"));
    }

    void HotkeyEdit::keyPressEvent(QKeyEvent *event)
    {
        if (!m_recording)
        {
            QWidget::keyPressEvent(event);
            return;
        }

        if (event->key() == Qt::Key_Escape)
        {
            // 取消回到现值：用户按 Esc 的意思是「我不改这一项」，把它写成空热键是曲解
            cancelRecording();
            clearFocus();
            return;
        }

        const auto key = editableKeyFromEvent(event->key());
        if (!key)
        {
            // 只按到了修饰键：保持录制状态等真正的按键，别把 Ctrl 这种半截组合写进去
            return;
        }

        setRecordingState(false);
        if (auto applied = applyCombination(Core::HotkeyCombination{event->modifiers(), *key}); !applied)
        {
            Base::Logger::warning(std::format("快捷键没能写入：{}", applied.error().message));
        }
        clearFocus();
    }

    void HotkeyEdit::focusOutEvent(QFocusEvent *event)
    {
        QWidget::focusOutEvent(event);
        if (!m_recording)
        {
            return;
        }
        // 点走别处等于放弃这次录制：录制态若留着，下一次 Tab 回来按的键会被当成这次录制的结果写进配置
        cancelRecording();
    }

    void HotkeyEdit::changeEvent(QEvent *event)
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::EnabledChange && m_display != nullptr)
        {
            refreshDisplayFill();
        }
    }

    void HotkeyEdit::refreshDisplayFill()
    {
        m_display->setFillRole(m_recording ? SurfaceFillRole::AccentSoft
                                           : (isEnabled() ? SurfaceFillRole::ControlFill : SurfaceFillRole::MutedFill));
        // 录制态的主色边是显式描边；静息态也给一条 borderStrong，与单行输入同一档可辨识度
        m_display->setBorderColor(m_recording ? activePalette().accent : (isEnabled() ? activePalette().borderStrong : activePalette().border));
    }

    void HotkeyEdit::setRecordingState(const bool recording)
    {
        m_recording = recording;
        // 样式表不再按录制态换形，属性只留作可观察的状态；可见的底与边由 SurfaceLabel 自绘
        m_display->setProperty("recording", recording);
        refreshDisplayFill();
    }

    void HotkeyEdit::cancelRecording()
    {
        setRecordingState(false);
        // 现值单独留在 m_displayText 里：录制中的提示、写入失败的提示都不该把它冲掉
        m_display->setText(m_displayText);
    }

    void HotkeyEdit::showPrompt(const QString &text)
    {
        // 只换提示文本，不动 m_displayText：取消时要靠它把配置里的现值还原到界面上
        m_display->setText(text);
    }

} // namespace GyanisScribe::Gui
