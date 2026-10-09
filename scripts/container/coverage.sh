#!/usr/bin/env bash
# 容器内覆盖率：coverage 预设建一棵带 --coverage 的树，跑完用例后用 gcovr 汇总。
# 用法：scripts/container/coverage.sh [阈值百分比]（缺省 55）
#
# 只统计 src/ 下的产物：tests/ 与 Conan 拉下来的依赖不算本工程的覆盖率。
set -euo pipefail

# shellcheck disable=SC1091
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${CONTAINER_SCRIPT_ROOT}"

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
# 缺省阈值按现测余量定：容器里跑出来是 58.5%（src 全量、不含生成的 moc 与 Main.cpp），
# 钉 55 留一点抖动余量；覆盖上来之后要把这个数抬上去，而不是往下调
THRESHOLD="${1:-55}"
REPORT_DIR="${CONTAINER_SCRIPT_ROOT}/build/coverage/coverage-report"

"${SCRIPT_DIR}/build.sh" coverage
"${SCRIPT_DIR}/test.sh" coverage

command -v gcovr >/dev/null 2>&1 || { echo "[失败] 没装 gcovr，先跑 scripts/container/init.sh" >&2; exit 1; }

mkdir -p "${REPORT_DIR}"
# 阈值判定交给 gcovr 自己（--fail-under-line）：它按内部算术比较，不需要把覆盖率
# 抄出来再解析一次。上一版从 coverage.json 找行覆盖率，而 gcovr 8.6 的现代 JSON 里
# 根本没有汇总字段（汇总只在 --json-summary 那份里），于是判据读成空值、只能靠额外的
# 「读不到就失败」兜底——两道都不如不让它经过字符串。
if gcovr --root "${CONTAINER_SCRIPT_ROOT}" \
    --filter 'src/' \
    --exclude 'src/App/Main.cpp' \
    --gcov-executable gcov-13 \
    --print-summary \
    --fail-under-line "${THRESHOLD}" \
    --json "${REPORT_DIR}/coverage.json" \
    --html-details "${REPORT_DIR}/index.html" \
    >"${REPORT_DIR}/summary.txt" 2>&1; then
    # 读数只为日志好读，取自 --print-summary 那段文字；措辞随版本变了也只是这行少个数字，
    # 不再影响判定（判定是上面 gcovr 的退出码）
    echo "[读数] $(grep -oE 'lines: [0-9.]+% \([0-9]+ out of [0-9]+\)' "${REPORT_DIR}/summary.txt" | head -1)（阈值 ${THRESHOLD}%），报告在 ${REPORT_DIR}/index.html"
    echo "[ok] 覆盖率达标"
else
    echo "[失败] 行覆盖率低于阈值 ${THRESHOLD}%，读数见 ${REPORT_DIR}/summary.txt" >&2
    exit 1
fi
