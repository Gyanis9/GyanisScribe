// Windows 系统识别后端的直测：这台机器上有语言包就跑真识别，一个都没有就整批跳过。
// 判据不用「调用成功」就算过——必须读出画上那几个数字，否则后端把任何图都说成
// 「认出来是空」也照样绿。语料选数字：系统对手写体与艺术字会飘，对大字号数字不飘。

#include "Core/Text.h"
#include "Core/Types.h"
#include "Ocr/WinRTImageOcrEngine.h"

#include <QApplication>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QString>

#include <gtest/gtest.h>

#include <format>
#include <memory>
#include <string>

namespace
{
    /// QApplication 的形参是 `int &`，要留一个可改的实参；它在构造期读完，活到套件结束即可
    int                           argumentCount = 1;
    std::unique_ptr<QApplication> application;

    /// 一次性分配的 argv 缓冲：QApplication 只存指针不拷贝，放在函数局部会留下悬空引用
    inline static char argumentZero[] = "TestWinRTImageOcrEngine";
    char              *arguments[]    = {argumentZero, nullptr};

    /**
     * @brief 造一张黑字白底的图
     * @param text 画在左上区域的文本
     * @return 640x160 的 ARGB32 图
     */
    QImage makeTextImage(const QString &text)
    {
        QImage image(640, 160, QImage::Format_ARGB32);
        image.fill(Qt::white);

        QPainter painter(&image);
        painter.setPen(Qt::black);
        QFont font = painter.font();
        font.setPointSize(48);
        painter.setFont(font);
        painter.drawText(20, 100, text);
        return image;
    }

    /**
     * @brief 把框的四个边按「左/上/右/下」拼成可读文本，供断言失败时打印
     * @param box 要打印的框
     * @return 打印用串
     */
    std::string describeBox(const QRect &box)
    {
        return std::format("{}/{}/{}/{}", box.left(), box.top(), box.right(), box.bottom());
    }

    class WinRTImageOcrEngineFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            application = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        /**
         * @brief 这台机器能不能跑系统识别
         * @return true 表示后端至少有一个对外语言可用
         * @note 这里只回报能力，跳过动作由用例体自己做：助手函数里的 GTEST_SKIP 只会从助手返回，
         *       用例体会接着往下跑并在构造后端时炸出异常，日志里同一批既记 Skipped 又记 failure
         */
        [[nodiscard]] static bool canRunSystemOcr()
        {
            return GyanisScribe::Ocr::WinRTImageOcrEngine().isAvailable();
        }
    };

    TEST_F(WinRTImageOcrEngineFixture, ReadsRenderedDigitsAndKeepsBoxesInsideTheImage)
    {
        if (!canRunSystemOcr())
        {
            GTEST_SKIP() << "这台机器的 Windows 没装本程序认识的识别语言包";
        }

        GyanisScribe::Ocr::WinRTImageOcrEngine engine;
        const auto                             language = engine.supportedLanguages().front();
        const auto                             image    = makeTextImage(QStringLiteral("12345"));

        const auto recognized = engine.recognize(image, language);
        ASSERT_TRUE(recognized.has_value()) << recognized.error().message;

        EXPECT_EQ(recognized->language, language) << "结果里报的语言与请求的不是一个";
        ASSERT_FALSE(recognized->lines.isEmpty()) << "一行都没认出来";

        // 去掉所有空白再比：系统可能把一串数字拆成几个词，拼回去才是用户复制走的那句
        QString joined;
        for (const auto &line: recognized->lines)
        {
            joined += line.text();
        }
        joined.remove(QRegularExpression(QStringLiteral("\\s")));
        EXPECT_NE(joined.indexOf(QStringLiteral("12345")), -1) << "读回来的是：" << joined.toStdString();

        for (const auto &line: recognized->lines)
        {
            EXPECT_TRUE(image.rect().contains(line.boundingBox)) << "整行的框跑到了图外：" << describeBox(line.boundingBox);
            ASSERT_FALSE(line.words.isEmpty());
            for (const auto &word: line.words)
            {
                // 方框要与传入的那张图同系：界面上「点文字定位」按这套坐标回指
                EXPECT_TRUE(image.rect().contains(word.boundingBox)) << "词框跑出去了：" << describeBox(word.boundingBox);
                EXPECT_FALSE(word.text.isEmpty()) << "框里有词却没文本";
            }
        }

        // 系统后端不给逐词置信度，这一格必须是「没有读数」而不是 0：0 会被界面打成一个 0% 的假读数
        EXPECT_FALSE(recognized->averageConfidence.has_value()) << "置信度该留空，留 0 等于把「不报这个数」报成「认得很差」";
    }

    TEST_F(WinRTImageOcrEngineFixture, RejectsALanguageWithoutAPackAndNamesWhatWorks)
    {
        if (!canRunSystemOcr())
        {
            GTEST_SKIP() << "这台机器的 Windows 没装本程序认识的识别语言包";
        }

        GyanisScribe::Ocr::WinRTImageOcrEngine engine;
        const auto                             recognized = engine.recognize(makeTextImage(QStringLiteral("12345")), QStringLiteral("fr"));
        ASSERT_FALSE(recognized.has_value()) << "系统里没有 fr 的语言包，却说认出来了";
        EXPECT_EQ(recognized.error().code, GyanisScribe::Core::ErrorCode::DependencyMissing);

        // 原因要点名现在能用的是哪几个，否则用户还得自己去设置页一个个试
        const auto reason = QString::fromStdString(recognized.error().message);
        for (const auto &language: engine.supportedLanguages())
        {
            EXPECT_NE(reason.indexOf(language), -1) << "原因里没列出现有语言 " << language.toStdString() << "：实际是 " << reason.toStdString();
        }
    }

    TEST_F(WinRTImageOcrEngineFixture, RejectsAnImageTooSmallToRead)
    {
        if (!canRunSystemOcr())
        {
            GTEST_SKIP() << "这台机器的 Windows 没装本程序认识的识别语言包";
        }

        GyanisScribe::Ocr::WinRTImageOcrEngine engine;
        const QImage                           tiny(4, 4, QImage::Format_ARGB32);
        const auto                             rejected = engine.recognize(tiny, engine.supportedLanguages().front());
        ASSERT_FALSE(rejected.has_value()) << "比门槛还小的图被送去系统认了";
        EXPECT_EQ(rejected.error().code, GyanisScribe::Core::ErrorCode::InvalidArgument);
        // 两个后端的这一道门槛与文案要一致，界面才能只写一处提示
        EXPECT_NE(QString::fromStdString(rejected.error().message).indexOf(QStringLiteral("8")), -1) << rejected.error().message;
    }

    TEST_F(WinRTImageOcrEngineFixture, NameIsTheCataloguedUserFacingLabel)
    {
        GyanisScribe::Ocr::WinRTImageOcrEngine engine;
        // 名字进过文案目录：英文档下它必须能翻，设置页与结果标题用的就是这一句
        EXPECT_EQ(engine.name(), QString::fromUtf8(GyanisScribe::Core::uiTextUtf8("Windows 系统识别").c_str()));
    }
} // namespace
