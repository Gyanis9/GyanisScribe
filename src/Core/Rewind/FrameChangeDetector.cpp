#include "Core/Rewind/FrameChangeDetector.h"

namespace GyanisScribe::Core
{
    namespace
    {
        constexpr int kFingerprintWidth  = 64;
        constexpr int kFingerprintHeight = 36;
    } // namespace

    FrameFingerprint makeFrameFingerprint(const QImage &image, const QString &foregroundTitle)
    {
        FrameFingerprint fingerprint;
        fingerprint.foregroundTitle = foregroundTitle;
        // 空图留空指纹：isSameFrame 据此判为「不同」，避免没抓到画面时被当成没变化
        if (image.isNull())
        {
            return fingerprint;
        }

        fingerprint.size     = image.size();
        fingerprint.cacheKey = static_cast<quint64>(image.cacheKey());
        // 固定缩到 64×36 再转灰度：比对成本与原图大小无关，整屏哈希则对光标闪烁这类小改动过于敏感
        const auto gray = image.scaled(kFingerprintWidth, kFingerprintHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_Grayscale8);
        if (!gray.isNull())
        {
            fingerprint.downscaledGray = QByteArray(reinterpret_cast<const char *>(gray.constBits()), gray.sizeInBytes());
        }
        return fingerprint;
    }

    bool isSameFrame(const FrameFingerprint &left, const FrameFingerprint &right) noexcept
    {
        // 空指纹彼此不算相同：抓帧失败不能被当成前后帧没变化
        if (left.size.isEmpty() || right.size.isEmpty() || left.size != right.size)
        {
            return false;
        }
        // 同一份 QImage 数据共享数据键，省掉逐字节比较；不同图的键不会相等
        if (left.cacheKey != 0 && left.cacheKey == right.cacheKey)
        {
            return true;
        }
        return left.downscaledGray == right.downscaledGray;
    }

    bool isCandidateChange(const FrameFingerprint &current, const FrameFingerprint &previous) noexcept
    {
        return !isSameFrame(current, previous) || current.foregroundTitle != previous.foregroundTitle;
    }
} // namespace GyanisScribe::Core
