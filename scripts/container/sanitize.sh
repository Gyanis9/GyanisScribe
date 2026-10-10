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

# TSan 起不来时先量环境，别让门禁把「没跑」当成「跑过了」。
# 这台内核（WSL2 6.18）的 mmap 布局要让 TSan 能用，必须关掉 ASLR；而关 ASLR 走的
# personality(ADDR_NO_RANDOMIZE) 又被 Docker 自带的 seccomp 档挡住（实测 EPERM）。
# 恢复条件：容器要以 --security-opt seccomp=unconfined 启动，本函数就会放行。
require_tsan_capability() {
    if setarch "$(uname -m)" -R true >/dev/null 2>&1; then
        return 0
    fi
    {
        echo "[失败] 本环境跑不了 TSan：setarch -R 被拒（personality 需要 seccomp 放行），"
        echo "         而 TSan 在这台内核上不开 ASLR 关闭就无法启动进程。"
        echo "         恢复办法：用 docker run --security-opt seccomp=unconfined 起容器再跑本脚本的 tsan 档。"
        echo "         ASan(+UBSan) 不受影响，仍是跨平台的硬门禁。"
    } >&2
    exit 1
}

run_one() {
    local preset="$1"
    local -a prefix=()
    echo "=== ${preset}"
    if [ "${preset}" = tsan ]; then
        # 先确认环境给得起关 ASLR，再动手建；跑不动就当场停，不让它绿
        require_tsan_capability
        # 内核的默认 mmap 布局下 TSan 连进程都起不来（FATAL: unexpected memory mapping），
        # 与代码无关；setarch -R 关掉 ASLR 是这类容器里的标准做法。
        # 包的是「建 + 跑」两步而不是只包 ctest：gtest_discover_tests 默认在构建期真跑一遍
        # 可执行体来枚举用例，那一步排在 ctest 之前（实测只包 ctest 时 FAILED: bin/TestSettings）
        prefix=(setarch "$(uname -m)" -R)
    fi
    # sanitizer 的运行期参数（含 LSan 抑制表）在 scripts/container/env.sh 里统一导出，
    # 这里不重复一份：两份副本会各自漂移，而 build.sh/test.sh 直接跑 asan 档时也要吃到同一份
    "${prefix[@]}" "${SCRIPT_DIR}/build.sh" "${preset}"
    "${prefix[@]}" "${SCRIPT_DIR}/test.sh" "${preset}"
}

case "${WANT}" in
    asan) run_one asan ;;
    tsan) run_one tsan ;;
    both) run_one asan; run_one tsan ;;
    *) echo "[失败] 只认 asan / tsan / both，收到：${WANT}" >&2; exit 2 ;;
esac
echo "[ok] sanitizer 门禁跑完（${WANT}）"
