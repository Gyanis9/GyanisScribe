// 取图那条路：只认 http/https、限时、限大小、认 content-type 四条判据各钉一次。
// 用本机一个一次性 HTTP 服务端喂固定响应，不碰外网：容器与 CI 上都能跑。
#include "App/ImageFetcher.h"

#include "App/ClipboardPin.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QImage>
#include <QMimeData>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using GyanisScribe::App::ImageFetcher;
using GyanisScribe::App::ImageFetchResult;
using GyanisScribe::App::imageUrlOf;
using GyanisScribe::App::isFetchableImageUrl;
using GyanisScribe::App::kMaximumFetchedImageBytes;

namespace
{
    /// 一张 8×4 的小图，编码成 PNG 字节：测试里当「服务端给的图片」用
    [[nodiscard]] QByteArray tinyPngBytes()
    {
        QImage image{8, 4, QImage::Format_ARGB32};
        image.fill(QColor{0, 128, 255});
        QByteArray encoded;
        QBuffer    buffer{&encoded};
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return encoded;
    }

    /**
     * @brief 一次性 HTTP 服务端：接一条请求就回一段固定响应
     * @details 大响应要等 socket 把缓冲区排空（一次 write 不一定全出去），所以按 bytesWritten 续写，
     *          写完了再断开——直接 write + disconnect 会在数据还没发完时把连接掐掉。
     */
    class CannedHttpServer
    {
    public:
        CannedHttpServer(const QByteArray &contentType, const QByteArray &body)
        {
            m_response = QByteArray("HTTP/1.1 200 OK\r\nContent-Type: ") + contentType + "\r\nContent-Length: " + QByteArray::number(body.size()) +
                         "\r\nConnection: close\r\n\r\n" + body;
            m_server.listen(QHostAddress::LocalHost, 0);
            QObject::connect(&m_server, &QTcpServer::newConnection, &m_server,
                             [this]
                             {
                                 auto *socket = m_server.nextPendingConnection();
                                 if (socket == nullptr)
                                 {
                                     return;
                                 }
                                 m_sockets.push_back(socket);
                                 // 先读到请求再回话：抢在客户端发完之前就答、又当场断开，对方会看到一个被掐掉的连接
                                 // （实测这么写的版本时红时绿：先读到请求头收尾的那个空行，再写响应）
                                 QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                                  [socket, this]
                                                  {
                                                      m_requests[socket] += socket->readAll();
                                                      const QByteArray headerEnd = QByteArrayLiteral("\r\n\r\n");
                                                      if (!m_requests[socket].contains(headerEnd) || m_answered.contains(socket))
                                                      {
                                                          return;
                                                      }
                                                      m_answered.insert(socket);
                                                      socket->write(m_response);
                                                      QObject::connect(socket, &QTcpSocket::bytesWritten, socket,
                                                                       [socket]
                                                                       {
                                                                           if (socket->bytesToWrite() == 0)
                                                                           {
                                                                               socket->disconnectFromHost();
                                                                           }
                                                                       });
                                                  });
                             });
        }

        [[nodiscard]] QString url() const
        {
            return QStringLiteral("http://127.0.0.1:%1/pic.png").arg(m_server.serverPort());
        }

        [[nodiscard]] bool listenOk() const
        {
            return m_server.isListening();
        }

