#include "Core/Privacy/PrivacyScanTiles.h"

#include <algorithm>
#include <utility>

namespace GyanisScribe::Core::Privacy
{
    namespace
    {
        /**
         * @brief 这一行是不是同一物理行在相邻带里的第二次出现
         * @param lines 已并进来的行，坐标是全局的
         * @param candidate 待并入的行，坐标已平移成全局
         * @return true 表示文本相同且框与已有行相交
         */
        bool repeatsMergedLine(const QVector<OcrLine> &lines, const OcrLine &candidate)
        {
            const QString text = candidate.text();
            for (const OcrLine &existing: lines)
            {
                // 只看文本会吞掉页面上真实重复的号码，只看位置又会吞掉上下紧邻的短行，两样一起看
                if (existing.text() == text && existing.boundingBox.intersects(candidate.boundingBox))
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    std::vector<QRect> scanTilesForImage(const QSize imageSize, const int bandHeight, const int overlap)
    {
        if (imageSize.width() <= 0 || imageSize.height() <= 0 || bandHeight <= 0 || overlap < 0 || overlap >= bandHeight)
        {
            return {};
        }

        std::vector<QRect> tiles;
        const int          step = bandHeight - overlap;
        for (int top = 0; top < imageSize.height(); top += step)
        {
            const int height = std::min(bandHeight, imageSize.height() - top);
            tiles.push_back(QRect{0, top, imageSize.width(), height});
            // 这一带已经够到图底就停：再走一步只会多切一个几乎重复的短带
            if (top + height >= imageSize.height())
            {
                break;
            }
        }
        return tiles;
    }

    OcrResult mergeTileResult(const OcrResult &merged, const OcrResult &tileResult, const QPoint tileOrigin, const int bandHeight, const bool isFirstBand, const bool isLastBand)
    {
        OcrResult result = merged;
        if (result.language.isEmpty())
        {
            result.language = tileResult.language;
        }

        for (const OcrLine &line: tileResult.lines)
        {
            const bool clippedTop    = !isFirstBand && line.boundingBox.top() <= 1;
            const bool clippedBottom = !isLastBand && line.boundingBox.bottom() >= bandHeight - 2;
            if (clippedTop || clippedBottom)
            {
                continue;
            }

            OcrLine shifted;
            shifted.boundingBox = line.boundingBox.translated(tileOrigin);
            shifted.words.reserve(line.words.size());
            for (const OcrWord &word: line.words)
            {
                shifted.words.push_back(OcrWord{word.text, word.boundingBox.translated(tileOrigin)});
            }

            if (repeatsMergedLine(result.lines, shifted))
            {
                continue;
            }
            result.lines.push_back(std::move(shifted));
        }
        return result;
    }
} // namespace GyanisScribe::Core::Privacy
