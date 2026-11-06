# Windows one-click build

**English** | [简体中文](README.zh-CN.md)

## Requirements

- Windows 10/11 x64
- Python 3.9+ (check "Add python.exe to PATH" during install)
- No dtc / fdtput and no cross compiler; DTB editing is pure Python.
- `prebuilt/windows/MultiBootKernelPatcher.exe` (x86-64) ships with the repo.

## Usage

1. Prepare two files:
   - the stock `boot_a.img` (full partition dump or a boot image read via
     `fastboot`);
   - the distro `boot.img` (for example Debian's `boot-forky-...img`).
2. Drag both onto `一键构建.bat` (stock first, distro second), or use the
   command line:

   ```
   一键构建.bat stock_boot.img distro_boot.img out\boot_a_win.img
   ```

3. The output defaults to `out\boot_a_win.img`. Flash it:

   ```
   fastboot flash boot_a out\boot_a_win.img
   ```

The script automatically adds
`root=PARTLABEL=linux rw rootwait udmabuf.size_limit_mb=256`
(the `udmabuf` parameter required by the Debian image). For other distros:

```
python windows\build.py --stock-boot stock.img --distro-boot boot.img ^
  --bootargs "root=PARTLABEL=linux rw rootwait"
```

`--default linux`, `--timeout-ms 30000` and the other options match
`scripts/make-boot.sh` on Linux.

## Notes

- The stock kernel must come from a GKI v3/v4 boot image; the script checks the
  kernel header.
- A distro boot.img may contain `Image.gz` + appended DTB (mkbootimg v0), or you
  can pass `--linux-image Image --dtb sheng.dtb` directly.
- The output boot image must be smaller than 192 MiB (sheng's boot partition);
  the script enforces this.
