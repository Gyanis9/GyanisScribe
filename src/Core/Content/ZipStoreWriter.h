/**
 * @file ZipStoreWriter.h
 * @brief store-only 的 ZIP 容器：只为 XLSX 这种自带压缩的小包服务
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QByteArray>

#include <vector>

namespace GyanisScribe::Core
{

    /**
     * @brief 只写不压缩的 ZIP 打包器
     * @details 不依赖第三方库、不写 zip64：条目少、体积小，压缩收益抵不上多一份依赖。
     *          时间戳固定为 1980-01-01，同一份输入产出逐字节相同的包。
     */
    class ZipStoreWriter
    {
    public:
        /**
         * @brief 加一个文件条目
         * @param name 包内路径，ASCII，正斜杠分隔
         * @param data 文件内容
         */
        void addFile(const QByteArray &name, const QByteArray &data);

        /**
         * @brief 收口成完整的 ZIP 字节
         * @return 本地头 + 数据 + 中央目录 + 结束记录
         */
        [[nodiscard]] QByteArray finish() const;

    private:
        struct Entry
        {
            QByteArray name;       ///< 包内路径
            QByteArray data;       ///< 原文
            quint32    crc    = 0; ///< CRC32
            quint32    offset = 0; ///< 本地头在包内的偏移
        };

        std::vector<Entry> m_entries; ///< 已加入的条目，按加入顺序
    };

} // namespace GyanisScribe::Core
