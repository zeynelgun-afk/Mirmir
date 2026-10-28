# Install Mirmir

Download a package from [GitHub Releases](https://github.com/zeynelgun-afk/Mirmir/releases).
The Linux bundles include the cooked Unreal game, Python and Qt; the Unreal
Editor is not required to run them. Your system must provide a Vulkan graphics
driver and the basic X11/XWayland desktop libraries. Linux packages target
x86_64 systems.

- [Linux packages, AppImage and portable archive](INSTALL-LINUX.md)
- [Direct Flatpak distribution](FLATPAK.md)
- [Windows installer source and build status](INSTALL-WINDOWS.md)
- [Rebuild the packages](../packaging/README.md)

Download `SHA256SUMS` beside your chosen artifact and verify it:

```bash
sha256sum --ignore-missing -c SHA256SUMS
```

Each release includes verification records. They distinguish package creation,
headless installation checks, actual game rendering and desktop visibility;
a package format alone does not establish compatibility with every GPU or
Linux distribution.

Petting, feeding, play, appearance and normal shutdown are available through
the tray or the cat's context menu. Use **Quit** (`Kapat` in the current Turkish UI) to save and close the running
instance before opening an upgraded version. Installation does not enable
autostart or change your desktop layout, session or window-manager settings.
Normal removal preserves the cat's user data.

Project code, the Qt shell, Unreal runtime and cat assets have separate
licenses. The included cat model retains its noncommercial condition. Read
[ASSET-LICENSES.md](../ASSET-LICENSES.md) and the bundle's `licenses/` directory.
