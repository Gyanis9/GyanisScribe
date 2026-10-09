/**
 * @file AppResources.h
 * @brief 应用资源（图标与样式表）的注册入口
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

namespace GyanisScribe::Ui
{

    /**
     * @brief 把 AppResources.qrc 里的资源注册进当前进程
     * @details AUTORCC 生成的那份初始化代码单独躺在一个目标文件里，没有任何符号引用它时链接器会整份丢掉，
     *          于是「仓库里有 SVG 与 QSS，运行时 :/icons 与 :/styles 却是空的」。
     *          摘掉本函数后重编整树，图标用例四枚当场变红——这条是实测，不是推测。
     * @note 幂等：内部只初始化一次，图标与样式表两条读取路径都调它，不需要调用方约定谁先。
     */
    void ensureApplicationResourcesRegistered();

} // namespace GyanisScribe::Ui
