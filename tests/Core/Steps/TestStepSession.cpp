#include "Core/Steps/StepSession.h"

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QPoint>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace GyanisScribe::Core;

namespace
{
    std::filesystem::path toPath(const QString &path)
    {
        return std::filesystem::path{path.toStdWString()};
    }

    RecordedStep clickStep(const QPoint point, const QString &element = QStringLiteral("保存"))
    {
        RecordedStep step;
        step.kind               = StepKind::Click;
        step.at                 = QDateTime::currentDateTime();
        step.globalLogicalPoint = point;
        step.windowTitle        = QStringLiteral("Demo");
        step.processName        = QStringLiteral("demo.exe");
        step.elementName        = element;
        step.controlType        = QStringLiteral("按钮");
        return step;
    }

    QImage solidImage(const int width, const int height)
    {
        QImage image{width, height, QImage::Format_ARGB32_Premultiplied};
        image.fill(Qt::red);
        return image;
    }
} // namespace

/**
 * @brief 钉住点击与窗口切换的自动说明模板
 * @details 有元素名带控件类型，无名字回落坐标；控件类型为空按「控件」写；
 *          右键加前缀；窗口标题缺失时回落进程名，标题与进程都缺失时只写窗口切换。
 */
TEST(StepSession, BuildsStepDescriptions)
{
    EXPECT_EQ(clickStepDescription(QStringLiteral("保存"), QStringLiteral("按钮"), QPoint(10, 20), false), QStringLiteral("点击「保存」（按钮）"));
    EXPECT_EQ(clickStepDescription(QString(), QString(), QPoint(10, 20), false), QStringLiteral("点击（10, 20）"));
    EXPECT_TRUE(clickStepDescription(QStringLiteral("保存"), QStringLiteral("按钮"), QPoint(10, 20), true).startsWith(QStringLiteral("右键点击")));
    EXPECT_EQ(clickStepDescription(QStringLiteral("保存"), QString(), QPoint(10, 20), false), QStringLiteral("点击「保存」（控件）"));
    EXPECT_EQ(clickStepDescription(QStringLiteral("保存"), QStringLiteral("   "), QPoint(10, 20), false), QStringLiteral("点击「保存」（控件）"));
    EXPECT_EQ(windowChangeStepDescription(QStringLiteral("设置"), QString()), QStringLiteral("切换到「设置」"));
    EXPECT_EQ(windowChangeStepDescription(QString(), QStringLiteral("demo.exe")), QStringLiteral("切换窗口（demo.exe）"));
    EXPECT_EQ(windowChangeStepDescription(QString(), QString()), QStringLiteral("切换窗口"));
    EXPECT_EQ(manualStepDescription(), QStringLiteral("手动补充"));
}

/**
 * @brief 钉住 append 在说明为空时按类型补自动说明，已有说明原样保留
 */
TEST(StepSession, FillsAutomaticDescriptionOnAppend)
{
    StepSession session;

    ASSERT_TRUE(session.append(clickStep(QPoint(7, 8))).has_value());
    EXPECT_EQ(session.steps().front().description, QStringLiteral("点击「保存」（按钮）"));

    RecordedStep window = clickStep(QPoint(1, 1));
    window.kind         = StepKind::WindowChange;
    window.windowTitle  = QStringLiteral("设置");
    ASSERT_TRUE(session.append(window).has_value());
    EXPECT_EQ(session.steps().at(1).description, QStringLiteral("切换到「设置」"));

    RecordedStep manual;
    manual.kind = StepKind::Manual;
    manual.at   = QDateTime::currentDateTime();
    ASSERT_TRUE(session.append(manual).has_value());
    EXPECT_EQ(session.steps().at(2).description, QStringLiteral("手动补充"));

    RecordedStep custom = clickStep(QPoint(3, 4));
    custom.description  = QStringLiteral("自己写的步骤");
    ASSERT_TRUE(session.append(custom).has_value());
    EXPECT_EQ(session.steps().at(3).description, QStringLiteral("自己写的步骤"));
}

/**
 * @brief 追加自动编号；删除/上移/下移后序号重排，越界一律拒绝
 */
