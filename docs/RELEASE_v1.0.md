# v1.0 发布说明

**内核版本**：`6.6.142-android15-8-wjljh-2026y11m20d-nova-kpm-4k`

| 文件 | md5 |
|---|---|
| `v1.0-boot_warsaw_6.6.142-android15-8-wjljh-2026y11m20d-4k.img` | `d1ecb6808618fc7592fecc010de794a8` |
| `v1.0-Anykernel3_for_warsaw_SukiSU-Nova_6.6.142-android15-8-wjljh-2026y11m20d-4k.zip` | `bf61f29e09b3a374e96b7a41a23e63f5` |

## 组成

| 组件 | 版本 |
|---|---|
| 内核 | AOSP GKI **6.6.142**（含 Xiaomi vendor hooks，622 个 `android_vh_*`） |
| SukiSU | **Nova v3.0.0**（KSU 35048） |
| SuSFS | **v2.4.0**（完整 27 文件移植） |
| KernelPatch | **e00**（已注入内核镜像） |

## 特性

- **KPM 可用** —— KernelPatch 已注入，`/data/adb/kpm/*.kpm` 开机自动加载
- **SuSFS 2.4.0** —— 路径隐藏、uname 伪装、kstat 伪造、SUS_MAP/SUS_MOUNT 等
- **Zygisk / LSPosed** 正常
- 保留原厂 AVB 结构，boot 大小 100,663,296 字节与原厂一致

## 刷入

```bash
# fastboot
fastboot flash boot v1.0-boot_warsaw_6.6.142-android15-8-wjljh-2026y11m20d-4k.img

# 或在 recovery / 管理器里刷 AK3
```

## 验证

```bash
su -c 'uname -r'                        # 6.6.142-android15-8-wjljh-2026y11m20d-nova-kpm-4k
su -c '/data/adb/ksud kpm list'          # 已加载的 KPM 模块
su -c '/data/adb/ksud extension check kpm'  # supported
ps -A | grep lspd                        # LSPosed 运行中
```

## 本版修复的两个关键问题

移植 SuSFS 2.4.0 时遇到的符号冲突（详见 `patches/susfs-nova-2.4.0/README.md`）：

1. **`ksu_su_compat_enabled` 类型不匹配** —— 内核按 16 字节 static_key 访问，Nova 定义成 1 字节 bool → 野指针 panic
2. **`ksu_handle_execveat_sucompat` 签名不匹配** —— 内核 5 参 vs Nova 3 参 → 把 `&flags` 当 `pt_regs *` → panic

## 已知限制

- 部分 SuSFS 钩子走 Nova 的 syscall 层而非 fs 层（重复处理会冲突），因此兼容层里对应函数为 no-op
- 仅 REDMI K90 Ultra（warsaw / M332BF）验证过
