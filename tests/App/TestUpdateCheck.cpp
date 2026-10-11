// 检查更新的判据全落在纯函数上：解析 GitHub 的 latest 回应、拿本机版本与它比。
// 联网那一层（UpdateChecker）不在用例里跑——CI 不该依赖 GitHub 的可用性与限额。

#include "App/UpdateCheck.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QNetworkReply>
#include <QString>

#include <string>

using GyanisScribe::App::failureMessageFor;
using GyanisScribe::App::parseLatestRelease;
using GyanisScribe::App::UpdateRelease;
using GyanisScribe::App::verdictFor;

/**
 * @brief 钉住能从 latest 回应里取出标签与发布页
 */
TEST(UpdateReleaseParsing, ReadsTagAndPageFromTheLatestPayload)
{
    const auto payload = QByteArray{R"({"tag_name":"v0.0.2","html_url":"https://github.com/Gyanis9/GyanisScribe/releases/tag/v0.0.2","name":"0.0.2"})"};

    const auto parsed = parseLatestRelease(payload);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(parsed->tagName, QStringLiteral("v0.0.2"));
    EXPECT_EQ(parsed->pageUrl, QStringLiteral("https://github.com/Gyanis9/GyanisScribe/releases/tag/v0.0.2"));
}

/**
 * @brief 钉住四种读不出来的回应各自给一句可操作的话
 * @details 中间被代理换成登录页、仓库还没有发布、响应缺字段——这三种在用户那儿长得一样
 *          （都是「点了没反应」），所以拒因要分得开，且都说清下一步去哪看。
 */
TEST(UpdateReleaseParsing, RejectsPayloadsWithoutTagOrPage)
{
    EXPECT_FALSE(parseLatestRelease(QByteArray{"[]"}).has_value()) << "数组不是一份发布信息";
    EXPECT_FALSE(parseLatestRelease(QByteArray{"<html>被中间设备换成了登录页</html>"}).has_value()) << "不是 JSON 要当场拒";

    const auto noTag = parseLatestRelease(QByteArray{R"({"html_url":"https://example.invalid"})"});
    ASSERT_FALSE(noTag.has_value());
    EXPECT_NE(noTag.error().message.find("版本标签"), std::string::npos) << "要说清缺的是哪一项";

    const auto noPage = parseLatestRelease(QByteArray{R"({"tag_name":"v1.0.0"})"});
    ASSERT_FALSE(noPage.has_value());
    EXPECT_NE(noPage.error().message.find("发布页"), std::string::npos);
}

/**
 * @brief 钉住「有新版本」给链接、「一样或更新」不给
 * @details 本机比远端新是开发中的构建常有的形状，那种不该提示用户去升级。
 */
TEST(UpdateVerdict, NewerTagTurnsIntoALinkWhileSameOrOlderDoNot)
{
    const UpdateRelease newer{.tagName = QStringLiteral("v0.0.2"), .pageUrl = QStringLiteral("https://github.com/Gyanis9/GyanisScribe/releases/tag/v0.0.2")};

    const auto fresh = verdictFor(QStringLiteral("0.0.1"), newer);
    EXPECT_TRUE(fresh.hasNewer);
    EXPECT_EQ(fresh.link, newer.pageUrl) << "有新版本时要给出能点的地址";
    EXPECT_NE(fresh.message.indexOf(QStringLiteral("0.0.2")), -1) << "文案里要写清是哪一个版本";

    const auto same = verdictFor(QStringLiteral("0.0.2"), newer);
    EXPECT_FALSE(same.hasNewer);
    EXPECT_TRUE(same.link.isEmpty()) << "已经是最新时不该再摆一个下载链接";

    const auto ahead = verdictFor(QStringLiteral("0.1.0"), newer);
    EXPECT_FALSE(ahead.hasNewer) << "本机版本比远端新（开发构建）不该提示升级";
}

/**
 * @brief 钉住版本号读不出来时「不比」而不是猜一个结果
 * @details 标签写法在 GitHub 上不止一种：前面带 v、带空白都要认；认不出的就说认不出。
 */
TEST(UpdateVerdict, UnreadableVersionsSaySoInsteadOfGuessing)
{
    const auto badRunning = verdictFor(QStringLiteral("dev-build"), UpdateRelease{.tagName = QStringLiteral("v1.0.0"), .pageUrl = QStringLiteral("https://example.invalid")});
    EXPECT_FALSE(badRunning.hasNewer);
    EXPECT_NE(badRunning.message.indexOf(QStringLiteral("读不出来")), -1);

    const auto badTag = verdictFor(QStringLiteral("0.0.1"), UpdateRelease{.tagName = QStringLiteral("发布二号"), .pageUrl = QStringLiteral("https://example.invalid")});
    EXPECT_FALSE(badTag.hasNewer);
    EXPECT_NE(badTag.message.indexOf(QStringLiteral("读不出来")), -1);

    const auto tolerant = verdictFor(QStringLiteral("0.0.1"), UpdateRelease{.tagName = QStringLiteral(" V0.0.2 "), .pageUrl = QStringLiteral("https://example.invalid")});
    EXPECT_TRUE(tolerant.hasNewer) << "大写 V 与首尾空白都是常见写法，该认";
}

/**
 * @brief 钉住三条常见失败各给一句能照做的话，且不把 Qt 的英文原句摆给用户
 * @details 私有仓库与「还没发布过正式版」在未鉴权的接口上是同一个 404，文案要把两种都说上，
 *          并给出「去发布页看」这条替代做法。
 */
TEST(UpdateFailureText, NamesTheCommonCasesAndStaysChinese)
{
    const auto notFound = failureMessageFor(QNetworkReply::ContentNotFoundError, 404);
    EXPECT_NE(notFound.indexOf(QStringLiteral("404")), -1);
    EXPECT_NE(notFound.indexOf(QStringLiteral("发布页")), -1) << "要说清下一步去哪看";

    const auto denied = failureMessageFor(QNetworkReply::ContentAccessDenied, 403);
    EXPECT_NE(denied.indexOf(QStringLiteral("403")), -1) << "限流那一档要带上状态码，便于用户自己和 GitHub 的记录对上";

    const auto offline = failureMessageFor(QNetworkReply::HostNotFoundError, 0);
    EXPECT_NE(offline.indexOf(QStringLiteral("域名解析不了")), -1) << "网络类要给可读的原因名，而不是一个枚举数字";

    for (const auto &message: {notFound, denied, offline})
    {
        EXPECT_EQ(message.indexOf(QStringLiteral("Error")), -1) << "把 Qt 的英文原句交出去：" << message.toStdString();
    }
}
