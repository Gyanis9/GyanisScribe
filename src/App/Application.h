/**
 * @file Application.h
 * @brief QApplication 子类，负责身份设置与日志启动
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QApplication>

/**
 * @brief 应用对象
 * @details 构造即把「组织名 + 应用名」定下来，QStandardPaths 与 QSettings 的取值全看这两项，
 *          因此日志目录必须在它自己构造完之后才谈得上正确。
 */
class Application : public QApplication
{
    Q_OBJECT

public:
    /**
     * @brief 构造应用并完成身份设置与日志初始化
     * @param argc 命令行参数个数，Qt 要求传引用
     * @param argv 命令行参数数组
     * @note 日志初始化失败不阻断启动：Logger 会退回 stderr，用户至少还能看见界面
     */
    Application(int &argc, char **argv);

    /**
     * @brief 析构应用，顺带冲刷日志
     */
    ~Application() override;

    Application(const Application &)            = delete;
    Application &operator=(const Application &) = delete;
};
