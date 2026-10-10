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

# 容器验证清单里单列的一条：「容器内能通过 Xvfb 启动主程序」。ctest 只跑用例，
# 产品入口一次没被起来过就是没验过——所以这一格单独跑，缺二进制算红而不是跳过。
APP_BIN="${TREE}/bin/GyanisScribe"
if [ ! -x "${APP_BIN}" ]; then
    echo "[失败] 没找到主程序 ${APP_BIN}：先跑 scripts/container/build.sh ${PRESET}" >&2
    exit 1
fi
# --selftest 走完启动路径就退：日志接管成功即返回 0（热键在 Linux 上登记不上是既有的降级，不算失败）
if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a "${APP_BIN}" --selftest
else
    "${APP_BIN}" --selftest
fi
echo "[ok] 主程序在虚拟屏上起得来（--selftest 退出码 0）"
echo "[ok] ${PRESET} 测试通过"
