#include "Core/Content/ZipStoreWriter.h"

#include <array>

namespace
{
    /// 标准 CRC32 表：多项式 0xEDB88320，逐字节查表
    const std::array<quint32, 256> &crcTable()
    {
        static const std::array<quint32, 256> table = []
        {
            std::array<quint32, 256> rows{};
            for (quint32 index = 0; index < 256; ++index)
            {
                quint32 value = index;
                for (int bit = 0; bit < 8; ++bit)
                {
                    value = (value & 1U) != 0U ? (value >> 1U) ^ 0xEDB88320U : value >> 1U;
                }
                rows[index] = value;
            }
            return rows;
        }();
        return table;
    }

    quint32 crc32(const QByteArray &data)
    {
        quint32 value = 0xFFFFFFFFU;
        for (const char raw: data)
        {
            const auto byte = static_cast<quint8>(raw);
            value           = (value >> 8U) ^ crcTable()[(value ^ byte) & 0xFFU];
        }
        return value ^ 0xFFFFFFFFU;
    }

    void append16(QByteArray &out, quint16 value)
    {
        out.append(static_cast<char>(value & 0xFFU));
        out.append(static_cast<char>((value >> 8U) & 0xFFU));
    }

    void append32(QByteArray &out, quint32 value)
    {
        out.append(static_cast<char>(value & 0xFFU));
        out.append(static_cast<char>((value >> 8U) & 0xFFU));
        out.append(static_cast<char>((value >> 16U) & 0xFFU));
        out.append(static_cast<char>((value >> 24U) & 0xFFU));
    }
} // namespace

namespace GyanisScribe::Core
{
    void ZipStoreWriter::addFile(const QByteArray &name, const QByteArray &data)
    {
        m_entries.push_back(Entry{name, data, crc32(data)});
    }

    QByteArray ZipStoreWriter::finish() const
    {
        QByteArray zip;
        // 中央目录每条要回填偏移：边写边记
        std::vector<quint32> offsets;
        offsets.reserve(m_entries.size());

        for (const auto &entry: m_entries)
        {
            offsets.push_back(static_cast<quint32>(zip.size()));
            append32(zip, 0x04034B50U); // 本地头签名
            append16(zip, 20);          // 需要的版本
            append16(zip, 0);           // 通用标志
            append16(zip, 0);           // 方法：0 = 不压缩
            append16(zip, 0);           // 修改时间：固定
            append16(zip, 0x0021);      // 修改日期：1980-01-01
            append32(zip, entry.crc);
            append32(zip, static_cast<quint32>(entry.data.size()));
            append32(zip, static_cast<quint32>(entry.data.size()));
            append16(zip, static_cast<quint16>(entry.name.size()));
            append16(zip, 0); // 扩展字段长度
            zip.append(entry.name);
            zip.append(entry.data);
        }

        const quint32 centralOffset = static_cast<quint32>(zip.size());
        for (std::size_t index = 0; index < m_entries.size(); ++index)
        {
            const auto &entry = m_entries[index];
            append32(zip, 0x02014B50U); // 中央目录签名
            append16(zip, 20);          // 生成版本
            append16(zip, 20);          // 需要的版本
            append16(zip, 0);
            append16(zip, 0);
            append16(zip, 0);
            append16(zip, 0x0021);
            append32(zip, entry.crc);
            append32(zip, static_cast<quint32>(entry.data.size()));
            append32(zip, static_cast<quint32>(entry.data.size()));
            append16(zip, static_cast<quint16>(entry.name.size()));
            append16(zip, 0); // 扩展字段
            append16(zip, 0); // 注释
            append16(zip, 0); // 起始磁盘
            append16(zip, 0); // 内部属性
            append32(zip, 0); // 外部属性
            append32(zip, offsets[index]);
            zip.append(entry.name);
        }
        const quint32 centralSize = static_cast<quint32>(zip.size()) - centralOffset;

        append32(zip, 0x06054B50U); // 结束记录签名
        append16(zip, 0);           // 这张盘编号
        append16(zip, 0);           // 中央目录起始盘
        append16(zip, static_cast<quint16>(m_entries.size()));
        append16(zip, static_cast<quint16>(m_entries.size()));
        append32(zip, centralSize);
        append32(zip, centralOffset);
        append16(zip, 0); // 注释长度
        return zip;
    }
} // namespace GyanisScribe::Core
