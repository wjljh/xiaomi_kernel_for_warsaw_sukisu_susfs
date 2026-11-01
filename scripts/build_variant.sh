#!/bin/bash
# 内核变体构建脚本：一条命令完成 配置变更 + 增量编译 + 打包 + 校验
#
# 用法:
#   ./build_variant.sh <变体名> [--base <基础构建目录>] [--enable SYM]... [--disable SYM]...
#                      [--localver <字符串>] [--clean]
#
# 需要的环境变量:
#   CLANG_DIR    clang 工具链根目录（例如 .../clang-r522817）
#   KBT_DIR      kernel-build-tools 根目录（提供 pahole）
#   KERNEL_SRC   内核源码目录，默认 <仓库根>/kernel
#   BOOT_TEMPLATE 可选，原厂 boot.img；设置后自动打包
#
# 增量原理: 从 --base 指定的已完成构建目录复制一份，只重编受配置影响的文件。
#           实测改 KSU 开关约 110 秒（clean build 约 320 秒）。
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

KERNEL_SRC=${KERNEL_SRC:-$REPO_ROOT/kernel}
OUT_ROOT=${OUT_ROOT:-$REPO_ROOT/out}
LOG_ROOT=${LOG_ROOT:-$REPO_ROOT/logs}
DIST_DIR=${DIST_DIR:-$REPO_ROOT/dist}

: "${CLANG_DIR:?请设置 CLANG_DIR 指向 clang 工具链}"
: "${KBT_DIR:?请设置 KBT_DIR 指向 kernel-build-tools}"
export PATH=$CLANG_DIR/bin:$KBT_DIR/bin:$PATH

NAME=""
BASEDIR=""
LOCALVER=""
CLEAN=0
OPTFILE=$(mktemp)
: > "$OPTFILE"

while [ $# -gt 0 ]; do
  case "$1" in
    --base)     BASEDIR="$2"; shift 2 ;;
    --enable)   echo "--enable $2"  >> "$OPTFILE"; shift 2 ;;
    --disable)  echo "--disable $2" >> "$OPTFILE"; shift 2 ;;
    --localver) LOCALVER="$2"; shift 2 ;;
    --clean)    CLEAN=1; shift ;;
    *)          NAME="$1"; shift ;;
  esac
done

if [ -z "$NAME" ]; then
  echo "用法: $0 <变体名> [--base <目录>] [--enable SYM]... [--disable SYM]... [--localver <str>] [--clean]"
  rm -f "$OPTFILE"
  exit 1
fi

mkdir -p "$OUT_ROOT" "$LOG_ROOT" "$DIST_DIR"
OUT=$OUT_ROOT/$NAME
LOG=$LOG_ROOT/build_$NAME.log
: > "$LOG"

cd "$KERNEL_SRC" || { echo "错误: 找不到内核源码 $KERNEL_SRC"; exit 1; }

if [ "$CLEAN" = "1" ]; then
  echo ">>> clean 构建（不使用已有产物）" | tee -a "$LOG"
  rm -rf "$OUT"; mkdir -p "$OUT"
  DEFCONFIG=$KERNEL_SRC/arch/arm64/configs/gki_defconfig
  if [ -f "$DEFCONFIG" ]; then
    cp "$DEFCONFIG" "$OUT/.config"
  else
    echo "错误: 找不到 gki_defconfig，--clean 需要一个基础 .config"
    rm -f "$OPTFILE"; exit 1
  fi
elif [ -n "$BASEDIR" ] && [ -d "$OUT_ROOT/$BASEDIR" ]; then
  echo ">>> 增量构建：复制 $BASEDIR 的已有产物" | tee -a "$LOG"
  rm -rf "$OUT"
  cp -a "$OUT_ROOT/$BASEDIR" "$OUT"
elif [ -n "$BASEDIR" ] && [ -d "$BASEDIR" ]; then
  echo ">>> 增量构建：复制 $BASEDIR 的已有产物" | tee -a "$LOG"
  rm -rf "$OUT"
  cp -a "$BASEDIR" "$OUT"
else
  echo ">>> 复用已存在的 $OUT" | tee -a "$LOG"
fi

CFG=$OUT/.config
if [ ! -f "$CFG" ]; then echo "错误: 找不到 $CFG"; rm -f "$OPTFILE"; exit 1; fi

while IFS= read -r o; do
  [ -n "$o" ] || continue
  echo ">>> scripts/config $o" | tee -a "$LOG"
  scripts/config --file "$CFG" $o
done < "$OPTFILE"
rm -f "$OPTFILE"

if [ -n "$LOCALVER" ]; then
  scripts/config --file "$CFG" --set-str LOCALVERSION "$LOCALVER"
fi

make ARCH=arm64 LLVM=1 O="$OUT" olddefconfig >> "$LOG" 2>&1
if [ $? != 0 ]; then echo "olddefconfig 失败"; tail -5 "$LOG"; exit 1; fi
echo ">>> 生效配置:" | tee -a "$LOG"
grep -E '^CONFIG_(KSU|KPM|KSU_DEBUG)=|^# CONFIG_(KSU|KPM|KSU_DEBUG) is not set' "$CFG" | tee -a "$LOG"
if [ -n "$LOCALVER" ]; then grep -E '^CONFIG_LOCALVERSION=' "$CFG" | tee -a "$LOG"; fi

S=$(date +%s)
make ARCH=arm64 LLVM=1 O="$OUT" -j$(nproc) >> "$LOG" 2>&1
EXIT=$?
E=$(date +%s)
echo ">>> MAKE_EXIT=$EXIT  耗时 $((E-S)) 秒" | tee -a "$LOG"

IMG=$OUT/arch/arm64/boot/Image
if [ "$EXIT" != "0" ] || [ ! -f "$IMG" ]; then
  echo "!!! 编译失败，错误摘要:"
  grep -E 'error:' "$LOG" | head -10
  exit 1
fi
ls -la "$IMG" | tee -a "$LOG"

if [ -n "${BOOT_TEMPLATE:-}" ] && [ -f "${BOOT_TEMPLATE}" ]; then
  BOOT=$DIST_DIR/boot_warsaw_$NAME.img
  rm -f "$BOOT"
  python3 "$SCRIPT_DIR/pack_boot.py" pack "$BOOT_TEMPLATE" "$IMG" "$BOOT" 2>&1 \
    | grep -E '打包完成|新 vbmeta|文件大小|冲突|错误' | tee -a "$LOG"
  echo ">>> MD5:" | tee -a "$LOG"
  md5sum "$BOOT" | tee -a "$LOG"
else
  echo ">>> 跳过打包（未设置 BOOT_TEMPLATE）"
fi

echo ">>> 内核版本: $(strings -a "$IMG" | grep -oE 'Linux version [^ ]+' | head -1)"
echo ">>> 完成: $IMG"