TEST(StepSession, AppendRemoveAndMoveReindex)
{
    QTemporaryDir dir;
    StepSession   session;
    ASSERT_TRUE(session.begin(std::filesystem::path{dir.path().toStdWString()}).has_value());
    for (auto index = 0; index < 3; ++index)
    {
        ASSERT_TRUE(session.append(clickStep(QPoint(index, index))).has_value());
    }
    EXPECT_EQ(session.count(), 3);
    EXPECT_EQ(session.steps()[0].index, 1);
    EXPECT_EQ(session.steps()[2].index, 3);
    ASSERT_TRUE(session.remove(1));
    EXPECT_EQ(session.count(), 2);
    EXPECT_EQ(session.steps()[0].index, 1);
    EXPECT_EQ(session.steps()[1].index, 2);
    ASSERT_TRUE(session.moveUp(1));
    EXPECT_EQ(session.steps()[0].globalLogicalPoint, QPoint(2, 2));
    EXPECT_EQ(session.steps()[0].index, 1) << "上移后序号跟着位置重排";
    EXPECT_EQ(session.steps()[1].index, 2);
    ASSERT_TRUE(session.moveDown(0));
    EXPECT_EQ(session.steps()[1].globalLogicalPoint, QPoint(2, 2));
    EXPECT_EQ(session.steps()[0].index, 1) << "下移后序号跟着位置重排";
    EXPECT_EQ(session.steps()[1].index, 2);

    EXPECT_FALSE(session.remove(2));
    EXPECT_FALSE(session.moveUp(0));
    EXPECT_FALSE(session.moveDown(1));
    EXPECT_EQ(session.count(), 2);
}

/**
 * @brief 1.5 秒内同元素/±4px 的重复点击合并；异元素、异窗口、超时都不合并
 */
TEST(StepSession, MergesRepeatedClicksOnTheSameElement)
{
    StepSession session;
    const auto  first = clickStep(QPoint(100, 100));
    ASSERT_TRUE(session.append(first).has_value());

    auto repeat = clickStep(QPoint(103, 101));
    repeat.at   = first.at.addMSecs(1000);
    EXPECT_TRUE(session.shouldMerge(session.steps().front(), repeat));

    auto other = clickStep(QPoint(200, 200), QStringLiteral("取消"));
    other.at   = first.at.addMSecs(200);
    EXPECT_FALSE(session.shouldMerge(session.steps().front(), other));

    auto onTheEdge = clickStep(QPoint(104, 100));
    onTheEdge.at   = first.at.addMSecs(kClickMergeWindowMs);
    EXPECT_TRUE(session.shouldMerge(session.steps().front(), onTheEdge));

    auto fivePixelsAway = clickStep(QPoint(105, 100));
    fivePixelsAway.at   = first.at.addMSecs(100);
    EXPECT_FALSE(session.shouldMerge(session.steps().front(), fivePixelsAway));

    auto tooLate = clickStep(QPoint(100, 100));
    tooLate.at   = first.at.addMSecs(kClickMergeWindowMs + 1);
    EXPECT_FALSE(session.shouldMerge(session.steps().front(), tooLate));

    auto early = clickStep(QPoint(100, 100));
    early.at   = first.at.addMSecs(-1);
    EXPECT_FALSE(session.shouldMerge(session.steps().front(), early));

    auto otherWindow        = clickStep(QPoint(100, 100));
    otherWindow.at          = first.at.addMSecs(100);
    otherWindow.windowTitle = QStringLiteral("别的窗口");
    EXPECT_FALSE(session.shouldMerge(session.steps().front(), otherWindow));

    auto windowChange = clickStep(QPoint(100, 100));
    windowChange.kind = StepKind::WindowChange;
    windowChange.at   = first.at.addMSecs(100);
    EXPECT_FALSE(session.shouldMerge(session.steps().front(), windowChange));

    auto diagonalEdge = clickStep(QPoint(102, 102));
    diagonalEdge.at   = first.at.addMSecs(100);
    EXPECT_TRUE(session.shouldMerge(session.steps().front(), diagonalEdge)) << "曼哈顿距离恰好 4 要合并";

    auto diagonalBeyond = clickStep(QPoint(102, 103));
    diagonalBeyond.at   = first.at.addMSecs(100);
    EXPECT_FALSE(session.shouldMerge(session.steps().front(), diagonalBeyond)) << "曼哈顿距离 5 不合并";

    const auto unnamedFirst  = clickStep(QPoint(10, 10), QString());
    auto       unnamedRepeat = clickStep(QPoint(12, 11), QString());
    unnamedRepeat.at         = unnamedFirst.at.addMSecs(300);
    EXPECT_TRUE(session.shouldMerge(unnamedFirst, unnamedRepeat)) << "两个空元素名也算同一元素";

    auto namedAgainstUnnamed = clickStep(QPoint(10, 10), QStringLiteral("保存"));
    namedAgainstUnnamed.at   = unnamedFirst.at.addMSecs(300);
    EXPECT_FALSE(session.shouldMerge(unnamedFirst, namedAgainstUnnamed)) << "空名与有名字不算同一元素";

    EXPECT_EQ(session.count(), 1) << "合并判定只读，不得改动已有步骤";
}

