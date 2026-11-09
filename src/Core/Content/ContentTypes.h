/**
 * @file ContentTypes.h
 * @brief 识别结果内容动作的共享类型与检测阈值
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QRect>
#include <QString>
#include <QVector>

namespace GyanisScribe::Core
{
    inline constexpr int    kMinTableRows           = 2;   ///< 表格至少需要的行数
    inline constexpr int    kMinTableColumns        = 2;   ///< 表格至少需要的列数
    inline constexpr int    kMaxTableColumns        = 12;  ///< 列数上限；超过视为版面误判
    inline constexpr double kTableVerticalGapFactor = 1.5; ///< 相邻行纵向间隙上限，按行高中位数折算
    inline constexpr double kTableCellGapFactor     = 0.8; ///< 单元格间隙下限，按词高中位数折算
    inline constexpr double kTableBoundaryTolerance = 0.5; ///< 列边界聚类容差，按词高中位数折算
    inline constexpr double kTableBoundarySupport   = 0.6; ///< 列边界至少被多少比例的段内行支持；单格行也计入分母
    inline constexpr double kTableSimpleConfidence  = 0.8; ///< 简单表的最低行完整度

    /**
     * @brief 一次表格检测的结果
     * @details cells 按 rows × columns 排；空串表示该格没有文字。
     *          复杂表同样 found 为真，complex 表示结构无法按列还原，complexReason 给出中文原因，
     *          界面据此说明“检测到复杂表格，暂不支持 CSV”。
     */
    struct TableDetection
    {
        bool                      found   = false;  ///< 是否找到候选表（可能为复杂表）
        bool                      complex = false;  ///< 是否无法按列还原：合并单元格、跨列文字或行列不一致
        int                       rows    = 0;      ///< 表格行数
        int                       columns = 0;      ///< 表格列数
        QVector<QVector<QString>> cells;            ///< rows × columns 的单元格文本
        QRect                     boundingBox;      ///< 表格在原图坐标系里的外接框
        int                       firstLine  = 0;   ///< 表在 OcrResult::lines 里的最小行下标
        int                       lastLine   = -1;  ///< 表在 OcrResult::lines 里的最大行下标（含）
        double                    confidence = 0.0; ///< 非空单元格数恰好等于列数的行占比
        QString                   complexReason;    ///< 复杂表的中文原因拼接；简单表为空
    };
} // namespace GyanisScribe::Core
