#!/usr/bin/env bash
# 容器内一键测试：有 Xvfb 就用虚拟屏跑（真的建一遍窗口），没有就退回 offscreen。
# 用法：scripts/container/test.sh [debug|release|asan|tsan|coverage] [-j N]
set -euo pipefail

# shellcheck disable=SC1091
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${CONTAINER_SCRIPT_ROOT}"

PRESET="${1:-debug}"
shift || true
JOBS="-j4"
if [ "${1:-}" = "-j" ]; then
    JOBS="-j${2:-4}"
fi

TREE="${CONTAINER_SCRIPT_ROOT}/build/${PRESET}"
if [ ! -f "${TREE}/CTestTestfile.cmake" ]; then
    echo "[失败] ${TREE} 里没有测试清单：先跑 scripts/container/build.sh ${PRESET}" >&2
    exit 1
fi

if command -v xvfb-run >/dev/null 2>&1; then
    # 虚拟屏下才谈得上「界面起得来」：offscreen 平台不建窗口，遮罩与贴图那批用例只会验到逻辑
    echo "[测试] preset=${PRESET}，走 Xvfb"
    xvfb-run -a ctest --test-dir "${TREE}" --preset "${PRESET}" ${JOBS} --output-on-failure
else
    echo "[测试] preset=${PRESET}，没有 xvfb-run，退回 QT_QPA_PLATFORM=${QT_QPA_PLATFORM}"
    ctest --test-dir "${TREE}" --preset "${PRESET}" ${JOBS} --output-on-failure
fi
echo "[ok] ${PRESET} 测试通过"
