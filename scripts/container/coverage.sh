#!/usr/bin/env bash
# 容器内覆盖率：coverage 预设建一棵带 --coverage 的树，跑完用例后用 gcovr 汇总。
# 用法：scripts/container/coverage.sh [阈值百分比]（缺省 60）
#
# 只统计 src/ 下的产物：tests/ 与 Conan 拉下来的依赖不算本工程的覆盖率。
set -euo pipefail

# shellcheck disable=SC1091
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${CONTAINER_SCRIPT_ROOT}"

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
THRESHOLD="${1:-60}"
REPORT_DIR="${CONTAINER_SCRIPT_ROOT}/build/coverage/coverage-report"

"${SCRIPT_DIR}/build.sh" coverage
"${SCRIPT_DIR}/test.sh" coverage

command -v gcovr >/dev/null 2>&1 || { echo "[失败] 没装 gcovr，先跑 scripts/container/init.sh" >&2; exit 1; }

mkdir -p "${REPORT_DIR}"
gcovr --root "${CONTAINER_SCRIPT_ROOT}" \
      --filter 'src/' \
      --exclude 'src/App/Main.cpp' \
      --gcov-executable gcov-13 \
      --print-summary \
      --html-details "${REPORT_DIR}/index.html" \
      --txt "${REPORT_DIR}/coverage.txt"

LINE_PERCENT="$(grep -oE 'lines: [0-9.]+%' "${REPORT_DIR}/coverage.txt" | head -1 | grep -oE '[0-9.]+' || echo 0)"
echo "[读数] 行覆盖 ${LINE_PERCENT}%（阈值 ${THRESHOLD}%），报告在 ${REPORT_DIR}/index.html"
awk -v got="${LINE_PERCENT}" -v need="${THRESHOLD}" 'BEGIN { exit (got + 0 >= need + 0) ? 0 : 1 }' || {
    echo "[失败] 行覆盖 ${LINE_PERCENT}% 低于阈值 ${THRESHOLD}%" >&2
    exit 1
}
echo "[ok] 覆盖率达标"