/**
 * @brief 说明截到 500 字符、折叠空白并允许清空；步骤上限 200
 */
TEST(StepSession, EnforcesDescriptionAndStepLimits)
{
    StepSession session;
    ASSERT_TRUE(session.append(clickStep(QPoint(1, 1))).has_value());
    ASSERT_TRUE(session.setDescription(0, QString(600, QChar(0x4E00))).has_value());
    EXPECT_EQ(session.steps().front().description.size(), kMaximumStepDescriptionLength);

    ASSERT_TRUE(session.setDescription(0, QStringLiteral("  多行\n\n说明\t  ")).has_value());
    EXPECT_EQ(session.steps().front().description, QStringLiteral("多行 说明"));

    ASSERT_TRUE(session.setDescription(0, QString()).has_value());
    EXPECT_TRUE(session.steps().front().description.isEmpty());

    ASSERT_FALSE(session.setDescription(9, QStringLiteral("越界")).has_value());

    for (auto index = 1; index < kMaximumSteps; ++index)
    {
        ASSERT_TRUE(session.append(clickStep(QPoint(index, index))).has_value());
    }
    EXPECT_EQ(session.count(), static_cast<std::size_t>(kMaximumSteps));
    const auto capped = session.append(clickStep(QPoint(999, 999)));
    ASSERT_FALSE(capped.has_value());
    EXPECT_EQ(capped.error().message, "已经记满 200 步，先导出或放弃这次记录") << "上限错误要填出步数";
    EXPECT_EQ(session.count(), static_cast<std::size_t>(kMaximumSteps));
}

/**
 * @brief 整图落 PNG，缩略图 320 宽；cleanup 删目录并清空步骤
 */
TEST(StepSession, StoresImagesAndCleansUp)
{
    QTemporaryDir dir;
    StepSession   session;
    ASSERT_TRUE(session.begin(std::filesystem::path{dir.path().toStdWString()}).has_value());
    ASSERT_TRUE(session.append(clickStep(QPoint(10, 10))).has_value());
    QImage image{800, 400, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::red);
    ASSERT_TRUE(session.attachImage(0, image).has_value());
    EXPECT_EQ(session.steps().front().thumbnail.width(), kStepThumbnailWidth);
    EXPECT_EQ(session.steps().front().thumbnail.height(), kStepThumbnailWidth / 2);
    EXPECT_TRUE(session.steps().front().imagePath.endsWith(QStringLiteral("step-001.png")));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path{session.steps().front().imagePath.toStdWString()}));
    EXPECT_TRUE(session.steps().front().captureError.isEmpty());

    session.cleanup();
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path{dir.path().toStdWString()} / L"step-001.png"));
    EXPECT_EQ(session.count(), 0);
    EXPECT_TRUE(session.tempDirectory().empty());
}

/**
 * @brief 删除步骤同时删掉它的整图；之后补的步骤不得覆盖别人的图片
 */
TEST(StepSession, RemovingStepDeletesImageWithoutReusingFileName)
{
    QTemporaryDir dir;
    StepSession   session;
    ASSERT_TRUE(session.begin(toPath(dir.path())).has_value());
    ASSERT_TRUE(session.append(clickStep(QPoint(1, 1))).has_value());
    ASSERT_TRUE(session.append(clickStep(QPoint(2, 2))).has_value());
    ASSERT_TRUE(session.attachImage(0, solidImage(400, 200)).has_value());
    ASSERT_TRUE(session.attachImage(1, solidImage(400, 200)).has_value());

    const std::filesystem::path firstImage = toPath(session.steps().front().imagePath);
    const QString               keptImage  = session.steps().at(1).imagePath;
    ASSERT_TRUE(std::filesystem::exists(firstImage));
    ASSERT_TRUE(session.remove(0));
    EXPECT_FALSE(std::filesystem::exists(firstImage));
    EXPECT_EQ(session.count(), 1);

    ASSERT_TRUE(session.append(clickStep(QPoint(3, 3))).has_value());
    ASSERT_TRUE(session.attachImage(1, solidImage(400, 200)).has_value());
    EXPECT_NE(session.steps().at(1).imagePath, keptImage) << "新步骤不能复用仍在用的文件名";
    EXPECT_TRUE(std::filesystem::exists(toPath(session.steps().at(0).imagePath)));
    EXPECT_TRUE(std::filesystem::exists(toPath(session.steps().at(1).imagePath)));
}

