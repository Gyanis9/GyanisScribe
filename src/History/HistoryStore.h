/**
 * @file HistoryStore.h
 * @brief 历史记录的落盘存储：一份 JSON 清单，成品文件留在原处
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"
#include "History/HistoryRules.h"
#include "History/HistoryTypes.h"

#include <QDateTime>
#include <QVector>

#include <filesystem>

namespace GyanisScribe::History
{

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
         * @return 时间倒序的记录；清单文件还不存在时返回空表而不是错误
         * @details 成品已经不在了的记录在这里被丢掉：列表里点一张不存在的图，
         *          比列表少一条更难解释。
         * @note 读不懂的清单会整体报错而不是读出半份：静默丢行会让人以为历史自己少了。
         */
        [[nodiscard]] Core::Expected<QVector<HistoryEntry>> load(const HistoryRetentionPolicy &policy, const QDateTime &now) const;

        /**
         * @brief 追加一条并按策略裁剪，随后落盘
         * @param entry 新记录，路径为身份：同路径再来一次是覆盖而不是并列两条
         * @param policy 保留策略
         * @param now 判过期用的时刻
         * @return 成功返回空载荷；失败返回带路径与原因的 Error
         */
        [[nodiscard]] Core::Expected<void> append(const HistoryEntry &entry, const HistoryRetentionPolicy &policy, const QDateTime &now);

        /**
         * @brief 从清单里去掉一条
         * @param filePath 成品文件路径，即这条记录的身份
         * @return 成功返回空载荷；清单里没有这条记录时返回 InvalidArgument
         * @note 只删记录不删文件：用户的成品目录由用户自己清，软件不替他做不可逆的那一步。
         */
        [[nodiscard]] Core::Expected<void> remove(const std::filesystem::path &filePath);

        /**
         * @brief 清空整份清单
         * @return 成功返回空载荷；写不回去时返回带路径与原因的 Error
         * @note 与 remove() 同一条口径：只清记录不动成品文件，用户的图与视频由用户自己决定去留
         */
        [[nodiscard]] Core::Expected<void> clear();

    private:
        /**
         * @brief 原样读出清单，不裁也不丢
         * @return 记录表；文件不存在时返回空表
         * @details 任一条读不懂就整体报错并指出第几条：读半份清单会让用户以为历史自己少了。
         */
        [[nodiscard]] Core::Expected<QVector<HistoryEntry>> readRaw() const;

        /**
         * @brief 把整份记录表写回清单
         * @param entries 要写的记录
         * @return 成功返回空载荷；失败返回带路径与原因的 Error
         * @details 先写临时名再原子替换：中途断电留下一份截断的清单，下次启动就读不出历史了。
         */
        [[nodiscard]] Core::Expected<void> writeRaw(const QVector<HistoryEntry> &entries) const;

        std::filesystem::path m_storagePath; ///< 清单文件位置
    };

} // namespace GyanisScribe::History