    private:
        QTcpServer                         m_server;
        QByteArray                         m_response;
        std::vector<QTcpSocket *>          m_sockets;  ///< 只在测试进程里活这一会儿，随服务端析构由 Qt 收拾
        std::map<QTcpSocket *, QByteArray> m_requests; ///< 每个连接收进来的请求字节（要读到请求头收尾才回话）
        std::set<QTcpSocket *>             m_answered; ///< 已经答过的连接，别答第二遍
    };

    /// 跑一次取图，等它收场；返回（取到了吗，成功结果，失败原因）
    struct FetchOutcome
    {
        bool             succeeded = false;
        ImageFetchResult result;
        QString          reason;
        bool             timedOut = false;
    };

    [[nodiscard]] FetchOutcome fetchAndWait(ImageFetcher &fetcher, const QUrl &url, const int waitMilliseconds = 15000)
    {
        FetchOutcome outcome;
        QEventLoop   loop;
        QObject::connect(&fetcher, &ImageFetcher::fetched, &loop,
                         [&outcome, &loop](const ImageFetchResult &result)
                         {
                             outcome.succeeded = true;
                             outcome.result    = result;
                             loop.quit();
                         });
        QObject::connect(&fetcher, &ImageFetcher::failed, &loop,
                         [&outcome, &loop](const QString &reason)
                         {
                             outcome.reason = reason;
                             loop.quit();
                         });
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop,
                         [&outcome, &loop]
                         {
                             outcome.timedOut = true;
                             loop.quit();
                         });
        guard.start(waitMilliseconds);

        fetcher.fetch(url);
        loop.exec();
        return outcome;
    }
} // namespace

/**
 * @brief 钉住「这条地址值不值得去取」：只认 http/https 且带主机名
 * @details 放开 `file:` 就等于把「贴一条地址」变成「把这台机器上的任意文件读进来贴上屏幕」，
 *          所以这一条要单独钉住，而不是只在实现里写一句注释。
 */
TEST(ImageFetcherUrlRules, OnlyHttpAndHttpsWithAHost)
{
    EXPECT_TRUE(isFetchableImageUrl(QUrl{QStringLiteral("https://example.com/a.png")}));
    EXPECT_TRUE(isFetchableImageUrl(QUrl{QStringLiteral("http://127.0.0.1:8080/a.png")}));
    EXPECT_FALSE(isFetchableImageUrl(QUrl{QStringLiteral("file:///C:/Windows/win.ini")})) << "本地文件不该走取图这条路";
    EXPECT_FALSE(isFetchableImageUrl(QUrl{QStringLiteral("ftp://example.com/a.png")}));
    EXPECT_FALSE(isFetchableImageUrl(QUrl{QStringLiteral("https://")})) << "没有主机名的地址取不出东西";
    EXPECT_FALSE(isFetchableImageUrl(QUrl{})) << "空地址不是「取不到图」而是「没填地址」";
}

/**
 * @brief 钉住「载荷里只有一条 http/https 地址」这一条判据
 * @details 拖进来好几条链接时「先取哪一张」没有合理答案，那条要交回原来的文本卡那条路；
 *          本地文件也不在这里（它有自己读文件的那一套）。两条一起钉，免得放宽成「有地址就取」。
 */
TEST(ImageFetcherUrlRules, OnlyASingleWebAddressCounts)
{
    QMimeData one;
    one.setUrls({QUrl{QStringLiteral("https://example.com/a.png")}});
    ASSERT_TRUE(imageUrlOf(&one).has_value());
    EXPECT_EQ(imageUrlOf(&one)->toString(), QStringLiteral("https://example.com/a.png"));

    QMimeData several;
    several.setUrls({QUrl{QStringLiteral("https://example.com/a.png")}, QUrl{QStringLiteral("https://example.com/b.png")}});
    EXPECT_FALSE(imageUrlOf(&several).has_value()) << "多条链接不该挑一条去取";

    QMimeData local;
    local.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Windows/win.ini"))});
    EXPECT_FALSE(imageUrlOf(&local).has_value()) << "本地文件走的是读文件那条路";

    EXPECT_FALSE(imageUrlOf(nullptr).has_value()) << "空载荷不该崩也不该猜";
}

/**
 * @brief 取图那几条用例的装置：网络类要一个应用对象，一套用例共用一个
 * @details argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝。
 */
class ImageFetcherFixture : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        static int   argumentCount  = 1;
        static char  argumentZero[] = "TestImageFetcher";
        static char *arguments[]    = {argumentZero, nullptr};
        application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
    }

    static void TearDownTestSuite()
    {
        application.reset();
    }

    static std::unique_ptr<QCoreApplication> application;
};

