#!/usr/bin/env bash
# 容器内一次性初始化：补齐 Dockerfile.dev 里那套包（幂等，缺什么装什么），再核对工具链。
# 用法：scripts/container/init.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

export DEBIAN_FRONTEND=noninteractive

# 与 Dockerfile.dev 同一条清单：容器复用时（本机那个长期存在的 ubuntu24）不会自动带上镜像里的新包，
# 这里补齐比「要求人人重建镜像」更现实
PACKAGES=(
    build-essential g++-13 clang-18 cmake ninja-build git python3 python3-pip pkg-config
    qt6-base-dev qt6-base-dev-tools libqt6svg6 qt6-svg-dev libgl1-mesa-dev libxkbcommon-dev
    xvfb xauth fonts-noto-cjk lcov gcovr
)

if [ -n "${SKIP_APT:-}" ]; then
    echo "[skip] SKIP_APT 已设，只做体检不装包"
else
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends "${PACKAGES[@]}"
fi

command -v conan >/dev/null 2>&1 || pip3 install --no-cache-dir --break-system-packages 'conan==2.*'

echo "--- 工具链体检"
printf 'g++-13   %s\n' "$(g++-13 --version | head -1)"
printf 'cmake    %s\n' "$(cmake --version | head -1)"
printf 'ninja    %s\n' "$(ninja --version)"
printf 'conan    %s\n' "$(conan --version)"
printf 'xvfb-run %s\n' "$(command -v xvfb-run)"

# Qt 与中文字体少了不会让 configure 立刻失败到看得见：Svg 缺了 IconProvider 编不过，
# 字体缺了界面全是方框而「GUI 起得来」的判据照样绿，所以两项都在这里当场判红
if ! ls /usr/lib/x86_64-linux-gnu/cmake/Qt6Svg/Qt6SvgConfig.cmake >/dev/null 2>&1; then
    echo "[失败] 没找到 Qt6Svg 的开发文件（装 qt6-svg-dev）" >&2
    exit 1
fi
CJK_COUNT="$(fc-list :lang=zh-cn 2>/dev/null | wc -l)"
if [ "${CJK_COUNT}" -lt 1 ]; then
    echo "[失败] 容器里没有中文字体，界面会全是方框（装 fonts-noto-cjk）" >&2
    exit 1
fi
echo "中文字体条目 ${CJK_COUNT} 条"

# 档位指到仓库里那份：default 档的 cppstd 是 gnu17，与工程要求的 C++23 + 关扩展不符。
# 导出这一步由 scripts/container/env.sh 统一做，这里只把结论念出来，免得留下第二处真源
# shellcheck disable=SC1091
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
echo "--- Conan 默认档：${CONAN_DEFAULT_PROFILE_PATH}"
echo "[ok] 容器初始化完成"
