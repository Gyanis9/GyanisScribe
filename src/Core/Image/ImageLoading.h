/**
 * @file ImageLoading.h
 * @brief 从盘上读回一张图
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QImage>

#include <filesystem>

namespace GyanisScribe::Core
{

    /**
     * @brief 从盘上读回一张图
     * @param filePath 图片路径
     * @return 图；读不出时返回写明原因的错误
     * @details 按 EXIF 方向摆正（setAutoTransform）：竖着拍的照片才不会躺倒进标注窗。
     *          读不出来时文案要写出路径与「可能没了/可能缺解码插件」两条出路——
     *          这条路被历史页的「重新识别」与贴图的历史转贴共用，静默给一张空图就等于
     *          让下游去猜「是没图还是读失败」。
     */
    [[nodiscard]] Base::Expected<QImage> loadImageFile(const std::filesystem::path &filePath);

} // namespace GyanisScribe::Core
