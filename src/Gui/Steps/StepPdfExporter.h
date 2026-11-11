/**
 * @file StepPdfExporter.h
 * @brief 把步骤列表导出成 PDF 教程
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Steps/StepTypes.h"

#include <QDateTime>

#include <filesystem>
#include <vector>

namespace GyanisScribe::Gui
{
    /**
     * @brief 把步骤教程写进 PDF 文件
     * @param filePath 目标文件，已存在则覆盖；父目录不存在时先创建
     * @param steps 按显示顺序排列的步骤；编号按位置从 1 起，不看 RecordedStep::index
     * @param generatedAt 生成时刻，写进标题区
     * @return 成功返回空载荷；没有步骤或路径为空时返回 InvalidArgument，
     *         建目录或落盘失败时返回带路径的中文 Error
     * @details 走 QTextDocument 加 QPrinter 的 A4 高分辨率档，步骤之间留间距、按可用高度流式分页，
     *          不强行每步一页。截图按最终显示序号现画点击标记后以 ImageResource 注册进文档，
     *          说明、窗口标题与抓屏错误都按 HTML 文本转义；图片读不出来时显示占位。
     *          写盘结束后核对文件存在且非空，失败时删掉半份文件，不在盘上留下坏 PDF。
     */
    [[nodiscard]] Base::Expected<void> writeStepPdf(const std::filesystem::path &filePath, const std::vector<Core::RecordedStep> &steps, const QDateTime &generatedAt);

} // namespace GyanisScribe::Gui
