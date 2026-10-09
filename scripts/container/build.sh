#!/usr/bin/env bash
# 容器内一键构建：cmake --preset + 构建。用法：scripts/container/build.sh [debug|release|asan|tsan|coverage]
set -euo pipefail

# shellcheck disable=SC1091
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${CONTAINER_SCRIPT_ROOT}"

PRESET="${1:-debug}"
echo "[构建] preset=${PRESET}，profile=${CONAN_DEFAULT_PROFILE_PATH}"

# 一条命令也不许靠管道活命：cmake 的失败会被 tail 的退出码 0 盖掉
cmake --preset "${PRESET}"
cmake --build --preset "${PRESET}"
echo "[ok] ${PRESET} 构建完成"
