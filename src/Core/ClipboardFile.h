/**
 * @file ClipboardFile.h
 * @brief 把图当作「文件」放进剪贴板：粘进资源管理器/聊天窗口得到一份 .png
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QImage>

#include <filesystem>

namespace GyanisScribe::Core
{

    /**
     * @brief 把一张图落成 PNG 文件，并按「文件」放进剪贴板
     * @param image 要交出去的图，空图会被拒（没有内容可复制）
     * @param directory 落地目录，不存在时创建
     * @return 落地文件的路径；空图或写不下去时返回原因
     * @details 文件名主干取内容的 SHA-256 前 16 位十六进制：同一张图重复复制复用同一个文件，
     *          不同的图不会互相覆盖，也不必维护「第几档文件名」这种状态。
     *          剪贴板里放的是这条本地路径（Windows 上就是 CF_HDROP）——「复制图像」与
     *          「复制文件」是两条出口：前者粘进文档里是一张图，后者粘进资源管理器/聊天窗口
     *          落一份 .png 文件。
     */
    [[nodiscard]] Expected<std::filesystem::path> copyImageAsFile(const QImage &image, const std::filesystem::path &directory);

} // namespace GyanisScribe::Core
