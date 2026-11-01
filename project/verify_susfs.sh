#!/bin/bash
# SuSFS 编译产物验收脚本
#
# 用法: ./verify_susfs.sh <构建目录> [日志路径] [boot.img 路径]
# 示例: ./verify_susfs.sh out-mytest logs/build_mytest.log dist/boot_warsaw_mytest.img
set -u

OUT=${1:?用法: $0 <构建目录> [日志] [boot.img]}
LOG=${2:-}
BOOT=${3:-}
IMG=$OUT/arch/arm64/boot/Image
VMLINUX=$OUT/vmlinux

pass=0; fail=0
chk() { # chk <名称> <条件命令...>
  local name="$1"; shift
  if eval "$@"; then echo "  [OK]   $name"; pass=$((pass+1));
  else echo "  [FAIL] $name"; fail=$((fail+1)); fi
}

echo "===== 1. 编译错误 ====="
if [ -n "$LOG" ] && [ -f "$LOG" ]; then
  n=$(grep -cE 'error:' "$LOG" || true)
  chk "error 数为 0（实际 $n）" "[ \"$n\" = \"0\" ]"
else
  echo "  [SKIP] 未提供编译日志"
fi

echo "===== 2. 产物 ====="
chk "Image 存在" "[ -f \"$IMG\" ]"
chk "vmlinux 存在" "[ -f \"$VMLINUX\" ]"

echo "===== 3. SuSFS 符号 ====="
if [ -f "$VMLINUX" ]; then
  n=$(nm "$VMLINUX" 2>/dev/null | grep -cE ' [tTdDbB] susfs_' || true)
  echo "  susfs_* 符号数: $n"
  chk "符号数 > 0" "[ \"$n\" -gt 0 ]"
  for s in susfs_init susfs_add_sus_path susfs_set_batch_sid; do
    chk "$s 存在" "nm \"$VMLINUX\" 2>/dev/null | grep -qE ' [tTdDbB] $s$'"
  done
fi

echo "===== 4. 内核版本串 ====="
if [ -f "$IMG" ]; then
  ver=$(strings -a "$IMG" | grep -oE 'Linux version [^ ]+' | head -1)
  echo "  $ver"
  chk "含 android15-8" "echo \"$ver\" | grep -q 'android15-8'"
fi

echo "===== 5. boot.img（如提供）====="
if [ -n "$BOOT" ] && [ -f "$BOOT" ]; then
  sz=$(stat -c %s "$BOOT")
  echo "  大小: $sz 字节"
  chk "大小为 100663296" "[ \"$sz\" = \"100663296\" ]"
else
  echo "  [SKIP] 未提供 boot.img"
fi

echo
echo "结果: 通过 $pass 项，失败 $fail 项"
[ "$fail" = "0" ]