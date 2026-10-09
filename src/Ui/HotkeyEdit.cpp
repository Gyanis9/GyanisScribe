#include "Ui/HotkeyEdit.h"

#include "Core/Logger.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QVBoxLayout>

#include <string>

namespace GyanisScribe::Ui
{
    std::optional<Qt::Key> editableKeyFromEvent(const Qt::Key key)
    {
        // 修饰键本身不是「按键本体」：只按到 Ctrl 时继续等下一个键，不能把半截组合写进配置
        switch (key)
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

        // 小键盘数字归到主键盘数字：同一颗键在两台机器上不该存成两种写法
        if (key >= Qt::Key_0 && key <= Qt::Key_9)
        {
            return key;
        }
        if (const auto shifted = static_cast<int>(key) - static_cast<int>(Qt::KeypadModifier); shifted >= static_cast<int>(Qt::Key_0) && shifted <= static_cast<int>(Qt::Key_9))
        {
            return static_cast<Qt::Key>(shifted);
        }
        return key;
    }

    HotkeyEdit::HotkeyEdit(Settings::ISettingsStore &settings, QWidget *parent) : QWidget(parent), m_settings(settings)
    {
        auto *layout = new QVBoxLayout{this};
        layout->setContentsMargins(0, 0, 0, 0);

        m_display = new QLabel{this};
        m_display->setFrameShape(QFrame::Box);
        m_display->setAlignment(Qt::AlignCenter);
        m_display->setMinimumWidth(120);
        m_display->setText(QStringLiteral("点击后按下快捷键"));
        layout->addWidget(m_display);

        setFocusPolicy(Qt::ClickFocus);
    }

    void HotkeyEdit::bindTo(const std::string_view key, const QString &title, const QString &defaultText)
    {
        m_key = std::string{key};
        // 无障碍名称与 tooltip 都写中文名：只有「Ctrl+Alt+A」这块文本时，读屏说不清它管哪个动作
        setAccessibleName(title);
        m_display->setAccessibleName(title);
        setToolTip(QStringLiteral("{} 的全局热键。点击这一格后按下组合键即可替换，按 Esc 取消").arg(title));

        const auto stored = m_settings.stringValue(key, defaultText);
        m_display->setText(stored);
    }

    QString HotkeyEdit::combinationText() const
    {
        return m_display->text();
    }

    Core::Expected<void> HotkeyEdit::applyCombination(const Hotkey::HotkeyCombination &combination)
    {
        if (auto usable = combination.checkUsableGlobally(); !usable)
        {
            // 不合格就什么都不写：半截组合落进配置，重启后热键直接登记不上，比现在改不动更难查
            showPrompt(combination.toText());
            return std::unexpected(usable.error());
        }

        const auto text = combination.toText();
        if (auto written = m_settings.setValue(m_key, text); !written)
        {
            showPrompt(QStringLiteral("写入失败"));
            return written;
        }
        m_display->setText(text);
        emit combinationChanged(text);
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
        m_recording = true;
        setFocus(Qt::MouseFocusReason);
        showPrompt(QStringLiteral("请按下快捷键（Esc 取消）"));
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
            m_recording = false;
            clearFocus();
            return;
        }

        const auto key = editableKeyFromEvent(static_cast<Qt::Key>(event->key()));
        if (!key)
        {
            // 只按到了修饰键：保持录制状态等真正的按键，别把 Ctrl 这种半截组合写进去
            return;
        }

        m_recording = false;
        if (auto applied = applyCombination(Hotkey::HotkeyCombination{event->modifiers(), *key}); !applied)
        {
            Core::Logger::warning(std::format("快捷键没能写入：{}", applied.error().message));
        }
        clearFocus();
    }

    void HotkeyEdit::showPrompt(const QString &text)
    {
        m_display->setText(text);
    }

} // namespace GyanisScribe::Ui
