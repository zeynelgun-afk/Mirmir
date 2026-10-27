# Güncel durum — 2026-10-28

**0.4.0 Linux Shipping** üretildi ve kullanıcının kaydı korunarak başlatıldı.
Motor Unreal Engine5.8.4; Godot deneyi kapalı arşivdir. Kod ve paketler
[halka açık GitHub deposunda](https://github.com/zeynelgun-afk/Mirmir),
indirmeler [Releases](https://github.com/zeynelgun-afk/Mirmir/releases) altındadır.

## Bu sürümde

- Sabit yatak/mama kabı gerçek Unreal RGBA'sıyla ayrı şeffaf pencerede kalır.
  Kedi yatağa yürüyüp çömelerek dinlenir; kuyruk gevşemesi ve küçük nefes vardır.
  Mama kabına yaklaşır, başını eğerek yeme tepkisi verir. Yaklaşma anlık değildir;
  kontrollü başlangıçta yaklaşık13s sürdü.
- Ekran kenarında Play'e yanlışlıkla Rest gönderilmesi giderildi. Feed/Play
  önceki sevme beklemesine takılmaz; sevme yemeği/oyunu kesmez. Motor onaylı
  eylem mesajları menüde görünür. Yatak/kaba tıklama Rest/Feed gönderir.
- Donmuş pozda yerinde gövde pivotu yerine sınırlı hız/eğrilik/yaw ivmesiyle
  ileri yaylar kullanılır. Dört pati için sınırlı IK vardır; hızlı bırakma
  sıçraması azaltılmıştır, sıfır kayma iddiası yoktur.
- Van/British/siyah-beyaz görünüm, gerçek bekleme/kuyruk/yürüyüş klip harmanı,
  kısa kürk kabukları, floatHDR renk dönüşümü ve fiziksel Chaos topu vardır.
- Arada imleç ilgisi ve kısa yaklaşma; el imleci, sevmede küçük baş eğimi ve
  gerçek kedi kayıtlarından mırlama/miyavlama. İmleç geçmişi kaydedilmez.
- Hareket30, sakin duruş8fps hedeflenir. Kayıtlar XDGdata, günlükler XDGstate,
  canlı kareler mümkünse özel runtime tmpfs dizinindedir.

## Kanıt ve sınırlar

C++/UHT Editor ve Shipping cook/package geçti. Çekirdek ASan/UBSan, kabuk/hash
kontrolleri ve3/3 gerçek Unreal otomasyonu başarılıdır.47s gerçek RHI testinde
bed/rest/feed/game/pet geçişleri,1093 örnek ve90 ham PNG kaydedildi. Oyundan
sonra gelen sevmede115/115 örnekte Play+top aktif kaldı. Gerçek Shipping ve
Flatpak sandbox içinde kedi/eşya kareleri PID/oturum/CRC/alpha ile doğrulandı;
normal kapatma testleri geçti.

`.deb`, `.rpm`, Arch paketi, AppImage, taşınabilir arşiv ve doğrudan Flatpak
üretildi. Son.deb temiz resmi Ubuntu22.04 container'da apt ile kuruldu; QtXCB
bağlantıları ve offscreen kabuk testi geçti. Bu, ayrı masaüstü/GPU testi değildir.
Fedora/openSUSE/Arch temiz kurulumu ve Windows çalıştırması doğrulanmadı.
Windows per-user sihirbazı/derleme kaynakları hazırdır; Windows EXE üretilmedi.

Son kullanıcı oturumu kilitlidir.0.4 motor ve Qt gösterim isteği/kareleri hazır,
fakat kilit sırasında X11 map/ekranda görünürlük doğrulanamadı. Kilit, oturum,
KWin veya sürücü ayarı değiştirilmedi.0.1–0.3 açık oturum kanıtları0.4 için
aynen yeniden etiketlenmez. Kullanıcının görsel doğallık kabulü ayrıca açıktır.

Dinlenme sınırlı prosedürel loaf/çömelme pozudur; sanatçı uyku klibi veya kapalı
göz geometrisi yoktur. Kedi hareketi tam kuvvet/kas/pati tabanı simülasyonu
değildir. IK bırakmasında ve hızlı top temasında kalan hatalar vardır. Görünüm
profilleri aynı modeli kullanır; Groom ve nihai fotogerçekçilik tamamlanmadı.

[0.4 doğrulaması](release-0.4-verification.json) ·
[Hareket araştırması](research/locomotion-2026-10-27.md) ·
[Kurulum](INSTALL.md) · [Flatpak/Flathub sınırı](FLATPAK.md).
Özgün kod MIT, ayrı PyQt kabuk GPL3; Cat Walk türevleri CC BY-NC4.0, ek hareket
CC BY4.0, sesler CC0. [Kaynaklar ve lisanslar](../ASSET-LICENSES.md).
