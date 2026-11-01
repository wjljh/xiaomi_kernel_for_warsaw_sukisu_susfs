# ak3/

本项目用 [AnyKernel3](https://github.com/osm0sis/AnyKernel3) 打包成 TWRP 可刷的 zip。

> 由于 AK3 模板内含 busybox / magiskboot 等二进制，本仓库**不附带**完整模板，只记录改动点。

## 模板结构

```
anykernel.sh
banner
Image                       # 编译出的内核
LICENSE
META-INF/com/google/android/{update-binary,updater-script}
tools/{ak3-core.sh,busybox,magiskboot,magiskpolicy,...}
```

## anykernel.sh 改动

| 配置项 | 值 | 说明 |
|---|---|---|
| `kernel.string` | `Linux version <内核版本串>` | 刷入时显示 |
| `do.devicecheck` | `0` | 不做机型校验 |
| `do.modules` | `0` | 不安装模块 |
| `do.systemless` | `0` | |
| `do.cleanup` | `1` | |
| `block` | `boot` | 写入 boot 分区 |
| `is_slot_device` | `auto` | |
| `ramdisk_compression` | `auto` | |
| `patch_vbmeta_flag` | `auto` | |
| `no_magisk_check` | `1` | 跳过 Magisk 检测 |

并保留 GKI 版本检查：

```sh
if [ "$(echo $kernel_version | cut -d. -f1-2)" = "6.6" ]; then
  ksu_supported=true
fi
```

## 打包步骤

```bash
cp out-mytest/arch/arm64/boot/Image ak3/Image
# 修改 anykernel.sh 里的 kernel.string 与中文提示
cd ak3 && zip -r ../AnyKernel3_warsaw.zip \
    anykernel.sh banner Image LICENSE META-INF tools
```

## 刷入

TWRP → 安装 → 选择 zip → 滑动刷入。

回滚：`fastboot flash boot <原厂 boot.img>`。
