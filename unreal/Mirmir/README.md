# Mırmır — Unreal Engine 5.8.4

Gerçek Unreal C++ modülü derlendi, varlıklar içe aktarıldı ve kedi hem editör
çalıştırıcısıyla hem bağımsız Linux Shipping paketiyle yerel masaüstü kabuğunda
görüntülendi. Bu, geliştirilen ilk çalışan Unreal dilimidir; nihai fotogerçekçi
kürk ve bütün doğal davranış animasyonları tamamlanmış değildir. Eski
`desktop-lab/` kapalıdır ve ürünün çalışma yolu değildir.

## Doğrulanan durum

- Resmî Unreal Engine **5.8.4**, `/home/zeynel/Applications/UnrealEngine-5.8.4`
  altında kurulu. Paket içindeki v26 Clang 20.1.8 ile UHT ve Linux C++ editör
  derlemesi geçti; üç Unreal davranış/kayıt otomasyon testi başarılı.
- Gerçek iskeletli kedi, **dört mesh parçası**, Unreal'da **0.8 saniyelik yürüyüş
  döngüsü** ve üç gerçek kedi SoundWave'i içe aktarıldı. Blueprint ve
  `/Game/Mirmir/Preview/MirmirPreview` haritası editör API'siyle oluşturuldu.
  Yerel yapılandırma denetimi [preview.json](../../.local/preview.json) içindedir.
- Editör tabanlı gerçek native-shell koşusunda yaklaşık **21 saniyede 307 kare,
  14.61 FPS** gözlendi. Alpha aralığı 0–255'ti; üretici PID'si, oturum, kare
  sırası, boyut ve CRC ile aynı Unreal süreci doğrulandı. Play, Pet, Rest, Mute
  ve Feed IPC komutlarının motor durumundaki etkileri kaydedildi.
- Shipping build/cook/stage/pak/package/archive tamamlandı. İlk bağımsız açılış,
  Radeon 890M/Mesa ACO tarafındaki PSO ön hazırlığında çöktü. Projede
  `r.PSOPrecaching=0` uygulanarak yeniden paketlenen sürüm gerçek Qt masaüstü
  kabuğunda **35 saniyeden uzun** çalıştı; `engine_kind=shipping`, hazır kareler
  ve alpha 0–255 gözlendi. Bu yerel uyumluluk çözümüdür; uzun süreli dayanıklılık
  veya bütün sürücülerin uyumu kanıtı değildir.
- Gerçek PulseAudio çıkışında oyunun kendi PID'sine ait, sessize alınmamış ve
  duraklatılmamış akış görüldü. Yalnız bu akışın dört saniyelik mırlama
  monitöründe RMS **0.01137**, tepe **0.09781** ölçüldü. Mikrofon veya başka
  uygulamaların sesi kaydedilmedi. Bu, çıkış sinyali kanıtıdır; fiziksel
  hoparlör veya kullanıcının duyma deneyimi ayrıca doğrulanmış sayılmaz.

Güncel genel durum [current-status.md](../../docs/current-status.md), hareket
ölçüm yöntemi [unreal-motion-verification.json](../../docs/unreal-motion-verification.json)
içindedir. Kısa kontrollü örnekleme sürekli kullanım performansının yerine geçmez.

## Çalıştırma

Proje kökünden:

```bash
./run.sh
./run.sh --stop
./run.sh --preview
./run.sh --editor
```

Varsayılan yol gerçek Unreal görüntülerini kullanan [yerel masaüstü
kabuğudur](../../native-shell/README.md). Geçerli `.local/package.json` varsa
Shipping ikilisi doğrudan başlatılır; yoksa kurulu editör kullanılır.
`--preview` normal oyun penceresi açar; `--editor` geliştirme editörüdür.
Masaüstü katmanı kullanıcı Wayland oturumunu değiştirmeden Qt/XWayland kullanır.
Godot veya sabit kedi görüntüsü yedeği bulunmaz.

Normal önizleme kısayolları `P` sevme, `F` mama, `B` oyun, `M` sessiz,
`Esc` çıkıştır. Masaüstü tepsisi aynı eylemleri IPC ile iletir. IPC durum
doğrulaması, bütün gerçek fare etkileşimlerinin kullanıcı tarafından kabul
edildiği anlamına gelmez. Kedi dışı bölge ve odak davranışı için yerel kabuk
belgesindeki doğrulama sınırlarını koruyun.

## Varlıklar, yürüyüş ve mevcut eksikler

Kedi kaynağı LostBoyz2078'in **Cat Walk** modelidir: **CC BY-NC 4.0, yalnızca
kişisel ve ticari olmayan yerel prototip**. [Kaynak/lisans
kaydı](../../assets/personal-use/sources.json), [döngü
varyantı](../../assets/personal-use/loop-variant.json) ve [genel
lisanslar](../../ASSET-LICENSES.md) korunmalıdır. Model ticari ürün varlığı
olarak kabul edilmez. Miyavlama ve mırlama kayıtları Joseph SARDIN /
BigSoundBank kaynaklıdır ve CC0 lisanslıdır.

