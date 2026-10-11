/**
 * @file ClipboardPin.h
 * @brief 剪贴板载荷转成可贴的那张图
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QImage>

class QMimeData;

namespace GyanisScribe::App
{

    /**
     * @brief 从剪贴板载荷里取出可贴的那张图
     * @param data 剪贴板的 mime 载荷，可为 nullptr（无人拷贝过时有的平台就返回空指针）
     * @return 成功返回图像（物理像素）；没有图像或读不出像素时返回带中文原因的错误
     * @details 只认图像那一路：纯文本与颜色信息要另做一层渲染才算得上一张贴图，不在这一条里冒充。
     *          两条拒因都写清「怎么弄一张图进来」——入口在托盘上，附近没有任何输入框可提示。
     */
    [[nodiscard]] Core::Expected<QImage> pinnableImageFrom(const QMimeData *data);

} // namespace GyanisScribe::App
