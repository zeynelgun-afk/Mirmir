# Linux installation

Target: x86_64, glibc 2.34 or newer, and a Vulkan-capable graphics driver.
Package formats are provided for Ubuntu 22.04+, Debian 12+ and current
Fedora, openSUSE and Arch-family systems. Consult the release verification
records for the environments actually tested. Ubuntu 20.04 and RHEL 8 are
outside the current Qt runtime's compatibility target.

Replace `VERSION` in the commands below with the downloaded release version.

| Distribution family | Package | Install command |
| --- | --- | --- |
| Debian / Ubuntu | `.deb` | `sudo apt install ./mirmir_VERSION-1_amd64.deb` |
| Fedora | `.rpm` | `sudo dnf install ./mirmir-VERSION-1.x86_64.rpm` |
| openSUSE | `.rpm` | `sudo zypper install ./mirmir-VERSION-1.x86_64.rpm` |
| Arch / CachyOS / Manjaro | `.pkg.tar.zst` | `sudo pacman -U ./mirmir-VERSION-1-x86_64.pkg.tar.zst` |

Then run `mirmir` or use the desktop application launcher. These packages
install the application under `/opt/mirmir`, with a launcher and `mirmir`
command in the standard system directories. Removing the package does not
remove your cat save.

For AppImage:

```bash
chmod +x Mirmir-VERSION-x86_64.AppImage
./Mirmir-VERSION-x86_64.AppImage
```

If FUSE is unavailable, use the runtime's `--appimage-extract-and-run` option.
The [Flatpak guide](FLATPAK.md) covers the separate direct-download bundle.

For the portable archive, extract into a user-owned directory:

```bash
mkdir -p Mirmir
tar --zstd -xf Mirmir-VERSION-linux-x86_64.tar.zst -C Mirmir
cd Mirmir
./mirmir --diagnose
./mirmir
```

`--diagnose` checks the private Python/Qt runtime, relevant Vulkan/X11 libraries
and the cooked game file without starting the engine. `./mirmir --stop` requests
normal shutdown of your owned running instance. Saves use the XDG user data
directory; logs use the XDG state directory. Temporary frames use a private
runtime tmpfs directory when available. User records are not written inside
the installed bundle.

On Wayland, the Qt shell uses XWayland for its own process. It does not switch
your session type or edit KWin settings. Graphics drivers, sound servers,
tray integration and actual desktop visibility still require verification in
the target desktop environment. A locked session can prevent window mapping
while the engine continues to render; no lock-screen bypass is attempted.