Dört mesh, aynı iskeletin pozunu izleyerek birlikte görüntülenir. Hareket gerçek
bakış yönünde uygulanır ve dönüşte yavaşlar. Yürüyüş oynatma hızı gerçek cm/s
hızına bağlanır; yaklaşık **68 cm/s** authored hız referansı kaynak eklem
verilerinden çıkarılmış başlangıç değeridir. Gerçek Unreal örneklemesinde düz
ilerleme bakış yönüyle eşleşti. Düşük yükseklikli distal kemik örnekleri pati
tabanı kilidi değildir; sıfır kayma veya doğal yürüyüşün kullanıcı kabulü
iddia edilmez. Yavaş dönüşlerde eski 0.1 oynatma alt sınırı son kaynakta 0.01'e
indirildi; önceki ölçüm raporu bu düzeltmeden önceki koşuyu açıkça belirtir.

Gerçek bekleme ve kuyruk klipleri uyumlu iskelete retarget edilip motor
karelerinde incelendi; native BlendStack bunları yürüyüşle harmanlar. Oturma
adayları temas/poz hataları nedeniyle ürüne atanmadı. Uyuma, temizlenme veya
koşma klibi yoktur. Dinlenme ihtiyacı, görsel uyuma animasyonu değildir.

0.2'de görünüm seçimi, fiziksel Chaos topu, kısa kürk kabukları ve floatHDR
renk dönüşümü gerçek Shipping çalıştırmasında doğrulandı. Hareket30/sakin8fps
hedeflenir. Güncel kanıt [release-0.2-verification.json](../../docs/release-0.2-verification.json)
içindedir; yukarıdaki14.61fps örneği0.1'e aittir. Pati/dönüş iyileştirmesi
sürüyor; [araştırma kaydı](../../docs/research/locomotion-2026-10-27.md) kaynakları
ve yaklaşım farklarını açıklar. Groom ve tamamlanmış fotogerçekçilik yoktur.

Mırlama ve miyavlama SoundWave'leri atanmıştır. Ayrı yeme veya oyuncak sesi
uydurulmaz; `FeedSound` henüz boş olabilir. Alt süreçte
`SDL_AUDIODRIVER=pulseaudio` ve `SDL_AUDIO_DRIVER=pulseaudio` varsayılanları,
projede `UnfocusedVolumeMultiplier=1.0` kullanılır. Kullanıcının genel ses veya
masaüstü ayarları değiştirilmez.

## Geliştirme ve yeniden üretim

Bu dizinden:

```bash
./scripts/build.sh -MaxParallelActions=2
./scripts/test-core.sh
./scripts/test-engine.sh
```

Motor yolu `MIRMIR_UE_ROOT` veya proje `.local/engine.json` kaydından çözülür.
Çekirdek testi ASan/UBSan altında motor bağımsız ihtiyaç/etkileşim davranışlarını
sınar; tek başına Unreal modülü veya JSON dosya I/O kanıtı değildir.

`scripts/import-assets.py` mevcut lisanslı GLB ve ses kaynaklarını içe aktarır;
`scripts/bootstrap-preview.py` gerçek mesh/animasyon/ses atamalarıyla Blueprint
ve haritayı oluşturur. Bu adımlar bu makinede çalıştırılmıştır. Mevcut Shared
varlıkları veya yabancı Blueprint'ler körlemesine üzerine yazılmaz; importu
tekrarlamadan önce mevcut içeriği inceleyin. Bootstrap yürüyüş, idle veya Groom
üretmez. Yerel Shipping üretimi `scripts/package-game.sh` ile yeni bir arşiv
klasörüne yapılır; önceki paketler korunur.

Kayıt: `${XDG_DATA_HOME:-$HOME/.local/share}/mirmir/unreal/cat-v1.json`.
Komutlar veri kökünün `commands/` dizinindedir. Kareler/runtime/durum ve
kabuk kilidi uygun `XDG_RUNTIME_DIR` özel tmpfs dizininde; bulunamazsa state
dizinindedir. `frame-location.json` güncel yolu gösterir. Kabuk günlükleri
`${XDG_STATE_HOME:-$HOME/.local/state}/mirmir/unreal/` içinde kalır. Unreal editör
önbelleği ve `Saved/` dosyaları ürün kaydından ayrıdır. Bozuk kayıt üzerine
kendiliğinden yazılmaz; `ResetCat` açık sıfırlama eylemidir. Eski Godot
kayıtları otomatik olarak taşınmış sayılmaz.
