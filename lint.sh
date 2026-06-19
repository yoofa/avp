#!/usr/bin/env bash
# =============================================================================
# lint.sh  —  clang-format + clang-tidy 一键检查脚本
#
# 用法：
#   ./lint.sh              # 检查模式（不修改文件，有问题退出码非 0）
#   ./lint.sh --fix        # 修复模式（自动应用 clang-format 和 tidy 修复）
#   ./lint.sh --format     # 仅格式化
#   ./lint.sh --tidy       # 仅静态检查
#   ./lint.sh --tidy --fix # 仅静态检查并自动修复
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# 颜色定义
# ---------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# ---------------------------------------------------------------------------
# 配置
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${SCRIPT_DIR}"

CLANG_FORMAT="${CLANG_FORMAT:-clang-format-23}"
CLANG_TIDY="${CLANG_TIDY:-clang-tidy-23}"
RUN_CLANG_TIDY="${RUN_CLANG_TIDY:-run-clang-tidy-23}"

# compile_commands.json（本地 x86-64 版本）
COMPILE_DB="${REPO_ROOT}/compile_commands_local.json"

# clang-tidy 只检查这些目录下的源文件（与 .clang-tidy HeaderFilterRegex 对应）
TIDY_DIRS=(
  "api"
  "base"
  "content_source"
  "core"
  "demuxer"
  "examples"
  "sdk"
)

# clang-format 排除路径
FORMAT_EXCLUDES=(
  "*/out/*"
  "*/third_party/*"
  "*/buildtools/*"
  "*/.git/*"
  "*/build/*"
)

# 并行度
JOBS="${JOBS:-$(nproc)}"

# ---------------------------------------------------------------------------
# 参数解析
# ---------------------------------------------------------------------------
DO_FORMAT=true
DO_TIDY=true
DO_FIX=false

for arg in "$@"; do
  case "$arg" in
    --fix) DO_FIX=true ;;
    --format)
      DO_FORMAT=true
      DO_TIDY=false
      ;;
    --tidy)
      DO_TIDY=true
      DO_FORMAT=false
      ;;
    --help | -h)
      sed -n '3,12p' "$0" | sed 's/^# //'
      exit 0
      ;;
    *)
      echo -e "${RED}Unknown option: $arg${RESET}"
      exit 1
      ;;
  esac
done

# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------
log_section() { echo -e "\n${BOLD}${CYAN}==== $* ====${RESET}"; }
log_ok() { echo -e "${GREEN}✔  $*${RESET}"; }
log_warn() { echo -e "${YELLOW}△  $*${RESET}"; }
log_error() { echo -e "${RED}✘  $*${RESET}"; }
log_info() { echo -e "   $*"; }

check_tool() {
  if ! command -v "$1" &>/dev/null; then
    log_error "未找到工具: $1"
    log_info "请安装: sudo apt-get install -y ${1%-*}"
    exit 1
  fi
}

# ---------------------------------------------------------------------------
# 工具版本检查
# ---------------------------------------------------------------------------
log_section "工具检查"
check_tool "$CLANG_FORMAT"
check_tool "$CLANG_TIDY"
check_tool "$RUN_CLANG_TIDY"
log_ok "$CLANG_FORMAT  $(${CLANG_FORMAT} --version | head -1)"
log_ok "$CLANG_TIDY   $(${CLANG_TIDY} --version | grep version | head -1 | xargs)"

if [[ "$DO_TIDY" == true && ! -f "$COMPILE_DB" ]]; then
  log_warn "compile_commands_local.json 不存在，尝试使用 compile_commands.json"
  COMPILE_DB="${REPO_ROOT}/compile_commands.json"
  if [[ ! -f "$COMPILE_DB" ]]; then
    log_error "找不到编译数据库，请先生成 compile_commands.json"
    exit 1
  fi
fi

# ---------------------------------------------------------------------------
# 收集 clang-format 目标文件
# ---------------------------------------------------------------------------
collect_format_files() {
  local exclude_args=()
  for ex in "${FORMAT_EXCLUDES[@]}"; do
    exclude_args+=("!" "-path" "$ex")
  done

  find "${REPO_ROOT}" -type f \
    \( -name "*.cc" -o -name "*.h" -o -name "*.cpp" -o -name "*.cxx" \) \
    "${exclude_args[@]}" |
    sort
}

