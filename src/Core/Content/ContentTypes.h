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
    inline constexpr double kTableRowPitchGapFactor = 0.7; ///< 相邻行纵向间隙上限的另一档：按行距中位数折算，容纳单元格内边距
    inline constexpr double kTableCellGapFactor     = 0.8; ///< 单元格间隙下限，按词高中位数折算
    inline constexpr double kTableBoundaryTolerance = 0.5; ///< 列边界聚类容差，按词高中位数折算
    inline constexpr double kTableBoundaryChainFactor = 2.0; ///< 同一簇内相邻分界候选的最大位置间隔，超过且两侧各有至少两个候选时拆簇；按词高中位数折算
    inline constexpr double kTableBoundarySupport   = 0.6; ///< 列边界至少被多少比例的段内行支持；单格行也计入分母
    inline constexpr double kTableSimpleConfidence  = 0.8; ///< 简单表的最低行完整度
    inline constexpr double kTableTwoColumnProseFillFactor = 0.75; ///< 两栏正文守卫：单格文字宽超过列宽的这个比例才算铺满；真机正常行距的两栏正文实测词框几乎顶满列宽
    inline constexpr double kTableTwoColumnProseRowFraction = 0.8; ///< 两栏正文守卫：两格都铺满的行至少占这个比例；真机正常行距的两栏正文逐行如此
    inline constexpr double kTableTwoColumnProsePitchFactor = 2.0; ///< 两栏正文守卫：行距不超过行高的这个倍数；表格行的单元格内边距会把行距抬到 2.5 倍左右

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
