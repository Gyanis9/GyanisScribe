/**
 * @file HistoryStore.h
 * @brief 历史记录的落盘存储：一份 JSON 清单，成品文件留在原处
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/History/HistoryRules.h"
#include "Core/History/HistoryTypes.h"

#include <QDateTime>
#include <QJsonArray>
#include <QVector>

#include <filesystem>
#include <string>

namespace GyanisScribe::Core
{

    /**
     * @brief 一次历史读取的结果
     * @details 读得懂的好记录照常裁剪；坏记录不进列表，但条数与第一条原因带出来给界面提示。
     */
    struct HistoryLoadResult
    {
        QVector<HistoryEntry> entries;            ///< 读得懂且按策略裁剪后的记录，时间倒序
        int                   skippedRecords = 0; ///< 读不懂而被跳过的条目数
        std::string           firstProblem;       ///< 第一条跳过的原因；没有跳过时为空
    };

    /**
     * @brief 历史清单的读写
     * @details 清单只记路径、时间、类别与尺寸，不拷贝成品：几百张截图搬进应用数据目录
     *          会把盘塞满，而用户按目录找文件的习惯也就断了。
     *          保留策略裁掉的同样只是记录，成品文件留在原处由用户自己处置。
     */
    class HistoryStore
    {
    public:
        /**
         * @brief 指向一份历史清单
         * @param storagePath 清单文件路径；所在目录不存在时写入会创建它
         */
        explicit HistoryStore(std::filesystem::path storagePath);

        /**
         * @brief 默认清单位置
         * @return 应用数据目录下的 history.json
         */
        [[nodiscard]] static std::filesystem::path defaultStoragePath();

        /**
         * @brief 读出并按策略裁剪后的历史
         * @param policy 保留策略
         * @param now 判过期用的时刻
         * @return 时间倒序的记录，以及被跳过的坏记录条数与第一条原因
         * @details 成品已经不在了的记录在这里被丢掉：列表里点一张不存在的图，
         *          比列表少一条更难解释。单条读不懂只跳过该条，原始 JSON 留给下次写回。
         * @note 整份清单读不出来（根 JSON 坏掉、文件打不开）仍整体报错，
         *       不拿一份空表冒充成功。
         */
        [[nodiscard]] Base::Expected<HistoryLoadResult> load(const HistoryRetentionPolicy &policy, const QDateTime &now) const;

        /**
         * @brief 追加一条并按策略裁剪，随后落盘
         * @param entry 新记录，路径为身份：同路径再来一次是覆盖而不是并列两条
         * @param policy 保留策略
         * @param now 判过期用的时刻
         * @return 成功返回空载荷；失败返回带路径与原因的 Error
         */
        [[nodiscard]] Base::Expected<void> append(const HistoryEntry &entry, const HistoryRetentionPolicy &policy, const QDateTime &now);

        /**
         * @brief 从清单里去掉一条
         * @param filePath 成品文件路径，即这条记录的身份
         * @return 成功返回空载荷；清单里没有这条记录时返回 InvalidArgument
         * @note 只删记录不删文件：用户的成品目录由用户自己清，软件不替他做不可逆的那一步。
         */
        [[nodiscard]] Base::Expected<void> remove(const std::filesystem::path &filePath);

        /**
         * @brief 清空整份清单
         * @return 成功返回空载荷；写不回去时返回带路径与原因的 Error
         * @note 与 remove() 同一条口径：只清记录不动成品文件，用户的图与视频由用户自己决定去留
         */
        [[nodiscard]] Base::Expected<void> clear();

    private:
        /// 原样读出的清单：好记录解码成条目，坏记录留原始 JSON 等写回时接上
        struct RawHistory
        {
            QVector<HistoryEntry> entries;      ///< 读得懂的记录
            QJsonArray            badRecords;   ///< 读不懂的原始项，写回时原样带上
            int                   skipped = 0;  ///< 坏记录条数
            std::string           firstProblem; ///< 第一条坏记录的原因
        };

        /**
         * @brief 原样读出清单，不裁也不丢
         * @return 好记录、坏记录与第一条原因；文件不存在时返回空表
         * @details 单条读不懂只跳过该条并留下原因；根 JSON 坏掉仍整体报错。
         */
        [[nodiscard]] Base::Expected<RawHistory> readRaw() const;

        /**
         * @brief 把整份记录表写回清单
         * @param entries 要写的记录
         * @param badRecords 上次读到的坏原始项，原样接在记录后面
         * @return 成功返回空载荷；失败返回带路径与原因的 Error
         * @details 先写临时名再原子替换：中途断电留下一份截断的清单，下次启动就读不出历史了。
         */
        [[nodiscard]] Base::Expected<void> writeRaw(const QVector<HistoryEntry> &entries, const QJsonArray &badRecords) const;

        std::filesystem::path m_storagePath; ///< 清单文件位置
    };

} // namespace GyanisScribe::Core