/**
 * @brief 没有临时目录、步骤下标越界、空图都拒绝落盘并留下原因
 */
TEST(StepSession, RejectsImagesWithoutDirectoryOrValidStep)
{
    StepSession session;
    ASSERT_TRUE(session.append(clickStep(QPoint(1, 1))).has_value());
    EXPECT_FALSE(session.attachImage(0, solidImage(100, 100)).has_value());
    EXPECT_FALSE(session.steps().front().captureError.isEmpty());

    QTemporaryDir dir;
    ASSERT_TRUE(session.begin(toPath(dir.path())).has_value());
    ASSERT_TRUE(session.append(clickStep(QPoint(1, 1))).has_value());
    const auto outOfRange = session.attachImage(5, solidImage(100, 100));
    ASSERT_FALSE(outOfRange.has_value());
    EXPECT_EQ(outOfRange.error().message, "步骤序号 6 越界，当前只有 1 步") << "越界错误要填出序号与步数";
    EXPECT_FALSE(session.attachImage(0, QImage{}).has_value());
    EXPECT_FALSE(session.steps().front().captureError.isEmpty());
}

/**
 * @brief 临时目录只删超过 24 小时、且是直接子目录的那些；恰好 24 小时仍在保留期内
 */
TEST(StepSession, CleanupOldSessionsRemovesOnlyExpiredDirectories)
{
    QTemporaryDir               temp;
    const std::filesystem::path root         = toPath(temp.path());
    const std::filesystem::path justOverDay  = root / L"just-over-a-day";
    const std::filesystem::path exactlyDay   = root / L"exactly-a-day";
    const std::filesystem::path freshSession = root / L"fresh";
    const std::filesystem::path siblingFile  = root / L"keep.txt";
    std::filesystem::create_directories(justOverDay);
    std::filesystem::create_directories(exactlyDay);
    std::filesystem::create_directories(freshSession);
    {
        std::ofstream file{siblingFile, std::ios::binary};
        file << "keep";
    }

    const QDateTime now       = QDateTime::fromMSecsSinceEpoch(1'800'000'000'000);
    const auto      nowSystem = std::chrono::system_clock::time_point{std::chrono::milliseconds{now.toMSecsSinceEpoch()}};
    const auto      nowFile   = std::chrono::clock_cast<std::chrono::file_clock>(nowSystem);
    std::filesystem::last_write_time(justOverDay, nowFile - std::chrono::hours(24) - std::chrono::seconds(1));
    std::filesystem::last_write_time(exactlyDay, nowFile - std::chrono::hours(24));
    std::filesystem::last_write_time(freshSession, nowFile);
    std::filesystem::last_write_time(siblingFile, nowFile - std::chrono::hours(25));

    StepSession::cleanupOldSessions(root, now);

    EXPECT_FALSE(std::filesystem::exists(justOverDay)) << "超过 24 小时的会话目录要删掉";
    EXPECT_TRUE(std::filesystem::exists(exactlyDay)) << "恰好 24 小时还在保留期内，不能删";
    EXPECT_TRUE(std::filesystem::exists(freshSession));
    EXPECT_TRUE(std::filesystem::exists(siblingFile)) << "root 下的文件不碰";
}

/**
 * @brief begin 建目录并清空步骤；目标位置是文件时给出带路径与系统原因的失败
 */
