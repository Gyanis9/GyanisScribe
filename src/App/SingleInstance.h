/**
 * @file SingleInstance.h
 * @brief 单实例检测：第二个启动把已有的那一份唤到前台，自己退出
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace GyanisScribe::App
{

    /**
     * @brief 本机的实例标识名
     * @return 由应用名拼成的固定串，Windows 走命名管道
     * @details 名字里带应用名而不是写死一个串：两份不同构建（比如开发版与安装版）在同机
     *          共存时不该互相把对方当成「已经有人在跑了」。
     */
    [[nodiscard]] QString instanceKey();

    /**
     * @brief 单实例守卫
     * @details 只负责「这把锁我拿到没有」与「别人来敲门时发信号」，不管界面怎么摆：
     *          唤到前台具体做什么由控制器接 secondLaunchRequested 决定。
     */
    class SingleInstance : public QObject
    {
        Q_OBJECT

    public:
        using QObject::QObject;

        /**
         * @brief 析构时关掉监听并删掉本机上的名字
         */
        ~SingleInstance() override;

        /**
         * @brief 试着当那个「唯一的实例」
         * @return true 表示锁拿到手，可以正常启动；false 表示本次不该继续——锁在别人手里，
         *         或本机监听没能建起来（那时也按退出处理，免得两份实例抢同一批全局热键）
         * @details 先连再听：连上了说明别人持锁。连不上时把盘上残留的名字清掉再监听——
         *          上一次崩溃会留下一个没人接的管道名，不清掉就再也没能启动。
         * @note 调用方拿到 true 之后不要再调一次：第二次会连上自己刚建的管道，把本进程判成非主实例
         */
        bool acquire();

        /**
         * @brief 通知已持锁的那一份把自己唤到前台
         * @return true 表示对方收到了
         * @details 「连上了」就是敲门，不写数据：写会撞上监听器刚把这条连接收走。
         *          有界等待，连不上就返回 false，调用方按「没能唤起重启」那一支走，
         *          而不是卡在一条没人接的管道上。
         */
        bool notifyExisting();

    signals:
        /// 第二个进程敲门了：把主窗口或托盘菜单唤出来
        void secondLaunchRequested();

    private:
        QLocalServer *m_server = nullptr; ///< 拿到锁后持有的监听器，未拿到时为空
    };

} // namespace GyanisScribe::App
