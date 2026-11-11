// PDF 导出的三件事：空步骤直接拒绝、有截图时图片真的进了 PDF、写不出去时不留半份文件。

#include "Gui/Steps/StepPdfExporter.h"

#include "Base/Types.h"
#include "Core/Steps/StepTypes.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Core::RecordedStep;
using GyanisScribe::Core::StepKind;
using GyanisScribe::Gui::writeStepPdf;

namespace
{
    /// 临时目录里的一个文件路径，文件本身尚未创建
    std::filesystem::path tempPath(const QTemporaryDir &directory, const wchar_t *name)
    {
        return std::filesystem::path{directory.path().toStdWString()} / name;
    }

    /// PDF 里页面对象的个数：每页一个 /Type /Page，页树节点另有 /Type /Pages
    std::size_t pageObjectCount(const std::string &pdfBytes)
    {
        const auto countOccurrences = [&pdfBytes](const std::string_view token)
        {
            std::size_t count = 0;
            for (auto position = pdfBytes.find(token); position != std::string::npos; position = pdfBytes.find(token, position + token.size()))
            {
                ++count;
            }
            return count;
        };
        return countOccurrences("/Type /Page") - countOccurrences("/Type /Pages");
    }

    /// 造一张噪点图：纯色块会被压缩到极小，比较文件大小时看不出图片有没有进去
    QImage noisyImage(const int width, const int height)
    {
        QImage image{width, height, QImage::Format_ARGB32_Premultiplied};
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                image.setPixelColor(x, y, QColor{(x * 37 + y * 11) % 256, (x * 17 + y * 29) % 256, (x * 7 + y * 53) % 256});
            }
        }
        return image;
    }
} // namespace

/// 一步中文说明也要生成真实文件，不能只返回成功
TEST(StepPdfExporter, WritesANonEmptyPdf)
{
    QTemporaryDir                    dir;
    GyanisScribe::Core::RecordedStep step;
    step.kind          = GyanisScribe::Core::StepKind::Manual;
    step.description   = QStringLiteral("唯一一步");
    const auto path    = tempPath(dir, L"tutorial.pdf");
    const auto written = writeStepPdf(path, {step}, QDateTime::currentDateTime());
    ASSERT_TRUE(written.has_value()) << written.error().message;
    ASSERT_TRUE(std::filesystem::exists(path));
    EXPECT_GT(std::filesystem::file_size(path), 2048u);
}

/// 没有步骤时不写文件，返回带中文原因的 InvalidArgument
TEST(StepPdfExporter, RejectsEmptySteps)
{
    QTemporaryDir dir;
    const auto    path    = tempPath(dir, L"empty.pdf");
    const auto    written = writeStepPdf(path, {}, QDateTime::currentDateTime());
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(written.error().message.empty());
    EXPECT_FALSE(std::filesystem::exists(path));
}

/// 截图经 ImageResource 注册进文档后，PDF 应该比同一步没有图时更大
TEST(StepPdfExporter, EmbedsTheScreenshot)
{
    QTemporaryDir dir;
    RecordedStep  textOnly;
    textOnly.kind        = StepKind::Manual;
    textOnly.description = QStringLiteral("只有说明");

    const auto textPath = tempPath(dir, L"text-only.pdf");
    ASSERT_TRUE(writeStepPdf(textPath, {textOnly}, QDateTime::currentDateTime()).has_value());
    const auto textOnlySize = std::filesystem::file_size(textPath);

    const auto imagePath = tempPath(dir, L"shot.png");
    ASSERT_TRUE(noisyImage(320, 180).save(QString::fromStdWString(imagePath.wstring()), "PNG"));

    RecordedStep withImage  = textOnly;
    withImage.imagePath     = QString::fromStdWString(imagePath.wstring());
    const auto imagePdfPath = tempPath(dir, L"with-image.pdf");
    ASSERT_TRUE(writeStepPdf(imagePdfPath, {withImage}, QDateTime::currentDateTime()).has_value());
    EXPECT_GT(std::filesystem::file_size(imagePdfPath), textOnlySize);

    // 文件变大也可能只是多了一页文字；直接找 PDF 里的图片对象，确认截图真的嵌进去了
    std::ifstream     pdfStream{imagePdfPath, std::ios::binary};
    const std::string pdfBytes{std::istreambuf_iterator<char>{pdfStream}, std::istreambuf_iterator<char>{}};
    EXPECT_NE(pdfBytes.find("/Subtype /Image"), std::string::npos);
}

/// 三步短文字按流式排版留在同一页，分页由可用高度决定，不是每步强开一页
TEST(StepPdfExporter, FlowsStepsWithoutForcedPageBreak)
{
    QTemporaryDir             dir;
    std::vector<RecordedStep> steps;
    for (const auto *text: {L"第一步", L"第二步", L"第三步"})
    {
        RecordedStep step;
        step.kind        = StepKind::Manual;
        step.description = QString::fromWCharArray(text);
        steps.push_back(step);
    }

    const auto path    = tempPath(dir, L"flow.pdf");
    const auto written = writeStepPdf(path, steps, QDateTime::currentDateTime());
    ASSERT_TRUE(written.has_value()) << written.error().message;

    std::ifstream     pdfStream{path, std::ios::binary};
    const std::string pdfBytes{std::istreambuf_iterator<char>{pdfStream}, std::istreambuf_iterator<char>{}};
    ASSERT_FALSE(pdfBytes.empty());
    EXPECT_EQ(pageObjectCount(pdfBytes), 1u) << "三步短文字被拆成了多页";
}

/// 输出目录建不出来时返回错误，并且目标位置不能留下半份文件
TEST(StepPdfExporter, ReportsWriteFailureWithoutLeavingAFile)
{
    QTemporaryDir dir;
    const auto    blocker = tempPath(dir, L"blocker");
    {
        std::ofstream stream{blocker};
        stream << "not a directory";
    }

    RecordedStep step;
    step.kind        = StepKind::Manual;
    step.description = QStringLiteral("写不出去的一步");

    const auto path    = blocker / L"tutorial.pdf";
    const auto written = writeStepPdf(path, {step}, QDateTime::currentDateTime());
    ASSERT_FALSE(written.has_value());
    EXPECT_FALSE(written.error().message.empty());
    EXPECT_FALSE(std::filesystem::exists(path));
}
