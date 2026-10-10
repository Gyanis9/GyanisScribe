// 这一档链到 Tesseract 时才编（容器两档与本机 Release 都有；本机 Debug 没有这份后端）。
// 钉的是「拿真语言包真的能认出字」这一整条路：此前这里只用空文件冒充过语言包，
// 于是 TessBaseAPISetImage 的形参写反（把图高当成每像素字节数）在现场一句「识别失败」里藏了很久。
#include "Ocr/OcrPreprocessor.h"
#include "Ocr/TesseractOcrEngine.h"

#include <QApplication>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QString>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using GyanisScribe::Ocr::OcrResult;
using GyanisScribe::Ocr::TesseractOcrEngine;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 随仓库那份语言包（Vendor/Tessdata）：安装包就带它，用例不许依赖用户往哪放文件
    const std::filesystem::path kTessDataDirectory{GYANISSCRIBE_VENDORED_TESSDATA};

    /**
     * @brief 画一张要识别的图
     * @param text 文本
     * @param pointSize 字号（点），刻意用小的那一档时才能看出预处理有没有真的放大
     * @return 白底黑字的图
     */
    [[nodiscard]] QImage renderText(const QString &text, int pointSize)
    {
        QImage image{QSize{420, 64}, QImage::Format_ARGB32_Premultiplied};
        image.fill(Qt::white);
        QPainter painter{&image};
        QFont    font = painter.font();
        font.setPointSize(pointSize);
        painter.setFont(font);
        painter.setPen(Qt::black);
        painter.drawText(image.rect(), Qt::AlignVCenter | Qt::AlignLeft, text);
        painter.end();
        return image;
    }

    class TesseractEngineFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestTesseractOcrEngine";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 语言包是随仓库带的那一份：缺了就是打包链路的缺陷，该红不该跳
            ASSERT_TRUE(std::filesystem::exists(kTessDataDirectory / "eng.traineddata")) << "随仓库的语言包不在预期的位置：" << kTessDataDirectory.string();
            m_engine = std::make_unique<TesseractOcrEngine>(kTessDataDirectory);
            ASSERT_TRUE(m_engine->isAvailable()) << "语言包在位却建不出可用后端：" << m_engine->unavailableReason().toStdString();
        }

        void TearDown() override
        {
            m_engine.reset();
        }

        /// 认一张渲染出来的图；失败直接把原因放进断言里，免得只看到一个空结果
        [[nodiscard]] OcrResult recognizeOrFail(const QImage &image)
        {
            const auto recognized = m_engine->recognize(image, QStringLiteral("en"));
            EXPECT_TRUE(recognized.has_value()) << "真语言包下识别失败：" << recognized.error().message;
            if (!recognized)
            {
                return {};
            }
            return *recognized;
        }

        std::unique_ptr<TesseractOcrEngine> m_engine;
    };
} // namespace

/**
 * @brief 钉住「链到后端 + 随包语言包」这条路真的能认字
 * @details 此前这条通道只用空文件冒充过语言包，断的是「初始化语言失败」，
 *          于是图根本没喂进 Tesseract（SetImage 的形参写反）也没人报出来。
 */
TEST_F(TesseractEngineFixture, RecognizesRenderedTextWithTheBundledLanguagePack)
{
    const auto result = recognizeOrFail(renderText(QStringLiteral("GyanisScribe 2026 report"), 14));

    ASSERT_FALSE(result.isEmpty()) << "一张 14pt 的干净渲染图一个字都没认出来";
    EXPECT_NE(result.text().indexOf(QStringLiteral("2026")), -1) << "实际认回：" << result.text().toStdString();
    EXPECT_NE(result.text().indexOf(QStringLiteral("report")), -1) << "实际认回：" << result.text().toStdString();
    EXPECT_GT(result.averageConfidence, 0.5) << "平均置信度低到不像认对了：" << result.averageConfidence;
}
