#include "App/SingleInstance.h"

#include "Base/Logger.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QCoreApplication>

#include <format>

namespace
{
    /// 试探连接的等待上限，毫秒：连不上就说明没人持锁，等更久也只是把启动拖慢
    constexpr int kProbeTimeoutMs = 250;

    /// 通知已持锁那一份时的等待上限，毫秒
    constexpr int kNotifyTimeoutMs = 500;

} // namespace

namespace GyanisScribe::App
{
    QString instanceKey()
    {
        // 名字取应用标识：开发版与安装版的 applicationName 不同，同机共存时不该互相挡住启动
        const auto name = QCoreApplication::applicationName();
        return QStringLiteral("GyanisScribe-") + (name.isEmpty() ? QStringLiteral("unnamed") : name);
    }

    SingleInstance::~SingleInstance()
    {
        if (m_server != nullptr)
        {
            m_server->close();
            // 退出时把本机上的名字清掉，否则下一次启动会连到一个没人接的管道上
            QLocalServer::removeServer(instanceKey());
        }
    }

    bool SingleInstance::acquire()
    {
        if (m_server != nullptr)
        {
            return true;
        }

        {
            QLocalSocket probe;
            probe.connectToServer(instanceKey());
            if (probe.waitForConnected(kProbeTimeoutMs))
            {
                // 连上了：锁在别人手里，本次进程该退出去，而不是两个实例抢同一批全局热键
                probe.disconnectFromServer();
                return false;
            }
        }

        // 到这一步只有两种可能：没人持锁，或上一次崩溃留下了一个没人接的名字。清掉再监听
        QLocalServer::removeServer(instanceKey());

        m_server = new QLocalServer(this);
        QObject::connect(m_server, &QLocalServer::newConnection, this,
                         [this]
                         {
                             // 对方只发一句暗号，读完就断：留着半开的管道会让下一次试探连接误判成「有人在跑」
                             while (m_server != nullptr && m_server->hasPendingConnections())
                             {
                                 auto *socket = m_server->nextPendingConnection();
                                 socket->deleteLater();
                             }
                             emit secondLaunchRequested();
                         });

        if (!m_server->listen(instanceKey()))
        {
            Base::Logger::warning(std::format("单实例检测没能建立监听（{}）：本次启动按「已有实例」退出，以免两份实例抢同一批全局热键", m_server->errorString().toStdString()));
            delete m_server;
            m_server = nullptr;
            return false;
        }
        return true;
    }

    bool SingleInstance::notifyExisting()
    {
        // 「连上了」本身就是敲门：不写任何东西，也就不需要等对方读——写会撞上监听器刚把这条连接收走，
        // 于是通知失败而用户看到第二次启动毫无反应
        QLocalSocket socket;
        socket.connectToServer(instanceKey());
        if (!socket.waitForConnected(kNotifyTimeoutMs))
        {
            return false;
        }
        socket.disconnectFromServer();
        return true;
    }

} // namespace GyanisScribe::App