std::unique_ptr<QCoreApplication> ImageFetcherFixture::application;
/**
 * @brief 钉住真的从本机服务端取回一张图
 * @details 走完整的网络那一圈：请求、content-type、字节累积、解码。期望的尺寸用手算：
 *          服务端给的是 8×4 的 PNG，取回来也该是 8×4，且来源地址就是请求的那一条。
 */
TEST_F(ImageFetcherFixture, FetchesAnImageFromALocalServer)
{
    CannedHttpServer server{"image/png", tinyPngBytes()};
    ASSERT_TRUE(server.listenOk()) << "本机端口没监听上，这一条跑不起来";

    ImageFetcher fetcher;
    const auto   outcome = fetchAndWait(fetcher, QUrl{server.url()});
    ASSERT_FALSE(outcome.timedOut) << "取图没有收场（判据等到超时）";
    ASSERT_TRUE(outcome.succeeded) << "没取到：" << outcome.reason.toStdString();
    EXPECT_EQ(outcome.result.image.size(), QSize(8, 4));
    EXPECT_EQ(outcome.result.source.toString(), server.url());
}

/**
 * @brief 钉住「不是图片就别贴」：content-type 说了算，且原因里点名它
 * @details 一条 HTML 错误页也能解码出乱像素的时代早就过去了，但用户拿到的提示要能看懂：
 *          所以这条同时钉「不许收下」与「原因里写着服务端给的是什么类型」。
 */
TEST_F(ImageFetcherFixture, RejectsANonImageContentType)
{
    CannedHttpServer server{"text/html", QByteArray("<html><body>404</body></html>")};
    ASSERT_TRUE(server.listenOk());

    ImageFetcher fetcher;
    const auto   outcome = fetchAndWait(fetcher, QUrl{server.url()});
    ASSERT_FALSE(outcome.timedOut);
    ASSERT_FALSE(outcome.succeeded) << "非图片类型被当成图片收下了";
    EXPECT_TRUE(outcome.reason.contains(QStringLiteral("text/html"))) << "原因里要点名服务端给的是什么类型：" << outcome.reason.toStdString();
}

/**
 * @brief 钉住大小闸门：超过上限的响应要中止并说清「太大」
 * @details 只看 Content-Length 挡不住分块传输，所以这里喂的是真的大响应：
 *          上限 + 1 MB，取图器收够 32 MB 就该当场放弃，而不是把它读完再报错。
 */
TEST_F(ImageFetcherFixture, RejectsAnOversizedImage)
{
    const QByteArray oversized(kMaximumFetchedImageBytes + 1024 * 1024, 'x');
    CannedHttpServer server{"image/png", oversized};
    ASSERT_TRUE(server.listenOk());

    ImageFetcher fetcher;
    const auto   outcome = fetchAndWait(fetcher, QUrl{server.url()});
    ASSERT_FALSE(outcome.timedOut) << "超大响应没有收场：大小闸门没生效";
    ASSERT_FALSE(outcome.succeeded);
    EXPECT_TRUE(outcome.reason.contains(QStringLiteral("MB"))) << "原因里要说清是撞上了大小上限：" << outcome.reason.toStdString();
}

/**
 * @brief 钉住「取不到也给一句能操作的话」
 * @details 端口上没人监听是最常见的一种失败：提示里要有「检查地址」这类出路，
 *          而不是把 Qt 的原文（Connection refused）直接甩给用户。
 */
TEST_F(ImageFetcherFixture, UnreachableAddressGivesAnActionableReason)
{
    QTcpServer probe;
    ASSERT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
    const auto deadPort = probe.serverPort();
    probe.close(); // 关掉之后这个端口上没人接

    ImageFetcher fetcher;
    const auto   outcome = fetchAndWait(fetcher, QUrl{QStringLiteral("http://127.0.0.1:%1/pic.png").arg(deadPort)}, 8000);
    ASSERT_FALSE(outcome.timedOut) << "连接失败没有收场";
    ASSERT_FALSE(outcome.succeeded);
    EXPECT_TRUE(outcome.reason.contains(QStringLiteral("检查地址"))) << "失败文案里要有出路：" << outcome.reason.toStdString();
}
