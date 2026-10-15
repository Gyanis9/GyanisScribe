/**
 * @file Application.h
 * @brief QApplication 子类，负责身份设置与日志启动
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "App/CommandLine.h"
#include "Core/Hotkey/IHotkeyManager.h"

#include <QApplication>

#include <memory>

namespace GyanisScribe::App
{
    class SingleInstance;
} // namespace GyanisScribe::App

namespace GyanisScribe::Main
{
    class AppController;
} // namespace GyanisScribe::Main

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
     * @param request 命令行解析出的启动请求；一次性的命令行动作（截图/贴图/取色）走「不锁单实例、
     *        不登记热键」的装配：脚本在常驻实例开着时照样能用，也不去抢那八个全局热键
     * @note 日志初始化失败不阻断启动：Logger 会退回 stderr，用户至少还能看见界面
     */
    Application(int &argc, char **argv, const GyanisScribe::App::CommandLineRequest &request = {});

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
    [[nodiscard]] const GyanisScribe::Core::HotkeyReloadReport &hotkeyReport() const noexcept;

    /**
     * @brief 试着当那个唯一的实例
     * @return true 表示锁拿到了，可以正常启动；false 表示本次不该继续（锁在别人手里，
     *         或本机监听没能建起来——两种都由 Main 收口去唤醒与退出）
     * @note 探测发生在构造里：不拿到锁就不建控制器、不登记热键，本次启动只剩一条日志
     */
    bool tryBecomePrimaryInstance();

    /**
     * @brief 让已持锁的那一份把自己唤到前台
     * @return true 表示对方收到了这句敲门
     */
    bool wakePrimaryInstance();

    /**
     * @brief 执行命令行要的那一个动作，并收场
     * @param request 解析好的请求（不含 Help/Version，那两条在 Main 里就打发了）
     * @return 进程退出码：自检按启动路径给出 0/1；截图落盘成功 0、失败 1；
     *         贴图与取色是常驻动作，事件循环结束后按 0 交回
     * @details 截图那条把落盘路径打到标准输出、失败原因打到标准错误：脚本按这两条接着干活
     */
    int executeCommandLine(const GyanisScribe::App::CommandLineRequest &request);

private:
    /**
     * @brief 一次性命令行截图：等交付完或失败，打印结果并返回退出码
     * @param request 启动请求，要用其中的范围、`--to` 点名的去处与 `--exec` 那一条命令
     * @return 0 表示点名的去处都交付完（落了盘就把路径打到标准输出）；1 表示失败并把原因打到了标准错误
     * @details 点了 `pin` 的那一次不退出：贴出来的那张属于这个进程，退出就把它一起带走（与 --pin 同一档做法）。
     */
    int runOneShotCapture(const GyanisScribe::App::CommandLineRequest &request);

    std::unique_ptr<GyanisScribe::Main::AppController> m_controller;                ///< 串起配置、平台、热键与截图的核心控制器；非主实例时为空
    std::unique_ptr<GyanisScribe::App::SingleInstance> m_singleInstance;            ///< 单实例守卫，第二次启动靠它把前一份唤出来
    GyanisScribe::Core::HotkeyReloadReport             m_hotkeyReport;              ///< 启动那一轮的登记结果，界面随后要拿去提示
    bool                                               m_isPrimaryInstance = false; ///< 构造里那一次探测的结果，tryBecomePrimaryInstance 只回报它
    bool                                               m_isReady           = false; ///< 日志是否接管成功
};
