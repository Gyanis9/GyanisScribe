#!/usr/bin/env bash
# 容器内各脚本共用的环境：仓库根、Conan 默认档、无界面 Qt 平台。
# 用法：source scripts/container/env.sh
set -euo pipefail

# 仓库根取自脚本自身位置，不用 git rev-parse：容器里可能没有 .git（docker cp 进来的树），
# 而 git 一旦在别的树上成功返回，脚本就会拿着错的根继续跑
CONTAINER_SCRIPT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export CONTAINER_SCRIPT_ROOT

# cppstd=23 且关扩展的那份档位；用 default 档会让依赖库按 gnu17 编出来
export CONAN_DEFAULT_PROFILE_PATH="${CONTAINER_SCRIPT_ROOT}/Conan/Profiles/LinuxGcc13"

# 无界面环境里 Qt 的 offscreen 平台插件；有 Xvfb 时 test.sh 会用 xvfb-run 覆盖这一条
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

export CC="${CC:-gcc-13}"
export CXX="${CXX:-g++-13}"

# sanitizer 的运行期参数放在这里而不是 sanitize.sh 里：这三条入口（build.sh/test.sh/sanitize.sh）
# 都会 source 本文件，而 asan 档的树用 test.sh 直接跑同样会撞上这些行为——
# 只挂在 sanitize.sh 上时，换一条入口跑就会出现「两条字体缓存泄漏用例红」这种与代码无关的差别。
# 泄漏检测默认开：Qt 自身的常驻分配若被报出来，要在这里看清单而不是先关掉了事。
export ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=1:abort_on_error=0:detect_stack_use_after_return=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=0}"
# 只放过系统库的字体缓存（判据见 scripts/container/lsan-suppressions.txt）
export LSAN_OPTIONS="${LSAN_OPTIONS:-suppressions=${CONTAINER_SCRIPT_ROOT}/scripts/container/lsan-suppressions.txt}"
