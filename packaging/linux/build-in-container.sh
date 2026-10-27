#!/bin/sh
set -eu
: "${VERSION:?VERSION required}"
root=/tmp/mirmir-package
mkdir -p "$root/opt" "$root/usr/bin" "$root/usr/share/applications" "$root/usr/share/icons/hicolor/scalable/apps" /out
cp -a /stage "$root/opt/mirmir"
ln -s /opt/mirmir/mirmir "$root/usr/bin/mirmir"
cp /recipes/linux/mirmir.desktop "$root/usr/share/applications/"
cp /stage/assets/icon.svg "$root/usr/share/icons/hicolor/scalable/apps/mirmir.svg"
mkdir -p "$root/DEBIAN"
size=$(du -sk "$root/opt" | cut -f1)
cat > "$root/DEBIAN/control" <<EOF
Package: mirmir
Version: $VERSION-1
Architecture: amd64
Maintainer: Mirmir contributors <noreply@github.com>
Section: games
Priority: optional
Installed-Size: $size
Depends: libc6 (>= 2.34), libstdc++6 (>= 11), libvulkan1, libglib2.0-0, libegl1, libgl1, libzstd1, zlib1g, libx11-6, libx11-xcb1, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-render-util0, libxcb-randr0, libxcb-render0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-xfixes0, libxcb-xinerama0, libxcb-xkb1, libxkbcommon-x11-0, libfontconfig1, libfreetype6, libpulse0, libasound2, libdbus-1-3
Recommends: xwayland
Homepage: https://github.com/zeynelgun-afk/Mirmir
Description: Unreal-rendered desktop cat companion
 Includes cooked Unreal runtime and a private Python/Qt shell.
 Cat assets are for personal noncommercial use; see included attribution.
EOF
dpkg-deb --root-owner-group --threads-max=2 -Zzstd --build "$root" "/out/mirmir_${VERSION}-1_amd64.deb"
dpkg-deb --info "/out/mirmir_${VERSION}-1_amd64.deb" > /out/deb-metadata.txt
rm -rf "$root/DEBIAN"
mkdir -p /tmp/rpmbuild/SPECS
cat > /tmp/rpmbuild/SPECS/mirmir.spec <<EOF
Name: mirmir
Version: $VERSION
Release: 1
Summary: Unreal-rendered desktop cat companion
License: MIT AND GPL-3.0-only AND LGPL-3.0-only AND CC-BY-NC-4.0 AND CC-BY-4.0 AND LicenseRef-UnrealEngine
URL: https://github.com/zeynelgun-afk/Mirmir
BuildArch: x86_64
AutoReqProv: no
Requires: libc.so.6(GLIBC_2.34)(64bit), libstdc++.so.6(GLIBCXX_3.4.29)(64bit), libvulkan.so.1()(64bit), libglib-2.0.so.0()(64bit), libEGL.so.1()(64bit), libGL.so.1()(64bit), libzstd.so.1()(64bit), libz.so.1()(64bit), libX11.so.6()(64bit), libX11-xcb.so.1()(64bit), libxcb.so.1()(64bit), libxcb-cursor.so.0()(64bit), libxcb-icccm.so.4()(64bit), libxcb-image.so.0()(64bit), libxcb-keysyms.so.1()(64bit), libxcb-render-util.so.0()(64bit), libxcb-randr.so.0()(64bit), libxcb-render.so.0()(64bit), libxcb-shape.so.0()(64bit), libxcb-shm.so.0()(64bit), libxcb-sync.so.1()(64bit), libxcb-xfixes.so.0()(64bit), libxcb-xkb.so.1()(64bit), libxkbcommon-x11.so.0()(64bit), libpulse.so.0()(64bit), libasound.so.2()(64bit), libfontconfig.so.1()(64bit), libfreetype.so.6()(64bit), libdbus-1.so.3()(64bit)
%description
A desktop companion using a cooked Unreal renderer and private Python/Qt shell.
Cat assets retain personal noncommercial conditions; see included attribution.
%install
mkdir -p %{buildroot}
cp -a $root/. %{buildroot}/
%files
%defattr(-,root,root,-)
/opt/mirmir
/usr/bin/mirmir
/usr/share/applications/mirmir.desktop
/usr/share/icons/hicolor/scalable/apps/mirmir.svg
EOF
rpmbuild --dbpath /tmp/rpmdb --nodeps --define '_topdir /tmp/rpmbuild' --define '_build_id_links none' --define '__os_install_post %{nil}' --define '_binary_payload w6.gzdio' --define 'debug_package %{nil}' -bb /tmp/rpmbuild/SPECS/mirmir.spec
cp /tmp/rpmbuild/RPMS/x86_64/*.rpm /out/
rpm --dbpath /tmp/rpmdb -qip /out/mirmir-"$VERSION"-1.x86_64.rpm > /out/rpm-metadata.txt
builddate=${SOURCE_DATE_EPOCH:-$(date +%s)}
cat > "$root/.PKGINFO" <<EOF
pkgname = mirmir
pkgbase = mirmir
pkgver = $VERSION-1
pkgdesc = Unreal-rendered desktop cat companion
url = https://github.com/zeynelgun-afk/Mirmir
builddate = $builddate
packager = Mirmir contributors
size = $((size * 1024))
arch = x86_64
license = MIT
license = GPL-3.0-only
license = LGPL-3.0-only
license = CC-BY-NC-4.0
license = CC-BY-4.0
license = custom:UnrealEngine
depend = glibc>=2.34
depend = gcc-libs
depend = vulkan-icd-loader
depend = glib2
depend = libglvnd
depend = zstd
depend = zlib
depend = libx11
depend = libxcb
depend = xcb-util-cursor
depend = xcb-util-image
depend = xcb-util-keysyms
depend = xcb-util-renderutil
depend = xcb-util-wm
depend = libxkbcommon-x11
depend = fontconfig
depend = freetype2
depend = libpulse
depend = alsa-lib
depend = dbus
EOF
tar --sort=name --owner=0 --group=0 -C "$root" -cf - .PKGINFO opt usr | zstd -T2 -8 -o "/out/mirmir-$VERSION-1-x86_64.pkg.tar.zst"
tar --sort=name --owner=0 --group=0 -C /stage -cf - . | zstd -T2 -8 -o "/out/Mirmir-$VERSION-linux-x86_64.tar.zst"
app=/tmp/Mirmir.AppDir
mkdir -p "$app/usr/lib"
cp -a /stage "$app/usr/lib/mirmir"
cp /recipes/linux/mirmir.desktop "$app/mirmir.desktop"
cp /stage/assets/icon.svg "$app/mirmir.svg"
ln -s mirmir.svg "$app/.DirIcon"
cat > "$app/AppRun" <<'EOF'
#!/bin/sh
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$here/usr/lib/mirmir/mirmir" "$@"
EOF
chmod 755 "$app/AppRun"
mksquashfs "$app" /tmp/mirmir.squashfs -comp gzip -noappend -all-root -processors 2 -quiet
cat /tools/appimage-runtime /tmp/mirmir.squashfs > "/out/Mirmir-$VERSION-x86_64.AppImage"
chmod 755 "/out/Mirmir-$VERSION-x86_64.AppImage"
mkdir -p /out/extracted-deb
dpkg-deb -x "/out/mirmir_${VERSION}-1_amd64.deb" /out/extracted-deb