# ---------------------------------------------------------------------------
# clang-format 检查 / 格式化
# ---------------------------------------------------------------------------
run_clang_format() {
  log_section "clang-format${DO_FIX:+ (--fix)}"

  local files
  files=$(collect_format_files)
  local file_count
  file_count=$(echo "$files" | wc -l)
  log_info "扫描文件数：$file_count"

  if [[ "$DO_FIX" == true ]]; then
    echo "$files" | xargs "${CLANG_FORMAT}" -i
    log_ok "已格式化 $file_count 个文件"
  else
    local violations
    violations=$(echo "$files" | xargs "${CLANG_FORMAT}" --style=file --dry-run --Werror 2>&1 || true)
    if [[ -z "$violations" ]]; then
      log_ok "所有文件格式符合规范"
    else
      log_error "发现格式违规："
      echo "$violations" |
        grep "error:" |
        sed "s|${REPO_ROOT}/||" |
        head -30
      local count
      count=$(echo "$violations" | grep -c "error:" || true)
      [[ $count -gt 30 ]] && log_warn "（共 $count 处，只显示前 30 条）"
      log_info "运行 ${BOLD}./lint.sh --format --fix${RESET} 自动修复"
      return 1
    fi
  fi
}

# ---------------------------------------------------------------------------
# clang-tidy 检查 / 修复
# ---------------------------------------------------------------------------
run_clang_tidy() {
  log_section "clang-tidy${DO_FIX:+ (--fix)}"

  # 构建文件路径正则：只分析 TIDY_DIRS 中的 .cc 文件
  local IFS='|'
  local dir_pattern="${TIDY_DIRS[*]}"
  unset IFS
  # dir_pattern 形如: api|content_source|core|demuxer|examples|sdk

  # 头文件过滤：只报告 TIDY_DIRS 中的头文件问题，排除 gen/（自动生成）
  local header_filter
  header_filter="^${REPO_ROOT}/(${dir_pattern})/(?!.*gen/).*"

  log_info "检查目录：${TIDY_DIRS[*]}"
  log_info "并行度：$JOBS"

  local fix_flag=""
  [[ "$DO_FIX" == true ]] && fix_flag="-fix"

  # -p "${COMPILE_DB}" \

  local tidy_output
  tidy_output=$(
    "${RUN_CLANG_TIDY}" \
      -j "${JOBS}" \
      ${fix_flag} \
      -quiet \
      -header-filter="${header_filter}" \
      -source-filter="${header_filter}" \
      "^${REPO_ROOT}/(${dir_pattern})/" \
      2>&1 || true
  )

  local errors
  errors=$(echo "$tidy_output" | grep -E "error:" | grep -v "^clang-tidy" || true)

  if [[ -z "$errors" ]]; then
    if [[ "$DO_FIX" == true ]]; then
      local fixed_count
      fixed_count=$(echo "$tidy_output" | grep -c "clang-tidy applied" || echo "0")
      log_ok "静态检查通过，已应用 $fixed_count 项自动修复"
    else
      log_ok "静态检查全部通过"
    fi
  else
    log_error "发现静态检查问题："
    echo "$errors" |
      sed "s|${REPO_ROOT}/||" |
      head -40
    local count
    count=$(echo "$errors" | wc -l)
    [[ $count -gt 40 ]] && log_warn "（共 $count 处，只显示前 40 条）"
    log_info "运行 ${BOLD}./lint.sh --tidy --fix${RESET} 尝试自动修复"
    return 1
  fi
}

# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------
cd "${REPO_ROOT}"

FAILED=0

if [[ "$DO_FORMAT" == true ]]; then
  run_clang_format || FAILED=1
fi

if [[ "$DO_TIDY" == true ]]; then
  run_clang_tidy || FAILED=1
fi

# ---------------------------------------------------------------------------
# 汇总
# ---------------------------------------------------------------------------
echo ""
if [[ $FAILED -eq 0 ]]; then
  echo -e "${BOLD}${GREEN}✔  All checks passed!${RESET}"
else
  echo -e "${BOLD}${RED}✘  Some checks failed. See above for details.${RESET}"
  exit 1
fi
