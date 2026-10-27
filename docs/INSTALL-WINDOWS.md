# Windows kurulum sihirbazı

Windows hedefi x64 Windows10/11'dir. Sihirbaz kullanıcı hesabına
`%LOCALAPPDATA%\Programs\Mirmir` altına kurar; yönetici yetkisi, Engine SDK,
Python veya Qt kurulumu istemez. Başlat menüsü ve isteğe bağlı masaüstü kısayolu
oluşturur; kayıtları kaldırma sırasında silmez. Otomatik başlangıç eklemez.

**Şu an Linux üzerinde Windows UE Shipping derlemesi ve Windows kurulum EXE'si
doğrulanmış değildir.** Inno Setup kaynağı ve derleme betikleri hazır bir Windows
payload'u zorunlu tutar. Linux ikili dosyasını `.exe` olarak paketlemez.
Bir Windows yükleyicisi yayımlanana kadar Windows hazır indirmesi var saymayın.

Derleyici makinede ayrı olarak lisanslanmış UE5.8.4 Windows kurulumu, uyumlu
Visual Studio C++ araçları, Python3.12 ve Inno Setup6 bulunmalıdır:

```powershell
./packaging/windows/build-all.ps1 -UnrealRoot 'C:\UnrealEngine-5.8.4'
```

Betik önce gerçek Win64 C++/Shipping cook, ardından PyInstaller Qt kabuğu,
donmuş kabuk öz sınaması ve Inno Setup derlemesi yapar. Herhangi bir adım
başarısızsa yükleyici üretildi diye raporlamaz. Uygulama son kullanıcısının
Unreal hesabı/SDK'si gerekmez; bunlar yalnız Windows derleyici makine içindir.
Sürüm varsayılan olarak `packaging/VERSION` dosyasından okunur.

İlk gerçek Windows koşusunda GPU/grafik backend'i, ses, saydam pencere, tıklama, normal
kapanma/yükseltme ve kayıt geri yükleme ayrıca doğrulanmalıdır. İmzasız bir ilk
EXE için Windows SmartScreen yayımlayıcı doğrulaması bulunmayabilir.
