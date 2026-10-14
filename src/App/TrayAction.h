/**
 * @file TrayAction.h
 * @brief 托盘这一族动作的词表：菜单上的一项、一条全局热键、一下鼠标点击都落在这张表上
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

namespace GyanisScribe::App
{

    /**
     * @brief 托盘菜单的一项动作
     * @details 用例按这张表核对「菜单里有几项、各指向哪儿」，不依赖真的托盘图标。
     *          这一张表单独成头：托盘控制器要知道「哪一下鼠标算一档」，而纯函数层（菜单表、热键清册、
     *          鼠标动作）都要用这个枚举，谁也不该为了拿一个枚举去 include 一个带窗口的类。
     */
    enum class TrayAction
    {
        CaptureRegion,     ///< 框选截图
        CaptureFullScreen, ///< 整屏截图
        CaptureLastRegion, ///< 重复上一次选区
        CaptureWindow,     ///< 截取当前前台窗口，没有该能力的平台不在菜单里放这一项
        ToggleRecord,      ///< 开始或停止录屏
        Recognize,         ///< 框选后识别文字
        PickColor,         ///< 屏幕取色
        OpenSettings,      ///< 打开设置页
        OpenAbout,         ///< 打开关于页
        RecognizeFile,     ///< 挑一张盘上的图去识别
        Quit,              ///< 退出程序
        CloseAllPins,      ///< 关掉全部贴图；新成员只追加在末尾，表用例按名字钉住了这一张表
        CaptureLongRegion, ///< 滚动长截图：先框一条区域，再反复抓同一块把新的一段接上去
        ClearPinsThrough,  ///< 取消全部贴图的鼠标穿透；新成员只追加在末尾
        TogglePinsVisible, ///< 收起或摆回全部贴图；新成员只追加在末尾
        PasteClipboard,    ///< 把剪贴板里的图贴到屏幕上；新成员只追加在末尾
        NextPinGroup,      ///< 切到下一组贴图；新成员只追加在末尾
        BlankCanvas,       ///< 新建空白画布直接进标注；新成员只追加在末尾
        RepinClosed,       ///< 重新贴出最近关掉的贴图（销毁过的不算）；新成员只追加在末尾
        ManagePinGroups,   ///< 打开贴图组管理窗（哪几组、各有几张、叫什么）；新成员只追加在末尾
    };

} // namespace GyanisScribe::App
