/**
 * @file PinCoordinator.h
 * @brief 贴图这一族从控制器里搬出来的落脚处（审查台账 A-11）
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "App/ImageFetcher.h"
#include "Base/Types.h"
#include "Gui/Controls/Toast.h"
#include "Gui/Pin/PinGroupManagerWindow.h"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>

#include <functional>
#include <optional>

class QMimeData;
class QUrl;

namespace GyanisScribe::App
{
    struct DropPinOutcome;
} // namespace GyanisScribe::App

namespace GyanisScribe::Gui
{
    class PinGroupManagerWindow;
    class PinManager;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::App
{

    /**
     * @brief 贴图族里「从别处来一张图/一段剪贴板/一个文件，把它摆到屏幕上」这一组动作
     * @details 只做贴图自己的事：内容判据、落到哪儿、贴不出时说什么。三条跨族的线没有硬连：
     *          要出给用户的话走 messageForUser；「剪贴板里是一条图片地址」那种要去网页取的，
     *          走构造时给进来的 FetchRequest（取图那条链在别处，贴图族不自己管它）；
     *          「贴回原来那块位置」的落点由调用方给——上次选区是截图那边的事。
     */
    class PinCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造
         * @param pins 贴图管理器；所有权不在这里，控制器还拿它做别的编排
         * @param parent Qt 父对象
         */
        PinCoordinator(GyanisScribe::Gui::PinManager &pins, QObject *parent = nullptr);

        /// 再贴一份（不动原来那张，新的一张摆在它右下一点）
        void pinAnother(const QImage &image, const QPoint &anchor);

        /**
         * @brief 有东西被拖到贴图上：文件/链接/文本按同一套内容渲染贴出来
         * @param data 拖进来的载荷
         * @param anchor 落在哪儿（全局逻辑坐标）
         * @details 拖进来的是一条 http/https 地址时先去把它取回来（见 fetchImageUrl），
         *          落在拖到的那一点上而不是光标处。
         */
        void handleDrop(const QMimeData *data, const QPoint &anchor);

        /// 去把一条图片地址取回来并贴到屏上；成败都在这一族的信号上收口
        void fetchImageUrl(const QUrl &url, std::optional<QPoint> dropPoint);

        /**
         * @brief 把一张图贴回它被截下的位置
         * @param image 要贴的图
         * @param anchor 贴到哪儿（全局逻辑坐标）：截图那边给的是这次选区的左上角
         * @details 贴回原来那块位置是有意的：用户截哪儿就贴在哪儿，跳回主屏中央会打断「就地比对」这个用法。
         */
        void pinAt(const QImage &image, const QPoint &anchor);

        /**
         * @brief 把一个盘上的文件贴到屏幕上
         * @param filePath 文件路径（显示文本）
         * @return 贴成了返回空；不是能贴的图或文件不在时返回带中文原因的错误
         */
        [[nodiscard]] GyanisScribe::Base::Expected<void> pinFile(const QString &filePath);

        /**
         * @brief 把剪贴板里的内容贴成一张贴图
         * @details 三种内容各走各的路：文件走拖放那条内容转换（可以一次贴多件）、
         *          一条 http/https 地址去取回来、图像与纯文本/颜色直接渲染成贴图或卡片。
         */
        void pasteClipboard();

        /// 把某一张贴图的内容换成剪贴板里那一份（动图与不在册的拒绝面都带中文原因）
        void replaceWithClipboard(int index);

        /// 把最近关掉的那张重新贴出来（关掉的那几张最多记 5 张，「销毁」掉的不算）
        void repinLastClosed();

        /// 打开（或抬起已开着的）贴图组管理窗
        void openGroupManager();

        /// 说一句「贴出 N 张 / 哪几个不是图片没贴」
        void showDropOutcome(const GyanisScribe::App::DropPinOutcome &outcome);

        /// 分组标签：有名字写成「分组 N：名字」，没名字只写号（号才是切组与菜单用的那个）
        [[nodiscard]] QString groupLabel(int group) const;

        /// 说一句改名结果（改名与「取消名字」共用这一处措辞）
        void reportGroupRenamed(int group, const QString &name);

        /// 说一句删组结果：组里的贴图搬去了哪儿要讲清楚
        void reportGroupDeleted(int group, int movedCount);

        /// 让开着鼠标穿透的那几张重新吃鼠标；一张都没恢复也要说一声（用户按这条是因为觉得贴图点不动）
        void restoreMouseThrough();

        /// 托盘那一条开关：看得见就先收、一张都看不见就摆回（收着的贴图用户点不到，摆回只能由托盘与热键给）
        void toggleVisibility();

        /// 切到下一组；只有一组时把「怎么多出一组」说清楚（按下去毫无动静最容易被当成坏了）
        void switchToNextGroup();

        /// 一次关掉全部：张数由管理器自己的信号通知托盘刷新，这里只留一条日志，不再弹提示吵人
        void closeAll();

    signals:
        /// 要说给用户的一句话（由控制器决定用气泡还是落日志）
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

    private:
        GyanisScribe::Gui::PinManager                     &m_pins;              ///< 贴图管理器，不拥有
        GyanisScribe::App::ImageFetcher                    m_fetcher;           ///< 网页取图：拖进来的可以是一条图片地址
        std::optional<QPoint>                              m_pendingFetchPoint; ///< 在飞那一张取回来落在哪儿；空表示跟光标走
        QPointer<GyanisScribe::Gui::PinGroupManagerWindow> m_window;            ///< 组管理窗，开着一扇就复用那一扇
    };

} // namespace GyanisScribe::App
