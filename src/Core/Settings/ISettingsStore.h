/**
 * @file ISettingsStore.h
 * @brief 配置读写的对外接口，取值与变更通知的唯一契约
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <filesystem>
#include <string_view>

namespace GyanisScribe::Core
{

    /**
     * @brief 配置存储抽象
     * @details 界面层与模块之间只通过本接口拿配置：模块不认识 QSettings，也不认识对话框，
     *          这样设置页改一项只需 emit 出来的 key 能被订阅者收到。
     * @note 键名一律「分组/大驼峰」，例如 Paths/Screenshot、Hotkey/Capture、Theme/Mode；
     *       分组前缀由使用它的模块在 SettingsKeys.h 里给常量，别在调用点写字面量。
     */
    class ISettingsStore : public QObject
    {
        Q_OBJECT

    public:
        using QObject::QObject;

        /**
         * @brief 析构存储
         */
        ~ISettingsStore() override = default;

        /**
         * @brief 读一个字符串取值
         * @param key 键名
         * @param defaultValue 键不存在时的回落值
         * @return 存到的文本，或回落值
         * @note 键存在但文本全为空白也按回落处理并记一条 Warning：空路径交下去要到写文件那一刻才炸，
         *       两个后端与四类取值的回落口径必须一致
         */
        [[nodiscard]] virtual QString stringValue(std::string_view key, const QString &defaultValue) const = 0;

        /**
         * @brief 读一个整数取值
         * @param key 键名
         * @param defaultValue 键不存在、或存的文本不是整数时的回落值
         * @return 解析出的整数
         * @note 存的文本解析不出整数时会记一条 Warning 日志再回落，不静默把「坏值」当「没值」
         */
        [[nodiscard]] virtual int intValue(std::string_view key, int defaultValue) const = 0;

        /**
         * @brief 读一个布尔取值
         * @param key 键名
         * @param defaultValue 键不存在、或存的文本不是布尔时的回落值
         * @return true 或 false
         */
        [[nodiscard]] virtual bool boolValue(std::string_view key, bool defaultValue) const = 0;

        /**
         * @brief 读一个字符串列表取值
         * @param key 键名
         * @param defaultValue 键不存在时的回落列表
         * @return 列表，顺序按存进去的那一份
         */
        [[nodiscard]] virtual QStringList stringListValue(std::string_view key, const QStringList &defaultValue) const = 0;

        /**
         * @brief 写一个取值
         * @param key 键名
         * @param value 载荷，类型由调用方决定
         * @return 成功返回空载荷；失败返回原因（键名为空或存储不可写）
         * @note 成功之后会发 valueChanged(key)，订阅者据此即时生效，不需要重启
         */
        virtual Base::Expected<void> setValue(std::string_view key, const QVariant &value) = 0;

        /**
         * @brief 立刻落盘
         */
        virtual void sync() = 0;

        /**
         * @brief 全部键名，导入导出与设置页遍历用
         * @return 已存下的键，不含默认值
         */
        [[nodiscard]] virtual QStringList allKeys() const = 0;

        /**
         * @brief 把当前配置导出成 JSON 文本
         * @param filePath 目标文件，已存在则覆盖
         * @return 成功返回空载荷；失败返回带路径与原因的 Error
         */
        virtual Base::Expected<void> exportToFile(const std::filesystem::path &filePath) const = 0;

        /**
         * @brief 从 JSON 文本导入配置
         * @param filePath 来源文件
         * @return 成功返回导入的键数；失败返回带路径与原因的 Error
         * @note 只覆盖文件里出现的那些键，文件里没有的键保持原值；每个被覆盖的键各发一次 valueChanged
         */
        virtual Base::Expected<int> importFromFile(const std::filesystem::path &filePath) = 0;

    signals:
        /**
         * @brief 某一项配置变了
         * @param key 变化的键名
         */
        void valueChanged(const QString &key);
    };

} // namespace GyanisScribe::Core
