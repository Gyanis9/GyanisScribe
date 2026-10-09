#!/usr/bin/env bash
# 容器内 Sanitizer 门禁：ASan(+UBSan) 与 TSan 各建一棵树、各跑一遍用例。
# 用法：scripts/container/sanitize.sh [asan|tsan]（缺省两个都跑）
#
# MSVC 不提供 TSan，而 ASan 在 Windows 上不支持泄漏检测，因此这两档只在容器里当硬门禁；
# 本机绿不构成跨平台正确性的证据（规范第 34 条）。
set -euo pipefail

# shellcheck disable=SC1091
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
cd "${CONTAINER_SCRIPT_ROOT}"

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
WANT="${1:-both}"

run_one() {
    local preset="$1"
    echo "=== ${preset}"
    "${SCRIPT_DIR}/build.sh" "${preset}"
    # 泄漏检测默认开：Qt 自身的常驻分配若被报出来，要在这里看清单而不是先关掉了事
    export ASAN_OPTIONS="halt_on_error=1:abort_on_error=0:detect_stack_use_after_return=1"
    export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
    export TSAN_OPTIONS="halt_on_error=0"
    "${SCRIPT_DIR}/test.sh" "${preset}"
}

case "${WANT}" in
    asan) run_one asan ;;
    tsan) run_one tsan ;;
    both) run_one asan; run_one tsan ;;
    *) echo "[失败] 只认 asan / tsan / both，收到：${WANT}" >&2; exit 2 ;;
esac
echo "[ok] sanitizer 门禁跑完（${WANT}）"
