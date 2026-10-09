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
