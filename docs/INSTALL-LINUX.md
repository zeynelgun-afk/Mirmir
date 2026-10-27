# Linux kurulumu

Hedef: x86_64, glibc≥2.34, Vulkan destekli GPU. Ubuntu22.04+, Debian12+ ve güncel
Fedora/Arch/openSUSE aileleri için paket biçimleri hazırlanır. Gerçek doğrulama
ayrı yayımlanır; bu liste her dağıtımda kurulum testi yapıldığı iddiası değildir.
Güncel Qt wheel'i nedeniyle Ubuntu20.04/RHEL8 destek hedefi değildir.

Debian/Ubuntu:

```bash
sudo apt install ./mirmir_*_amd64.deb
mirmir
```

Fedora:

```bash
sudo dnf install ./mirmir-*.x86_64.rpm
mirmir
```

openSUSE için aynı RPM'i `sudo zypper install ./mirmir-*.x86_64.rpm` ile kurun.
Arch/CachyOS/Manjaro için `sudo pacman -U ./mirmir-*-x86_64.pkg.tar.zst` kullanın.
Paket yöneticisi uygulamayı `/opt/mirmir` altına, menü kısayolunu ve `mirmir`
komutunu standart dizinlere kurar. Normal kaldırma kedi kaydını silmez.

AppImage:

```bash
chmod +x Mirmir-*-x86_64.AppImage
./Mirmir-*-x86_64.AppImage
```

FUSE kullanılmıyorsa bu runtime'ın `--appimage-extract-and-run` seçeneğini kullanın.
Taşınabilir arşiv için bir kullanıcı dizinine çıkarıp `./mirmir` çalıştırın:

```bash
mkdir -p Mirmir
tar --zstd -xf Mirmir-*-linux-x86_64.tar.zst -C Mirmir
cd Mirmir
./mirmir --diagnose
./mirmir
```

`--diagnose` motoru başlatmadan Python/Qt, Vulkan/X11 kütüphaneleri ve Shipping
dosyasını denetler. `./mirmir --stop` yalnız kendi etkin Mırmır örneğine normal
kapanma isteği gönderir. Kayıtlar XDG kullanıcı veri dizininde, geçici kareler
desteklenen sistemlerde özel tmpfs dizininde tutulur. Paket dizinine yazılmaz.

Wayland'da Qt kabuğu kendi süreci için XWayland kullanır; oturum tipi veya KWin
ayarı değiştirmez. GPU sürücüsü, ses sunucusu ve sistem tepsisi davranışı
masaüstüne göre ayrıca sınanmalıdır.
