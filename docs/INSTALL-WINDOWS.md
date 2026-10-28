# Windows installer

The Windows target is x64 Windows 10/11. The Inno Setup recipe installs per
user under `%LOCALAPPDATA%\Programs\Mirmir`, creates Start menu shortcuts and
an optional desktop shortcut, and preserves saves during removal. It does not
request administrative privileges or enable autostart. A completed Windows
bundle would include the game and Python/Qt runtime for the end user.

**A Windows Unreal Shipping build and installer EXE have not been produced or
validated on this Linux host.** The installer source and build scripts require
a real cooked Win64 payload. Until an EXE appears in a verified release, there
is no prebuilt Windows download to install.

A Windows build machine needs a separately licensed Unreal Engine 5.8.4
installation, compatible Visual Studio C++ tools, Python 3.12 and Inno Setup 6.
From the repository root:

```powershell
./packaging/windows/build-all.ps1 -UnrealRoot 'C:\UnrealEngine-5.8.4'
```

The script builds the Win64 editor module and Shipping game, freezes the Qt
shell with PyInstaller, runs the frozen shell's self-test, then invokes Inno
Setup. A failed prerequisite or build step stops installer creation. The
version comes from `packaging/VERSION` and must match the cooked project
version. Engine credentials and the SDK are build-machine requirements;
end users would not need them.

The wizard source is [Mirmir.iss](../packaging/windows/Mirmir.iss). The
[manual build workflow](../.github/workflows/windows-installer.yml) uses a
trusted self-hosted Windows build runner.

The first real Windows release still needs checks for the graphics backend,
sound, transparent windows, input handling, normal shutdown, upgrades and save
restoration. An unsigned EXE does not have a verified Windows publisher identity.