TEST(StepSession, BeginCreatesDirectoryAndRejectsAFilePath)
{
    QTemporaryDir temp;
    StepSession   session;
    ASSERT_TRUE(session.append(clickStep(QPoint(1, 1))).has_value());
    const std::filesystem::path target = toPath(temp.path()) / L"session";
    ASSERT_TRUE(session.begin(target).has_value());
    EXPECT_TRUE(std::filesystem::is_directory(target));
    EXPECT_EQ(session.tempDirectory(), target);
    EXPECT_EQ(session.count(), 0);

    const std::filesystem::path blocker = toPath(temp.path()) / L"blocker";
    {
        std::ofstream file{blocker, std::ios::binary};
        file << "not a directory";
    }
    StepSession blocked;
    const auto  result = blocked.begin(blocker);
    ASSERT_FALSE(result.has_value());
    const std::string pathText = QString::fromStdWString(blocker.wstring()).toStdString();
    EXPECT_NE(result.error().message.find(pathText), std::string::npos) << result.error().message;
    EXPECT_NE(result.error().message.find("无法创建步骤临时目录"), std::string::npos) << result.error().message;
    EXPECT_NE(result.error().message.find("请检查目录权限后重试"), std::string::npos) << result.error().message;
    EXPECT_EQ(result.error().message.find("（）"), std::string::npos) << "系统原因不能为空：" << result.error().message;
}

/**
 * @brief 建目录失败的 begin 不能动已有步骤与旧临时目录
 */
TEST(StepSession, FailedBeginKeepsPreviousSteps)
{
    QTemporaryDir               temp;
    StepSession                 session;
    const std::filesystem::path first = toPath(temp.path()) / L"first";
    ASSERT_TRUE(session.begin(first).has_value());
    ASSERT_TRUE(session.append(clickStep(QPoint(1, 1))).has_value());
    ASSERT_TRUE(session.append(clickStep(QPoint(2, 2))).has_value());

    const std::filesystem::path blocker = toPath(temp.path()) / L"blocker";
    {
        std::ofstream file{blocker, std::ios::binary};
        file << "not a directory";
    }
    ASSERT_FALSE(session.begin(blocker).has_value());
    EXPECT_EQ(session.count(), 2);
    EXPECT_EQ(session.tempDirectory(), first);
    EXPECT_EQ(session.steps().front().globalLogicalPoint, QPoint(1, 1));

    ASSERT_FALSE(session.begin({}).has_value()) << "空路径同样不能动已有步骤";
    EXPECT_EQ(session.count(), 2);
    EXPECT_EQ(session.tempDirectory(), first);
}

/**
 * @brief 重新贴图换了文件名时删掉旧图，避免旧 PNG 留成孤儿
 */
TEST(StepSession, ReattachingImageRemovesThePreviousFile)
{
    QTemporaryDir dir;
    StepSession   session;
    ASSERT_TRUE(session.begin(toPath(dir.path())).has_value());
    ASSERT_TRUE(session.append(clickStep(QPoint(1, 1))).has_value());
    ASSERT_TRUE(session.append(clickStep(QPoint(2, 2))).has_value());
    ASSERT_TRUE(session.attachImage(1, solidImage(400, 200)).has_value());

    const std::filesystem::path firstImage = toPath(session.steps().at(1).imagePath);
    ASSERT_EQ(firstImage.filename().wstring(), std::wstring{L"step-002.png"});
    ASSERT_TRUE(std::filesystem::exists(firstImage));

    ASSERT_TRUE(session.moveUp(1));
    ASSERT_TRUE(session.attachImage(0, solidImage(300, 100)).has_value());
    const std::filesystem::path secondImage = toPath(session.steps().front().imagePath);
    EXPECT_EQ(secondImage.filename().wstring(), std::wstring{L"step-001.png"});
    EXPECT_TRUE(std::filesystem::exists(secondImage));
    EXPECT_FALSE(std::filesystem::exists(firstImage)) << "重贴换了文件名，旧图不能留成孤儿";
    EXPECT_EQ(session.steps().front().thumbnail.width(), 300);
}

/**
 * @brief 整图已经不在了，remove 仍按成功移除步骤处理
 */
TEST(StepSession, RemovingStepWithMissingImageStillSucceeds)
{
    QTemporaryDir dir;
    StepSession   session;
    ASSERT_TRUE(session.begin(toPath(dir.path())).has_value());
    ASSERT_TRUE(session.append(clickStep(QPoint(1, 1))).has_value());
    ASSERT_TRUE(session.attachImage(0, solidImage(200, 100)).has_value());
    const std::filesystem::path imagePath = toPath(session.steps().front().imagePath);
    ASSERT_TRUE(std::filesystem::remove(imagePath));
    ASSERT_FALSE(std::filesystem::exists(imagePath));

    EXPECT_TRUE(session.remove(0)) << "整图已不在只算正常情况，步骤照常移除";
    EXPECT_EQ(session.count(), 0);
}
