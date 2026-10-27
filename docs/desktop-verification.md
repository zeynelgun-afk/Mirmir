# Masaüstü laboratuvarı doğrulaması

2026-10-27 19:10 TRT / 16:10 UTC. Bu kayıt **Godot fizibilite prototipine** aittir. Unreal derlemesini, fotogerçekçi kürkü veya kullanıcının elle deneyimini doğrulamaz.

## Sonuç

Gerçek KDE/Wayland oturumunda, XWayland üzerinden yürütülen son koşuda **20 otomatik kapsam kontrolü geçti**. Gerçek tıklamanın kompozitör üzerinden diğer uygulamaya ulaşması **doğrulanmadı**. JSON bu ayrımı `verified_scope_passed: true`, `passed: false`, `manual_user_validation: false` alanlarıyla kaydeder.

Koşu komutu:

```bash
python scripts/verify_desktop.py --no-input \
  --output-dir artifacts/desktop-verification-no-input
```

Girdi göndermeyen kip varsayılandır. Komutun çıkış kodu yalnızca gerçekleştirilen otomatik kapsamı bildirir. Atlanan gerçek tıklama deneyi `unverified` içinde kalır; sıfır çıkış kodu bütün masaüstü deneyiminin geçtiği anlamına gelmez.

## Doğrudan ölçülenler

| Alan | Kanıt |
| --- | --- |
| Grafik oturumu | KDE, Wayland oturumu; Godot 4.7.1 `--display-driver x11 --rendering-method gl_compatibility` |
| Donanım | AMD Radeon 890M, Mesa 26.2.5 |
| Masaüstü | X11 kökü 2880×1800 fiziksel piksel; KDE mantıksal ekranı 1440×900, ölçek 2 |
| Pencere | 880×700, fiziksel konum `(1965,1010)`, 32 bit ARGB, `override_redirect=true` |
| Şeffaflık | Uygulamanın kendi viewport görüntüsünün dört köşesinde alpha 0; görünür içerikte alpha 255 |
| Giriş şekli | İlk kare yedeği yerine alpha örneklemesinden oluşan gerçek X Shape bölgesi; son sorguda 87 dikdörtgen; biçim/poz değiştikçe sayı değişebilir |
| Bölge sınırı | Yerel `(25,25)` boş köşe giriş şeklinin dışında; `(440,350)` kedi gövdesi içinde |
| Durum geçişleri | İzole IPC komutlarıyla sevme, mama, oyun ve dinlenme durumları doğrulandı |
| Kalıcılık | İzole gerçek kayıtta `total_pets=1`, `bond=12.18`; yeniden açılışta aynı değerler yüklendi |
| Test yalıtımı | Geçici XDG veri/durum dizinleri kullanıldı; `--test-mode` kayıt yazmadı; geçici dizinler temizlendi |
| Odağa etkisi | X11 klavye odağı değişmedi; son koşu fareyi hareket ettirmedi |
| Kapanış | IPC `quit` komutuyla süreç sıfır koduyla kapandı |

`pet` durumuna geçişin IPC ile doğrulanması, fareyle sevmenin çalıştığını kanıtlamaz. Giriş bölgesinin işletim sistemine uygulanmış olması da kullanıcının gerçek tıklama akışını tek başına kanıtlamaz.

## XTest denemesinin sınırı

Önceki koşuda uygulamanın altına yalnızca teste ait bir X11 pencere yerleştirildi. Bu pencerenin fiziksel konumu ve boyutu Godot penceresiyle aynıydı; Qt ölçeklendirmesi kullanılmadı.

XTest ile istenen `(1990,1035)` kök koordinatına karşılık XWayland `(2593,1285)` ve alıcı pencere `0` bildirdi. Güvenlik denetimi, alıcının teste ait pencere olduğunu doğrulayamadığı için **fare düğmesine basılmadan deneyi kesti**. Doğrudan `SendEvent` ile sahte bir başarı üretilmedi. Bir önceki koşuda eski fare konumuna dönüş girişimi de kesin olarak doğrulanamadı; sonraki koşularda fare emülasyonu durduruldu.

Bu gözlem, kedinin gerçek kullanıcı tıklamasını almadığını kanıtlamaz; bu oturumdaki otomasyon yönteminin yeterli kanıt üretemediğini gösterir. Gerçek tıklama geçişi ve elle sevme henüz açık doğrulama maddeleridir.

## Performans ve bilinen sınırlar

Son kısa başlangıç görüntülerinde motorun FPS sayacı 17 bildirdi; uygulama açıldıktan sonraki dört IPC durum örneğinde 30 FPS görüldü. Bunlar uzun süreli performans ölçümü değildir ve sabit 30 FPS vaadini desteklemez. `memory_bytes` Godot'nun kendi statik bellek sayacıdır; süreç RSS'si veya GPU belleği değildir.

Önceki hızlı `pet → quit` kalıcılık denemesinde oluşan kapanış uyarısı teşhis edildi: `--verbose` kaydı, `purr.ogg` için `AudioStreamOggVorbis`, `OggPacketSequence`, `AudioStreamPlaybackOggVorbis` ve `OggPacketSequencePlayback` nesnelerinin ses karıştırma iş parçacığında tutulduğunu gösterdi. Kapanışta döngü bağlantısı kesilip oynatıcılar durduruluyor, akış başvuruları bırakılıyor ve karıştırıcıya kısa bir boşalma süresi tanınıyor. Düzeltme sonrasındaki üç başlatma/kapatma içeren son bütünleşik koşuda **uyarı, motor hatası veya betik çalışma hatası yok**; `engine_log_messages` boş.

## Yerel kanıt dosyaları

- [Son koşu JSON](../artifacts/desktop-verification-no-input/result.json)
- [Uygulamanın kendi viewport görüntüsü](../artifacts/desktop-verification-no-input/viewport.png)
- [Kayıt yüklendikten sonraki görüntü](../artifacts/desktop-verification-no-input/reloaded-viewport.png)
- [Godot günlüğü](../artifacts/desktop-verification-no-input/godot.log)
- [Önceki XTest sınırlamasının JSON kaydı](../artifacts/desktop-verification/result.json)

`artifacts/` Git dışında tutulur. JSON, doğrulanan kaynak dosyalarının SHA-256 özetlerini içerir. Bu koşudaki `main.gd` özeti: `53f940202cc76903d0d96e6aeb961407ef20e47f64827cdb6551e00abd5be16c`.
