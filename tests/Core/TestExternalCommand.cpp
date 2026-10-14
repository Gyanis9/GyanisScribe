// 截图后那条外部命令的两层判据：拆得对不对（纯逻辑）与起不起得来（真起一个进程）。
// 拆错只会让脚本拿着错参数跑下去，起不来则有「静默什么都没发生」这一种更难查的失败，所以两层都要钉。
#include "Core/ExternalCommand.h"

#include "Core/Types.h"

#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <string>

using GyanisScribe::Core::ExternalCommandPlan;
using GyanisScribe::Core::kExternalCommandFilePlaceholder;
using GyanisScribe::Core::planExternalCommand;
using GyanisScribe::Core::startExternalCommand;

namespace
{
    /// 探针就是这一份测试可执行体自己：带一个匹配不到任何用例的过滤器，起得来、立刻退，不会递归跑用例
    constexpr const char *kProbeFilter = "--gtest_filter=GyanisScribeAbsentSuite.*";

    [[nodiscard]] QString probeProgram()
    {
        return QString::fromUtf8(GYANISSCRIBE_EXTERNAL_COMMAND_PROBE);
    }
} // namespace

/**
 * @brief 拆法按 shell 的引号规则：带空格的程序名整段算一个
 * @details 这一档决定了用户在那里学会的写法在这里是否成立；按空格硬拆会把「C:/Program Files/x.exe」拆成三段
 */
TEST(ExternalCommandPlan, QuotedProgramStaysOnePiece)
{
    const QStringList expectedArguments{QStringLiteral("--path"), QStringLiteral("x")};
    const auto        planned = planExternalCommand(QStringLiteral("\"C:/Program Files/上传.exe\" --path x"), QString{});
    ASSERT_TRUE(planned.has_value());
    EXPECT_EQ(planned->program, QStringLiteral("C:/Program Files/上传.exe"));
    EXPECT_EQ(planned->arguments, expectedArguments);
}

/**
 * @brief `%file%` 换成成品路径：独立一段与连写（`--out=%file%`）都认
 */
TEST(ExternalCommandPlan, ReplacesFilePlaceholderWithSavedPath)
{
    const QStringList plainExpected{QStringLiteral("--path"), QStringLiteral("D:/我的 图/a.png")};
    const auto        plain = planExternalCommand(QStringLiteral("tool --path %file%"), QStringLiteral("D:/我的 图/a.png"));
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->arguments, plainExpected);

    const QStringList gluedExpected{QStringLiteral("--out=D:/b.png")};
    const auto        glued = planExternalCommand(QStringLiteral("tool --out=%file%"), QStringLiteral("D:/b.png"));
    ASSERT_TRUE(glued.has_value());
    EXPECT_EQ(glued->arguments, gluedExpected);
}

/**
 * @brief 命令里没有占位符时，这一轮没落盘也算得过去：不是所有命令都要那份文件
 */
TEST(ExternalCommandPlan, CommandWithoutPlaceholderNeedsNoSavedFile)
{
    const QStringList expectedArguments{QStringLiteral("截好了一张")};
    const auto        planned = planExternalCommand(QStringLiteral("通知.exe 截好了一张"), QString{});
    ASSERT_TRUE(planned.has_value());
    EXPECT_EQ(planned->arguments, expectedArguments);
}

/**
 * @brief 占位符落在程序名上、命令行整串为空，都在拆分这一层被拒
 * @details 程序名换成路径就成了「用一张图片当程序跑」；空串则是用户填了个空格还以为填了命令
 */
TEST(ExternalCommandPlan, RejectsPlaceholderAsProgramAndEmptyLine)
{
    const auto placeholderAsProgram = planExternalCommand(QStringLiteral("%file% --path x"), QStringLiteral("D:/a.png"));
    ASSERT_FALSE(placeholderAsProgram.has_value());
    EXPECT_EQ(placeholderAsProgram.error().code, GyanisScribe::Core::ErrorCode::InvalidArgument);
    EXPECT_NE(placeholderAsProgram.error().message.find("%file%"), std::string::npos) << placeholderAsProgram.error().message;

    for (const auto *empty: {"", "   "})
    {
        const auto blank = planExternalCommand(QString::fromUtf8(empty), QStringLiteral("D:/a.png"));
        ASSERT_FALSE(blank.has_value()) << empty;
        EXPECT_NE(blank.error().message.find("空的"), std::string::npos) << blank.error().message;
    }
}

/**
 * @brief 命令要 `%file%` 而这一轮没落盘：拒绝，而不是把那一格换成空串交出去
 * @details 换成空串跑出去的是「少了一个参数」的命令，外部程序按自己的方式报错，用户那头只看到「截完什么都没发生」
 */
TEST(ExternalCommandPlan, RejectsPlaceholderWhenNothingWasSaved)
{
    const auto planned = planExternalCommand(QStringLiteral("上传.exe --path %file%"), QString{});
    ASSERT_FALSE(planned.has_value());
    EXPECT_EQ(planned.error().code, GyanisScribe::Core::ErrorCode::InvalidArgument);
    EXPECT_NE(planned.error().message.find("文件"), std::string::npos) << planned.error().message;
}

/**
 * @brief 起跑：真的把一个在场的程序交出去（探针是这一份测试可执行体自己）
 */
TEST(ExternalCommandStart, LaunchesProgramThatExists)
{
    const ExternalCommandPlan plan{.program = probeProgram(), .arguments = {QString::fromUtf8(kProbeFilter)}};
    const auto                started = startExternalCommand(plan);
    ASSERT_TRUE(started.has_value()) << started.error().message;
}

/**
 * @brief 起跑失败要报出是哪一个程序开不起来
 * @details 这一条是「静默失败」的对照面：设置里那一格填错程序名时，用户要看到名字才知道改哪
 */
TEST(ExternalCommandStart, ReportsWhichProgramDidNotStart)
{
    const ExternalCommandPlan plan{.program = QStringLiteral("GyanisScribeNoSuchProgram-3f9a.exe"), .arguments = {}};
    const auto                started = startExternalCommand(plan);
    ASSERT_FALSE(started.has_value());
    EXPECT_EQ(started.error().code, GyanisScribe::Core::ErrorCode::DependencyMissing);
    EXPECT_NE(started.error().message.find("GyanisScribeNoSuchProgram-3f9a.exe"), std::string::npos) << started.error().message;
}
