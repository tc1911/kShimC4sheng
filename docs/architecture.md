# 架构

## 1. 镜像布局

`scripts/build.sh` 产出的是完整的 `boot_a` 分区镜像（v4 头 + 打包后的内核）。
MBKP 把各条目按 2 MiB / 8 B 对齐后拼接，manifest 放在最后：

| packed offset | 条目 | 类型 | flags | 内容 |
|---|---|---|---|---|
| 0x0 | Android | Linux | `BASE_IMAGE` + `EXECUTABLE` | 原厂 stock 内核（raw Image） |
| 0x2000 | Attached Shim | Shim | `EXECUTABLE` | 4 MiB kShimC runtime |
| 0x600000 | Linux | Linux | `EXECUTABLE` + `HAS_DTB` | 开源内核 Image（raw） |
| 0x602000 | sheng-dtb | DTB | — | 已改写 `/chosen/bootargs` 的 DTB |
| 0x6020xx | initrd | Blob | — | 可选 initrd（Armada 有，Debian 没有） |
| 末尾 | — | Manifest | — | 条目表 + 原厂头备份 |

偏移随载荷大小变化，上表是合成测试镜像的示例。manifest 的
`base_image` 备份的是**原厂**内核头的前 5 个 u64：

```
0x00 instructions    0x10 image_size    0x20 manifest_offset
0x28 manifest_size   0x30 shim_offset
```

打包后的内核头被改成：

```
0x00  BL <shim>              0x04  原厂第一条指令
0x10  packed size            0x20  manifest offset
0x28  manifest size          0x30  shim offset
```

## 2. 启动流程

```
ABL 读 boot_a
  └─ 加载 packed 内核到 kernel load address，跳到 offset 0（BL shim）
       └─ kShimC 保存现场、读 manifest、恢复原厂头、画菜单（8 s）
            ├─ Android（默认）
            │    ├─ 恢复原厂头 5 个 u64
            │    ├─ 使用 ABL 传入的 DT 指针（dtbo_a）
            │    ├─ init_boot_a / vendor_boot_a 已由 ABL 加载
            │    └─ 跳原厂 kernel entry（entry_offset 0）
            └─ Linux
                 ├─ 把 Image / DTB / initrd 放到 manifest 的 load address
                 ├─ 以 DTB 的 /chosen/bootargs 启动
                 │    root=PARTLABEL=linux rw rootwait
                 └─ initrd 存在时 /chosen/kshim,initrd-entry = <条目号>
```

选 Android 不重启、不改 `misc`、不切槽，所以 Android 看到的
`ro.boot.slot_suffix` 仍是 `_a`，`super`/`userdata` 也都在 A 槽。

## 3. 为什么不用 boot_b

现有社区方案把 Linux boot 刷进 `boot_b`、擦 `dtbo_b`、`set_active b`。
如果 shim 放进 `boot_b`：

- ABL 会加载 `init_boot_b`/`vendor_boot_b`，与 Android 的 `_a` 系统不匹配；
- 要启动 Android 必须伪造 `slot_suffix=_a` 或依赖 `system_b`；
- 切回 Android 仍然要改 BCB/槽位。

boot_a 方案把“选 Android”变成一次普通的内核跳转，ABL 的槽位始终是 A，
不需要 `misc` 写入，也不需要 root。

## 4. 容量

| 项 | 实测/估计 |
|---|---|
| runtime | 4.00 MiB（固定槽位） |
| stock Android 内核 | 20–45 MiB（GKI，待实测） |
| Linux Image（Armada） | 47.07 MiB |
| initrd（Armada） | 43.40 MiB |
| DTB | 0.14 MiB |
| 合计 | 95–140 MiB |
| boot 分区 | 192 MiB |

`build.sh` 会在打包后检查 `boot 头 + packed ≤ 分区 - 64 KiB`，超限直接失败。

## 5. A/B 与 OTA

- shim 只写 `boot_a`；`boot_b` 保持原样（可留原厂 boot 作应急）。
- 设备必须处于 A 槽，否则 ABL 根本不加载 `boot_a`。
- Android OTA 更新 `boot_a` 后会覆盖 shim；若 OTA 切到 B 槽，菜单暂时消失，
  需要重新 `set_active a` 并重刷。
- `dtbo_a` 不动，Android 的 DT 仍由原厂 dtbo 提供；Linux 用内嵌 DTB。
