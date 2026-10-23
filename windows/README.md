# Windows 一键构建

## 依赖

- Windows 10/11 x64
- Python 3.9+（安装时勾选 “Add python.exe to PATH”）
- 不需要 dtc / fdtput，也不需要交叉编译器；DTB 编辑已改为纯 Python。
- `prebuilt/windows/MultiBootKernelPatcher.exe` 已随仓库提供（x86-64）。

## 用法

1. 准备两个文件：
   - 原厂 `boot_a.img`（整块分区 dump 或 `fastboot` 读出的 boot 镜像）
   - 发行版 `boot.img`（例如 Debian 的 `boot-forky-...img`）
2. 把这两个文件一起拖到 `一键构建.bat` 上（第一个是原厂，第二个是发行版），
   或者命令行：
   ```
   一键构建.bat stock_boot.img distro_boot.img out\boot_a_win.img
   ```
3. 产物默认 `out\boot_a_win.img`，刷入：
   ```
   fastboot flash boot_a out\boot_a_win.img
   ```

脚本会自动带上 `root=PARTLABEL=linux rw rootwait udmabuf.size_limit_mb=256`
（Debian 镜像需要的 `udmabuf` 参数）。其它发行版可用：

```
python windows\build.py --stock-boot stock.img --distro-boot boot.img ^
  --bootargs "root=PARTLABEL=linux rw rootwait"
```

`--default linux`、`--timeout-ms 30000` 等参数与 Linux 版 `scripts/make-boot.sh` 相同。

## 说明

- 原厂内核必须从 GKI v3/v4 boot 镜像中取；脚本会检查内核头。
- 发行版 boot.img 支持 `Image.gz + 追加 DTB`（mkbootimg v0），也支持直接给
  `--linux-image Image --dtb sheng.dtb`。
- 输出的 boot 镜像必须小于 192 MiB（sheng 的 boot 分区），脚本会校验。
