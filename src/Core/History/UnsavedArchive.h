/**
 * @file UnsavedArchive.h
 * @brief 没存过文件的那张截图：像素的一份备份，让它也能进截图历史
 * @author Gyanis
 * @date 2026-10-13
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QDateTime>
#include <QImage>

#include <cstddef>
#include <filesystem>

namespace GyanisScribe::Core
{

    /// 这一份最多留几张：存的是像素而不是记录，上限要比历史清单的条数收得更紧
    inline constexpr std::size_t kUnsavedArchiveCapacity = 50;

    /**
     * @brief 把一张截图写进指定目录，并裁掉超出上限的旧份
     * @param image 截图像素；空图直接报错，不写一张 0×0 的文件
     * @param moment 截图时刻，决定文件名
     * @param directory 落盘目录，不存在时创建；调用方给 Base::Paths::unsavedCaptureDirectory()
     * @param capacity 目录里最多留几张，超出按写入时刻删最旧的；调用方给 kUnsavedArchiveCapacity
     * @return 写出去的文件路径；目录建不出、图写不下或像素是空的，返回可操作的中文错误
     * @details 目录与上限由调用方交出而不是写死在函数里：ctest 并行时每个用例是一个独立进程，
     *          共用真实数据目录会让两条用例互相裁掉对方的文件。
     *          永远写 PNG：这一份是给「误触 Esc 也还在」兜底的，有损档会把用户没存过的那张
     *          压出二次损伤，而透明通道丢了就找不回来。
     */
    [[nodiscard]] Base::Expected<std::filesystem::path> archiveUnsavedCapture(const QImage &image, const QDateTime &moment, const std::filesystem::path &directory,
                                                                              std::size_t capacity);

} // namespace GyanisScribe::Core
