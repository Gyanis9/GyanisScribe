// PP-OCRv6 ONNX 后端的直测：真模型、真运行库，一个用例一个进程。
// 判据不用「调用成功」就算过——第一格必须读出画上去的中英数字，否则后端把任何图都说成
// 「认出来是空」也照样绿。运行库路径由 ctest 的 ENVIRONMENT 指到随包那一份。

#include "Base/Types.h"
#include "Plugins/OcrEngines/PpOcrOnnxEngine.h"

#include <QApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using GyanisScribe::Base::ErrorCode;
    using GyanisScribe::Plugins::kPpOcrDetModelFileName;
    using GyanisScribe::Plugins::kPpOcrDictFileName;
    using GyanisScribe::Plugins::kPpOcrRecModelFileName;
    using GyanisScribe::Plugins::PpOcrOnnxEngine;

    /// 随仓库那一份模型目录，路径由构建期给
    const std::filesystem::path kVendoredModels{ GYANISSCRIBE_VENDORED_PPOCR };

    /// QApplication 的形参是 `int &`，要留一个可改的实参；它在构造期读完，活到套件结束即可
    int                           argumentCount = 1;
    std::unique_ptr<QApplication> application;

    /// 一次性分配的 argv 缓冲：QApplication 只存指针不拷贝，放在函数局部会留下悬空引用
    inline static char argumentZero[] = "TestPpOcrOnnxEngine";
    char              *arguments[]    = {argumentZero, nullptr};

    /**
     * @brief 造一张黑字白底、两行文本的图
     * @return 800x260 的 ARGB32 图，第一行中英混排，第二行英文
     */
    QImage makeTextImage()
    {
        QImage image(800, 260, QImage::Format_ARGB32);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            painter.setPen(Qt::black);
            painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 40));
            painter.drawText(20, 90, QStringLiteral("识别测试 2026"));
            painter.drawText(20, 200, QStringLiteral("PaddleOCR"));
        }
        return image;
    }

    /**
     * @brief 往磁盘上写一份内容任意的文件
     * @param path 目标路径
     * @param content 文件内容
     * @return true 表示写完且流状态正常
     */
    bool writeFile(const std::filesystem::path &path, const std::string &content)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        return stream.good();
    }

    /**
     * @brief 在目录里放三个内容相同的假模型文件
     * @param directory 目标目录
     * @return true 表示三份都写成功
     * @note 名字取引擎导出的常量：用例与实现各写一份文件名，改一边就会分家
     */
    bool writeFakeModelFiles(const std::filesystem::path &directory)
    {
        const std::string junk = "this is not an onnx model";
        return writeFile(directory / kPpOcrDetModelFileName, junk) && writeFile(directory / kPpOcrRecModelFileName, junk) &&
               writeFile(directory / kPpOcrDictFileName, junk);
    }

    class PpOcrOnnxEngineFixture : public ::testing::Test
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
    };

    TEST_F(PpOcrOnnxEngineFixture, ReadsRenderedChineseEnglishAndDigits)
    {
        PpOcrOnnxEngine engine(kVendoredModels);
        ASSERT_TRUE(engine.isAvailable()) << engine.unavailableReason().toStdString();

        const QImage image  = makeTextImage();
        const auto   result = engine.recognize(image, QStringLiteral("zh-Hans"));
        ASSERT_TRUE(result.has_value()) << result.error().message;

        EXPECT_EQ(result->language, QStringLiteral("zh-Hans"));
        const auto text = result->text();
        EXPECT_NE(text.indexOf(QStringLiteral("识别测试")), -1) << text.toStdString();
        EXPECT_NE(text.indexOf(QStringLiteral("2026")), -1) << text.toStdString();
        EXPECT_NE(text.indexOf(QStringLiteral("PaddleOCR")), -1) << text.toStdString();

        ASSERT_FALSE(result->lines.isEmpty());
        ASSERT_TRUE(result->averageConfidence.has_value());
        EXPECT_GT(*result->averageConfidence, 0.8);

        for (const auto &line: result->lines)
        {
            // 方框要与传入的那张图同系：界面上「点文字定位」按这套坐标回指
            EXPECT_TRUE(QRect(QPoint(0, 0), QSize(800, 260)).contains(line.boundingBox)) << line.boundingBox.x() << "/" << line.boundingBox.y();
            ASSERT_FALSE(line.words.isEmpty());
            for (const auto &word: line.words)
            {
                EXPECT_TRUE(QRect(QPoint(0, 0), QSize(800, 260)).contains(word.boundingBox));
                EXPECT_FALSE(word.text.isEmpty());
            }
        }
    }

    TEST_F(PpOcrOnnxEngineFixture, ReturnsEmptyForBlankImage)
    {
        PpOcrOnnxEngine engine(kVendoredModels);
        ASSERT_TRUE(engine.isAvailable()) << engine.unavailableReason().toStdString();

        QImage blank(640, 200, QImage::Format_ARGB32);
        blank.fill(Qt::white);

        const auto result = engine.recognize(blank, QStringLiteral("zh-Hans"));
        ASSERT_TRUE(result.has_value()) << result.error().message;
        EXPECT_TRUE(result->isEmpty());
    }

    TEST_F(PpOcrOnnxEngineFixture, ReportsMissingModelFilesWithActionableReason)
    {
        QTemporaryDir empty;
        ASSERT_TRUE(empty.isValid());

        PpOcrOnnxEngine engine(std::filesystem::path(empty.path().toStdWString()));
        EXPECT_FALSE(engine.isAvailable());
        EXPECT_TRUE(engine.supportedLanguages().isEmpty());

        const auto reason = engine.unavailableReason();
        for (const char *fileName: {kPpOcrDetModelFileName, kPpOcrRecModelFileName, kPpOcrDictFileName})
        {
            EXPECT_NE(reason.indexOf(QString::fromLatin1(fileName)), -1) << "原因里没点名 " << fileName << "：" << reason.toStdString();
        }
    }

    TEST_F(PpOcrOnnxEngineFixture, RejectsCorruptModelAsInternalError)
    {
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        const std::filesystem::path modelDirectory(directory.path().toStdWString());
        ASSERT_TRUE(writeFakeModelFiles(modelDirectory));

        PpOcrOnnxEngine engine(modelDirectory);
        ASSERT_TRUE(engine.isAvailable()) << engine.unavailableReason().toStdString();

        const auto result = engine.recognize(makeTextImage(), QStringLiteral("zh-Hans"));
        ASSERT_FALSE(result.has_value()) << "坏模型被当成一次成功的空识别";
        EXPECT_EQ(result.error().code, ErrorCode::InternalError);
        EXPECT_NE(QString::fromStdString(result.error().message).indexOf(QString::fromLatin1(kPpOcrDetModelFileName)), -1) << result.error().message;
    }

    TEST_F(PpOcrOnnxEngineFixture, ReportsMissingRuntimeLibrary)
    {
        const bool     hadPrevious = qEnvironmentVariableIsSet("GYANISSCRIBE_ONNXRUNTIME_DLL");
        const QByteArray previous  = qgetenv("GYANISSCRIBE_ONNXRUNTIME_DLL");
        qputenv("GYANISSCRIBE_ONNXRUNTIME_DLL", QByteArrayLiteral("Z:/no/such/directory/onnxruntime.dll"));

        {
            PpOcrOnnxEngine engine(kVendoredModels);
            EXPECT_FALSE(engine.isAvailable());
            const auto reason = engine.unavailableReason();
            EXPECT_NE(reason.indexOf(QStringLiteral("onnxruntime.dll")), -1) << reason.toStdString();
        }

        if (hadPrevious)
        {
            qputenv("GYANISSCRIBE_ONNXRUNTIME_DLL", previous);
        }
        else
        {
            qunsetenv("GYANISSCRIBE_ONNXRUNTIME_DLL");
        }
    }

    TEST_F(PpOcrOnnxEngineFixture, HandlesWideLineWithoutCrashing)
    {
        PpOcrOnnxEngine engine(kVendoredModels);
        ASSERT_TRUE(engine.isAvailable()) << engine.unavailableReason().toStdString();

        QImage image(4000, 80, QImage::Format_ARGB32);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            painter.setPen(Qt::black);
            painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 96));
            painter.drawText(10, 70, QStringLiteral("12345 67890"));
        }

        const auto result = engine.recognize(image, QStringLiteral("zh-Hans"));
        ASSERT_TRUE(result.has_value()) << result.error().message;
        for (const auto &line: result->lines)
        {
            EXPECT_TRUE(image.rect().contains(line.boundingBox));
        }
    }

    TEST_F(PpOcrOnnxEngineFixture, HandlesLightTextOnDarkBackground)
    {
        PpOcrOnnxEngine engine(kVendoredModels);
        ASSERT_TRUE(engine.isAvailable()) << engine.unavailableReason().toStdString();

        QImage image(800, 200, QImage::Format_ARGB32);
        image.fill(Qt::black);
        {
            QPainter painter(&image);
            painter.setPen(Qt::white);
            painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 40));
            painter.drawText(20, 140, QStringLiteral("1234"));
        }

        const auto result = engine.recognize(image, QStringLiteral("zh-Hans"));
        ASSERT_TRUE(result.has_value()) << result.error().message;
        for (const auto &line: result->lines)
        {
            EXPECT_TRUE(image.rect().contains(line.boundingBox));
        }
    }

    /**
     * @brief 读数用例：一张 4K 图识别一次要多少毫秒（不断言时间）
     * @details 真实截图常是 3840×2160，而识别的代价随像素走；跑在什么机器上就报什么数。
     *          字号跟着图放大：4K 上还用 40 磅那行字小得认不出，量到的就只是「没认出」的耗时。
     */
    TEST_F(PpOcrOnnxEngineFixture, ReportsMedianCostOfA4kImage)
    {
        PpOcrOnnxEngine engine(kVendoredModels);
        ASSERT_TRUE(engine.isAvailable()) << engine.unavailableReason().toStdString();

        QImage image(3840, 2160, QImage::Format_ARGB32);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            painter.setPen(Qt::black);
            painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 96));
            painter.drawText(80, 320, QStringLiteral("12345"));
        }

        constexpr int       kRounds = 3;
        std::vector<double> milliseconds;
        for (int round = 0; round < kRounds; ++round)
        {
            QElapsedTimer watch;
            watch.start();
            const auto recognized = engine.recognize(image, QStringLiteral("zh-Hans"));
            milliseconds.push_back(static_cast<double>(watch.nsecsElapsed()) / 1'000'000.0);
            ASSERT_TRUE(recognized.has_value()) << recognized.error().message;
        }

        std::ranges::sort(milliseconds);
        std::printf("[读数] 4K（3840×2160）图识别一次：中位 %.1fms（%d 轮，min %.1f / max %.1f）\n", milliseconds[static_cast<std::size_t>(kRounds) / 2U], kRounds,
                    milliseconds.front(), milliseconds.back());
    }
} // namespace
