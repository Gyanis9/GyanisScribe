/**
 * @file SettingsStore.h
 * @brief QSettings 后端的配置存储，含 JSON 导入导出
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Settings/ISettingsStore.h"

#include <memory>

class QSettings;

namespace GyanisScribe::Core
{

    /**
     * @brief ISettingsStore 的 QSettings 实现
     * @details 生产用默认位置（Windows 走注册表）；传路径的那一个供用例与便携模式使用，
     *          两者行为一致，只是后端指向不同。写操作不每次落盘，由 sync() 或析构统一刷，
     *          避免设置页每敲一个字符就写一次注册表。
     */
    class SettingsStore : public ISettingsStore
    {
        Q_OBJECT

    public:
        /**
         * @brief 用应用的默认配置位置构造
         * @param parent QObject 父对象，默认无
         */
        explicit SettingsStore(QObject *parent = nullptr);

        /**
         * @brief 用指定的 INI 文件构造
         * @param iniFile 配置文件路径
         * @param parent QObject 父对象，默认无
         * @note 生产路径也走这一支：程序目录里有 portable.txt 标记时，AppController 就按
         *       Base::Paths::portableConfigFile() 指的那份 INI 构造（绿色版把配置摆在程序旁边）；
         *       没有标记时恒用系统默认位置（Windows 是注册表）。
         */
        explicit SettingsStore(const std::filesystem::path &iniFile, QObject *parent = nullptr);

        /**
         * @brief 析构并冲刷未落盘的改动
         */
        ~SettingsStore() override;

        SettingsStore(const SettingsStore &)            = delete;
        SettingsStore &operator=(const SettingsStore &) = delete;

        /**
         * @brief 读文本取值，见 ISettingsStore::stringValue()
         * @param key 键名
         * @param defaultValue 键不存在、或存的文本全为空白时的回落值
         * @return 存到的文本或回落值
         * @details 重写基类接口：全空白的文本按「没值」处理并记 Warning，与整数·布尔·列表同一口径，
         *          不把空白原样交给下游当路径用。
         */
        [[nodiscard]] QString stringValue(std::string_view key, const QString &defaultValue) const override;

        /**
         * @brief 读整数取值，严格解析，见 ISettingsStore::intValue()
         * @param key 键名
         * @param defaultValue 键不存在或文本不是十进制整数时的回落值
         * @return 解析出的整数或回落值
         * @details 重写基类接口：用 std::from_chars 且要求整串消费完，"12abc" 与 "1.5" 都算坏值——
         *          记一条 Warning 后回落，既不截断也不四舍五入。
         */
        [[nodiscard]] int intValue(std::string_view key, int defaultValue) const override;

        /**
         * @brief 读布尔取值，认固定词表，见 ISettingsStore::boolValue()
         * @param key 键名
         * @param defaultValue 键不存在或文本不在词表内时的回落值
         * @return 解析出的布尔值或回落值
         * @details 重写基类接口：词表是 true/false、1/0、yes/no（大小写与首尾空格忽略）。
         *          不采用 QVariant::toBool()，它把任何非空文本都当成真，会把坏值悄悄读成「开」。
         */
        [[nodiscard]] bool boolValue(std::string_view key, bool defaultValue) const override;

        /**
         * @brief 读列表取值，见 ISettingsStore::stringListValue()
         * @param key 键名
         * @param defaultValue 键不存在时的回落列表
         * @return 列表本体，顺序与存入一致
         */
        [[nodiscard]] QStringList stringListValue(std::string_view key, const QStringList &defaultValue) const override;

        /**
         * @brief 写一个取值并发出变更通知，见 ISettingsStore::setValue()
         * @param key 键名，空键直接拒绝
         * @param value 载荷
         * @return 成功返回空载荷；失败返回原因
         */
        [[nodiscard]] Base::Expected<void> setValue(std::string_view key, const QVariant &value) override;

        /**
         * @brief 落盘，并把改动同步到后端
         */
        void sync() override;

        /**
         * @brief 已存的键名，见 ISettingsStore::allKeys()
         * @return 键列表
         */
        [[nodiscard]] QStringList allKeys() const override;

        /**
         * @brief 导出成 JSON，见 ISettingsStore::exportToFile()
         * @param filePath 目标文件
         * @return 成功返回空载荷；失败返回带路径的原因
         * @details 列表导出成 JSON 数组，其余一律导出成文本：文本档在两个后端（注册表与 INI）之间
         *          是唯一都能放得下的形状。
         */
        [[nodiscard]] Base::Expected<void> exportToFile(const std::filesystem::path &filePath) const override;

        /**
         * @brief 从 JSON 导入，见 ISettingsStore::importFromFile()
         * @param filePath 来源文件
         * @return 成功返回写入的键数（可以为 0）；失败返回带路径与原因的错误
         */
        [[nodiscard]] Base::Expected<int> importFromFile(const std::filesystem::path &filePath) override;

    private:
        /**
         * @brief 两个公开构造的汇合点
         * @param settings 已建好的后端，所有权交进来
         * @param parent QObject 父对象
         */
        explicit SettingsStore(std::unique_ptr<QSettings> settings, QObject *parent);

        std::unique_ptr<QSettings> m_settings; ///< 配置后端，所有权在本对象手里
    };

} // namespace GyanisScribe::Core
