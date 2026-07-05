#!/usr/bin/env bash
# ============================================================
# format-check.sh — 按模块批量检查/格式化代码
#
# 用法:
#   ./scripts/format-check.sh              # dry-run，预览所有模块改动（不改文件）
#   ./scripts/format-check.sh core         # 只检查 core 模块
#   ./scripts/format-check.sh core view    # 检查多个模块
#   ./scripts/format-check.sh --apply      # 实际格式化所有模块
#   ./scripts/format-check.sh core --apply # 实际格式化 core 模块
#
# 模块名可写短名（core）或路径（src/core / tests）。
# 规则: 使用项目根目录的 .clang-format
# ============================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

# --- 参数解析 ---
APPLY=0
MODULES=()
for arg in "$@"; do
    case "$arg" in
        --apply|-a) APPLY=1 ;;
        -h|--help)
            sed -n '2,13p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *) MODULES+=("$arg") ;;
    esac
done

# 默认: 全部 src 子目录 + tests
if [ ${#MODULES[@]} -eq 0 ]; then
    for d in src/*/ ; do MODULES+=("${d%/}"); done
    [ -d tests ] && MODULES+=("tests")
fi

# 把短名（core）解析成路径（src/core）
resolve_module() {
    local m="$1"
    [ -d "$m" ] && { echo "$m"; return; }
    [ -d "src/$m" ] && { echo "src/$m"; return; }
    echo "$m"
}

# --- 找 clang-format ---
CF="${CLANG_FORMAT:-clang-format}"
if ! command -v "$CF" >/dev/null 2>&1; then
    for p in "/c/Program Files/LLVM/bin/clang-format.exe"; do
        [ -x "$p" ] && CF="$p" && break
    done
fi
if ! command -v "$CF" >/dev/null 2>&1 && [ ! -x "$CF" ]; then
    echo "ERROR: clang-format 未找到。请设 CLANG_FORMAT 环境变量或加入 PATH。" >&2
    exit 1
fi

# --- 收集某模块下所有源文件 ---
collect_files() {
    local dir="$1"
    [ -d "$dir" ] || return 0
    find "$dir" -type f \( -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" \
                              -o -name "*.h"   -o -name "*.hh" -o -name "*.hpp" \) 2>/dev/null
}

# --- 单模块处理 ---
process_module() {
    local mod_input="$1"
    local mod
    mod="$(resolve_module "$mod_input")"
    local files
    files="$(collect_files "$mod")"
    if [ -z "$files" ]; then
        echo "  [跳过] $mod_input (->$mod): 无源文件"
        return
    fi

    local total changed=0 viol_files=""
    total=$(echo "$files" | wc -l)

    # --dry-run 输出违规行（格式: file:line:col: warning/error ...）
    # 注意: 退出码恒为 0，必须看输出内容
    while IFS= read -r f; do
        if "$CF" --dry-run "$f" 2>&1 | grep -q "clang-format-violation"; then
            viol_files="${viol_files}
$f"
            changed=$((changed + 1))
        fi
    done <<< "$files"

    echo "  $mod_input (-> $mod): 共 $total 个文件，$changed 个需要格式化"

    if [ "$APPLY" -eq 1 ] && [ "$changed" -gt 0 ]; then
        echo "$viol_files" | grep -v '^$' | tr '\n' ' ' | xargs "$CF" -i
        echo "    -> 已格式化 $changed 个文件"
    elif [ "$changed" -gt 0 ] && [ "$APPLY" -eq 0 ]; then
        echo "    预览改动（前 3 个文件，每文件前 12 行违规）:"
        local shown=0
        while IFS= read -r f; do
            [ -z "$f" ] && continue
            [ "$shown" -ge 3 ] && break
            echo "      --- $f ---"
            "$CF" --dry-run "$f" 2>&1 | head -12 | sed 's/^/      /'
            shown=$((shown + 1))
        done <<< "$viol_files"
        echo "    （加 --apply 执行实际格式化）"
    fi
}

# --- 主流程 ---
echo "============================================================"
if [ "$APPLY" -eq 1 ]; then
    echo "  模式: 实际格式化 (--apply)"
else
    echo "  模式: 预览 (dry-run，不改文件)"
fi
echo "  项目: $ROOT"
echo "  规则: $ROOT/.clang-format"
echo "  模块: ${MODULES[*]}"
echo "============================================================"

for mod in "${MODULES[@]}"; do
    process_module "$mod"
done

echo "============================================================"
echo "完成。"
