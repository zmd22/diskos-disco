# Windows app source

Source for `diskOS-Disco.exe`, the Windows installer GUI. It runs the Linux installer inside a private WSL distro and
attaches the Disc via usbipd-win. User guide: [docs/WINDOWS.md](../docs/WINDOWS.md).

- **Build the app** (on Linux, needs `mono-devel`): `sh build.sh`. Output is a single EXE targeting the .NET Framework
  bundled with Windows. Builds are unsigned, so SmartScreen requires **More info › Run anyway**.
- **Build the WSL environment:** `sudo sh wsl/build-env.sh` writes `diskos-wsl.tar.gz` (minimal Ubuntu 24.04,
  installer, Python deps, static usbboot, current payload) to `out/`. The distro runs as root, so no udev rule is needed.
