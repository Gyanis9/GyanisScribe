/**
 * @file StepHtmlBuilder.h
 * @brief 把步骤列表导出成单文件 HTML 教程
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Steps/StepTypes.h"

#include <QDateTime>
#include <QString>

#include <filesystem>
#include <vector>

namespace GyanisScribe::Core
{
    /// 内嵌前把整图最长边缩到的像素上限；小图保持原尺寸，不放大
    inline constexpr int kStepHtmlImageEdge = 1600;

    /// 内嵌 JPEG 的质量档
    inline constexpr int kStepHtmlJpegQuality = 85;

    /**
     * @brief 生成完整的单文件 HTML 教程
     * @param steps 按显示顺序排列的步骤；编号按位置从 1 起，不看 RecordedStep::index
     * @param generatedAt 生成时刻，写进标题区
     * @return 含内联样式与 data URI 图片的完整文档；steps 为空时返回空串
     * @details 说明、窗口标题与抓屏错误都按 HTML 文本转义；图片读不出来时显示占位；
     *          整图先缩到最长边不超过 1600，再按 JPEG 质量 85 编码成 base64 data URI。
     */
    [[nodiscard]] QString buildStepHtml(const std::vector<RecordedStep> &steps, const QDateTime &generatedAt);

    /**
     * @brief 把步骤教程写进 HTML 文件
     * @param filePath 目标文件，已存在则覆盖；父目录不存在时先创建
     * @param steps 按显示顺序排列的步骤
     * @param generatedAt 生成时刻，写进标题区
     * @return 成功返回空载荷；没有步骤时返回 InvalidArgument，写盘失败返回带路径的中文 Error
     * @details 写盘走 QSaveFile：先写临时文件再整体换名，中途失败不会留下半份教程。
     */
    [[nodiscard]] Base::Expected<void> writeStepHtml(const std::filesystem::path &filePath, const std::vector<RecordedStep> &steps, const QDateTime &generatedAt);

} // namespace GyanisScribe::Core
