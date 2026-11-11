/**
 * @file TestStepsEnvironment.h
 * @brief TestGuiSteps 共用的 QApplication 全局测试环境
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

namespace GyanisScribe::TestSupport
{
    /**
     * @brief 登记进程内唯一的 QApplication 全局测试环境
     * @details TestStepsEnvironment.cpp 在静态初始化阶段调用一次；函数自身幂等，
     *          重复调用不会再造第二个 QApplication。HUD、说明、PDF 与审核窗四个套件
     *          共用这一份应用对象，按用例名过滤单跑任一套件时它同样会就位。
     */
    void registerStepsTestEnvironment();
} // namespace GyanisScribe::TestSupport
