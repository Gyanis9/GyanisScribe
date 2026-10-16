#include "App/PinCoordinator.h"

#include "App/ClipboardPin.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Gui/Pin/PinGroupManagerWindow.h"
#include "Gui/Pin/PinManager.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

#include <format>

namespace GyanisScribe::App
{
    PinCoordinator::PinCoordinator(GyanisScribe::Gui::PinManager &pins, QObject *parent) : QObject(parent), m_pins(pins)
    {
        // 取图的成败都在这一族收口：贴到哪儿、提示什么话，只有拿着贴图管理器的人知道
        QObject::connect(&m_fetcher, &GyanisScribe::App::ImageFetcher::fetched, this,
                         [this](const GyanisScribe::App::ImageFetchResult &result)
                         {
                             const auto at = m_pendingFetchPoint.value_or(QCursor::pos());
                             m_pendingFetchPoint.reset();
                             m_pins.addPin(result.image, at);
                             emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已从网页取到这张图（{}×{}）并贴到屏幕上"),
                                                                                                result.image.width(), result.image.height())
                                                                           .c_str()),
                                                 GyanisScribe::Gui::ToastKind::Information);
                         });
        QObject::connect(&m_fetcher, &GyanisScribe::App::ImageFetcher::failed, this,
                         [this](const QString &reason)
                         {
                             m_pendingFetchPoint.reset();
                             emit messageForUser(reason, GyanisScribe::Gui::ToastKind::Information);
                         });
    }

    void PinCoordinator::pinAnother(const QImage &image, const QPoint &anchor)
    {
        // 留底再贴一份：不动原来那张，新的一张摆在它右下一点（落点由管理器算好交进来）
        m_pins.addPin(image, anchor);
    }

    void PinCoordinator::handleDrop(const QMimeData *data, const QPoint &anchor)
    {
        // 网页上的图：拖进来的是一条 http/https 地址时去取回来，落在拖到的那一点上
        if (const auto url = GyanisScribe::App::imageUrlOf(data); url.has_value())
        {
            fetchImageUrl(*url, anchor);
            return;
        }
        const auto dropped = GyanisScribe::App::pinDroppedContent(m_pins, data, anchor, QApplication::font());
        if (!dropped)
        {
            emit messageForUser(QString::fromUtf8(dropped.error().message), GyanisScribe::Gui::ToastKind::Information);
            return;
        }
        showDropOutcome(*dropped);
    }

    void PinCoordinator::fetchImageUrl(const QUrl &url, std::optional<QPoint> dropPoint)
    {
        m_pendingFetchPoint = dropPoint;
        // 先把「正在取」说出来：取图是异步的，不说这一句用户会以为「按了没反应」
        emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("正在从网页取这张图：{}"), url.toString().toStdString()).c_str()),
                            GyanisScribe::Gui::ToastKind::Information);
        m_fetcher.fetch(url);
    }

    void PinCoordinator::pinAt(const QImage &image, const QPoint &anchor)
    {
        // 贴回原来那块位置：用户截哪儿就贴在哪儿，跳回主屏中央会打断「就地比对」这个用法
        m_pins.addPin(image, anchor);
        GyanisScribe::Base::Logger::info(std::format("已贴出 {}×{} 像素", image.width(), image.height()));
    }

    GyanisScribe::Base::Expected<void> PinCoordinator::pinFile(const QString &filePath)
    {
        const QFileInfo info{filePath};
        if (!info.exists() || !info.isFile())
        {
            return std::unexpected(
                    GyanisScribe::Base::Error{GyanisScribe::Base::ErrorCode::InvalidArgument,
                                              GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("没找到这个文件：{}"), info.absoluteFilePath().toStdString())});
        }

        // 复用拖放那条内容转换：图片读像素（GIF 连来源带走）、链接渲染成卡片，判据只有一份
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(info.absoluteFilePath())});
        const auto outcome = GyanisScribe::App::pinDroppedContent(m_pins, &mime, QCursor::pos(), QApplication::font());
        if (!outcome)
        {
            return std::unexpected(outcome.error());
        }
        if (outcome->pinnedCount == 0)
        {
            return std::unexpected(GyanisScribe::Base::Error{
                    GyanisScribe::Base::ErrorCode::InvalidArgument,
                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("「{}」不是能贴的图片：支持 PNG / JPG / WebP / GIF 这类格式"), info.fileName().toStdString())});
        }
        return {};
    }

    void PinCoordinator::pasteClipboard()
    {
        const auto *data = QGuiApplication::clipboard()->mimeData();
        // 剪贴板里是文件（资源管理器里复制的那一份）：走拖放那条内容转换——图片读像素、动图连来源带走，
        // 一次可以贴多件；判据只有「能不能当图片贴」这一条，与拖进来的那一份共用
        if (data != nullptr && data->hasUrls() && !data->hasImage())
        {
            // 网页上的图：剪贴板里只有一条 http/https 地址时去把它取回来（本地文件那条路照旧走下面）
            if (const auto url = GyanisScribe::App::imageUrlOf(data); url.has_value())
            {
                fetchImageUrl(*url, std::nullopt);
                return;
            }
            const auto outcome = GyanisScribe::App::pinDroppedContent(m_pins, data, QCursor::pos(), QApplication::font());
            if (!outcome)
            {
                emit messageForUser(QString::fromUtf8(outcome.error().message), GyanisScribe::Gui::ToastKind::Information);
                return;
            }
            if (outcome->pinnedCount == 0)
            {
                emit messageForUser(GyanisScribe::Base::uiText("复制的文件里没有能当图片贴的：请复制图片文件本身，或复制图片内容"), GyanisScribe::Gui::ToastKind::Information);
                return;
            }
            showDropOutcome(*outcome);
            return;
        }

        const auto image = GyanisScribe::App::pinContentFrom(data, QGuiApplication::font());
        if (!image)
        {
            // 取不到就直说原因与替代做法：这一条的入口在托盘上，附近没有任何输入框能提示
            emit messageForUser(QString::fromUtf8(image.error().message), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        // 落在光标那一处：剪贴板来的图没有「原来那块位置」，跟着鼠标走才是用户此刻的落点
        // 颜色写法贴出来的是色卡：把那个色一起交下去，右键「改这一张的颜色…」以后才有初值
        m_pins.addPin(*image, QCursor::pos(), {}, GyanisScribe::App::colorCardColorOf(data));
        GyanisScribe::Base::Logger::info(std::format("剪贴板里的图已贴出：{}×{} 像素", image->width(), image->height()));
    }

    void PinCoordinator::replaceWithClipboard(const int index)
    {
        // 与「把剪贴板贴到屏幕上」共用同一条内容判据：图像原样换，纯文本与颜色写法换成各自的卡片
        const auto image = GyanisScribe::App::pinContentFrom(QGuiApplication::clipboard()->mimeData(), QApplication::font());
        if (!image)
        {
            emit messageForUser(QString::fromUtf8(image.error().message), GyanisScribe::Gui::ToastKind::Information);
            return;
        }
        // 色卡身份跟着换进来的内容走：换成颜色写法就记下那个色，换成别的就把这一档摘掉（传空）
        if (const auto replaced = m_pins.replacePinContent(index, *image, GyanisScribe::App::colorCardColorOf(QGuiApplication::clipboard()->mimeData())); !replaced)
        {
            // 拒绝面都带中文原因（动图不换、那张已不在册），原来的那份一律保持不动
            emit messageForUser(QString::fromUtf8(replaced.error().message), GyanisScribe::Gui::ToastKind::Information);
            return;
        }
        GyanisScribe::Base::Logger::info(std::format("贴图内容已换成剪贴板里那一份：{}×{} 像素", image->width(), image->height()));
    }

    void PinCoordinator::repinLastClosed()
    {
        const auto taken = m_pins.takeClosedPin();
        if (!taken)
        {
            // 空册也要回一句：这条入口在托盘上，不出声就成了「按了没反应」
            emit messageForUser(GyanisScribe::Base::uiText("没有可以重新贴出的贴图：关掉的那几张最多记 5 张，「销毁」掉的不算"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        m_pins.addPin(taken->image, QCursor::pos(), taken->sourceFile);
        emit messageForUser(
                QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已重新贴出那张关掉的贴图，还能按回来 {} 张"), m_pins.closedPinCount()).c_str()),
                GyanisScribe::Gui::ToastKind::Success);
    }

    void PinCoordinator::openGroupManager()
    {
        // 开着一扇就抬那一扇重读：两扇表对着同一桌贴图，晚打开的那一扇会把「摆在桌上」那一列停在旧账上。
        // 不给父对象：挂在设置窗上会让这扇窗跟着设置窗一起被藏起来，用户回头找不着它；
        // 关掉即自己销毁（窗口构造里挂了 WA_DeleteOnClose），这里这份 QPointer 随即变空
        if (m_window.isNull())
        {
            m_window = new GyanisScribe::Gui::PinGroupManagerWindow(m_pins);
        }
        m_window->openAndRefresh();
    }

    void PinCoordinator::showDropOutcome(const GyanisScribe::App::DropPinOutcome &outcome)
    {
        if (!outcome.rejectedNames.isEmpty())
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已贴出 {} 张；{} 不是图片，没贴"), outcome.pinnedCount,
                                                                               outcome.rejectedNames.join(QStringLiteral("、")).toStdString())
                                                          .c_str()),
                                GyanisScribe::Gui::ToastKind::Information);
            return;
        }
        emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已贴出 {} 张"), outcome.pinnedCount).c_str()),
                            GyanisScribe::Gui::ToastKind::Success);
    }

    QString PinCoordinator::groupLabel(const int group) const
    {
        // 号还是那个号：切组、菜单与布局都按号走，名字只是挂在后面的标签，替掉号会让人认不出自己在哪一组
        const auto name = m_pins.groupName(group);
        return name.isEmpty() ? GyanisScribe::Base::uiText("分组 %1").arg(group) : GyanisScribe::Base::uiText("分组 %1：%2").arg(group).arg(name);
    }

    void PinCoordinator::reportGroupRenamed(const int group, const QString &name)
    {
        emit messageForUser(name.isEmpty() ? GyanisScribe::Base::uiText("已取消分组 %1 的名字，切组与菜单里重新按号叫它").arg(group)
                                           : GyanisScribe::Base::uiText("分组 %1 改名为「%2」").arg(group).arg(name),
                            name.isEmpty() ? GyanisScribe::Gui::ToastKind::Information : GyanisScribe::Gui::ToastKind::Success);
    }

    void PinCoordinator::reportGroupDeleted(const int group, const int movedCount)
    {
        emit messageForUser(movedCount > 0 ? GyanisScribe::Base::uiText("已删掉分组 %1，把 %2 张并回第一组").arg(group).arg(movedCount)
                                           : GyanisScribe::Base::uiText("分组 %1 上本来没摆贴图，名字一并清了").arg(group),
                            GyanisScribe::Gui::ToastKind::Success);
    }

    void PinCoordinator::restoreMouseThrough()
    {
        const auto restored = m_pins.clearMouseThrough();
        // 一张都没恢复也要说一声：用户按这条是因为他觉得贴图「点不动」，
        // 没有这句他就会一直以为是程序卡住了
        emit messageForUser(restored > 0 ? QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已让 {} 张贴图重新吃鼠标"), restored).c_str())
                                         : GyanisScribe::Base::uiText("没有贴图开着鼠标穿透"),
                            restored > 0 ? GyanisScribe::Gui::ToastKind::Success : GyanisScribe::Gui::ToastKind::Information);
    }

    void PinCoordinator::toggleVisibility()
    {
        // 看得见就先收、一张都看不见就摆回：这一条是开关，不是单向的隐藏。
        // 收着的贴图用户点不到，摆回这一格只能由托盘与热键给
        const auto show    = m_pins.visiblePinCount() == 0;
        const auto changed = m_pins.setAllPinsConcealed(!show);
        emit       messageForUser(
                changed > 0 ? QString::fromUtf8(GyanisScribe::Base::uiFormat(
                                                        show ? GyanisScribe::Base::uiTextUtf8("已摆回 {} 张贴图") : GyanisScribe::Base::uiTextUtf8("已收起 {} 张贴图"), changed)
                                                        .c_str())
                            : GyanisScribe::Base::uiText("桌上没有贴图"),
                changed > 0 ? GyanisScribe::Gui::ToastKind::Success : GyanisScribe::Gui::ToastKind::Information);
    }

    void PinCoordinator::switchToNextGroup()
    {
        const auto switched = m_pins.switchToNextGroup();
        if (!switched)
        {
            // 只有一组时把「怎么多出一组」说清楚：热键按下去毫无动静最容易被当成坏了
            emit messageForUser(QString::fromUtf8(switched.error().message.c_str()), GyanisScribe::Gui::ToastKind::Information);
            return;
        }
        emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已切到{}，桌上是这一组"), groupLabel(*switched).toStdString()).c_str()),
                            GyanisScribe::Gui::ToastKind::Success);
    }

    void PinCoordinator::closeAll()
    {
        GyanisScribe::Base::Logger::info(std::format("托盘要求关闭全部贴图：{} 张", m_pins.count()));
        m_pins.closeAll();
    }

} // namespace GyanisScribe::App
