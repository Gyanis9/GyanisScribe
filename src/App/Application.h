/**
 * @file Application.h
 * @brief QApplication 子类，负责身份设置与日志启动
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Hotkey/IHotkeyManager.h"

#include <QApplication>

#include <memory>

namespace GyanisScribe::App
{
    class AppController;
    class SingleInstance;
} // namespace GyanisScribe::App

/**
 * @brief 应用对象
 * @details 构造即把「组织名 + 应用名」定下来，QStandardPaths 与 QSettings 的取值全看这两项，
 *          因此日志目录必须在它自己构造完之后才谈得上正确；控制器同样排在应用对象之后创建，
 *          它要用的屏幕列表与配置位置都依赖这两步已经就位。
 */
class Application : public QApplication
{
    Q_OBJECT

public:
    /**
     * @brief 构造应用并完成身份设置、日志初始化与控制器创建
     * @param argc 命令行参数个数，Qt 要求传引用
     * @param argv 命令行参数数组
     * @note 日志初始化失败不阻断启动：Logger 会退回 stderr，用户至少还能看见界面
     */
    Application(int &argc, char **argv);

    /**
     * @brief 析构应用：先交还控制器（注销热键、收遮罩），再冲刷日志
     */
    ~Application() override;

    Application(const Application &)            = delete;
    Application &operator=(const Application &) = delete;


    /**
     * @brief 启动路径是否走通
     * @return true 日志已接管输出，控制器已建好
     * @return false 日志目录建不出来或日志文件打不开；此时界面仍可用，但事后查不到现场
     */
    [[nodiscard]] bool isReady() const noexcept;

    /**
     * @brief 启动时的热键登记结果
     * @return 本轮报告，含每一项没登记上的原因；界面用它把冲突提示发给用户
     */
    [[nodiscard]] const GyanisScribe::Hotkey::HotkeyReloadReport &hotkeyReport() const noexcept;

    /**
     * @brief 试着当那个唯一的实例
     * @return true 表示锁拿到了，可以正常启动；false 表示已有一份在跑，本次应退出
     */
    bool tryBecomePrimaryInstance();

    /**
     * @brief 让已持锁的那一份把自己唤到前台
     * @return true 表示对方收到了这句敲门
     */
    bool wakePrimaryInstance();

private:
    std::unique_ptr<GyanisScribe::App::AppController>  m_controller;      ///< 串起配置、平台、热键与截图的核心控制器
    std::unique_ptr<GyanisScribe::App::SingleInstance> m_singleInstance;  ///< 单实例守卫，第二次启动靠它把前一份唤出来
    GyanisScribe::Hotkey::HotkeyReloadReport           m_hotkeyReport;    ///< 启动那一轮的登记结果，界面随后要拿去提示
    bool                                               m_isReady = false; ///< 日志是否接管成功
};
