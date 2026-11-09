/**
 * @file DuplicateHash.h
 * @brief 重复截图的感知哈希与结果分组
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Memory/MemoryTypes.h"

#include <QImage>
#include <QString>

#include <vector>

namespace GyanisScribe::Core
{
    /**
     * @brief 计算 64 位差值哈希
     * @param image 待比对的图像，可为空
     * @return 16 位小写十六进制；空图或缩放失败时返回空串
     */
    [[nodiscard]] QString computeDHash(const QImage &image);

    /**
     * @brief 计算两个 dHash 的 Hamming 距离
     * @param left 左侧哈希
     * @param right 右侧哈希
     * @return 0 到 64 的距离；任一为空、长度不是 16 或含非十六进制字符时返回 64
     */
    [[nodiscard]] int hammingDistance(const QString &left, const QString &right);

    /**
     * @brief 按 dHash 相似度把搜索结果分组
     * @param results 当前结果页，保持界面原有顺序
     * @param threshold 合并阈值，距离不超过它即并入同组
     * @return 组内按时间倒序，组间按组内最新条目的原顺序；dhash 为空的条目自成一组
     */
    [[nodiscard]] std::vector<std::vector<MemoryResult>> groupDuplicates(std::vector<MemoryResult> results, int threshold = kDuplicateHammingThreshold);
} // namespace GyanisScribe::Core
