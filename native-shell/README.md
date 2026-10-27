# Unreal görüntüsü için yerel masaüstü katmanı

Bu kabuk 3B görüntüyü üretmez. **Gerçek Unreal sürecinin** dışa aktardığı güncel RGBA PNG karelerini KDE/XWayland üzerinde saydam, çerçevesiz, üstte kalan ve klavye odağı istemeyen bir PyQt6 penceresinde gösterir. Godot veya sabit kedi görüntüsüyle ürün yedeği yoktur. Motor/kare hazır değilse kedi gizlenir, neden sistem tepsisinde görünür.

```bash
python native-shell/mirmir_shell.py
python native-shell/mirmir_shell.py --stop
```

## Gerçek çalıştırma durumu — 2026-10-27

Unreal 5.8.4 C++ derlemesi, dört mesh parçasının aynı iskeletle birleştirilmesi,
0.8 saniyelik authored yürüyüşün ve gerçek seslerin içe aktarılması, Blueprint
ve harita oluşturulması tamamlandı. Editör tabanlı native-shell koşusunda
yaklaşık **21 saniyede 307 kare / 14.61 FPS** ölçüldü. Alpha 0–255 ve aynı gerçek
Unreal PID'sine ait oturum/sıra/boyut/CRC eşleşmeleri doğrulandı; Play, Pet,
Rest, Mute ve Feed IPC etkileri motor durumunda gözlendi.

Shipping paketi de üretildi. İlk bağımsız açılışın Mesa ACO PSO ön hazırlığında
çökmesi üzerine projede `r.PSOPrecaching=0` ile yeniden paketlendi. Düzeltilmiş
**gerçek Shipping** sürümü bu kabukta 35 saniyeden uzun çalıştı;
`engine_kind=shipping`, hazır kareler ve alpha 0–255 gözlendi. Bu kısa kontrollü
koşular uzun süreli dayanıklılık ve kullanıcı deneyimi kabulünü tamamlamaz.

X11 pencere sorgusunda kedi biçimli maske ve klavye odağı istemeyen pencere
görüldü; açılış boyunca aktif pencere kimliği korundu. Gerçek kullanıcı
tıklamaları, çoklu ekran ve bütün tam ekran uygulamaları ayrıca doğrulama
alanıdır. Yürüyüş/duruş sınırları [Unreal belgesinde](../unreal/Mirmir/README.md),
ölçüm yöntemi [hareket kaydında](../docs/unreal-motion-verification.json) bulunur.

Offscreen modda varsayılan dummy ses çıkışını aşmak için yalnız Unreal alt
sürecine `SDL_AUDIODRIVER=pulseaudio` ve `SDL_AUDIO_DRIVER=pulseaudio`
varsayılanları verilir; projede odağı kaybetme ses çarpanı 1'dir. Gerçek
PulseAudio'da kendi oyun PID'sine ait açık akış ve yalnız bu akışın dört
saniyelik mırlama monitöründe RMS **0.01137**, tepe **0.09781** ölçüldü.
Mikrofon veya diğer uygulamalar kaydedilmedi. Fiziksel hoparlör ve kullanıcının
duyma deneyiminin kabulü ayrıca açıktır.

## Başlatma seçimi

Öncelik, `.local/package.json` içindeki tamamlanmış yerel Linux **Shipping** paketidir. Manifestin `packaging_command_succeeded=true` olması, arşiv altındaki `Mirmir-Linux-Shipping` dosyasının çalıştırılabilir olması ve harita yolunun geçerliliği denetlenir. Kabuk, resmi Linux code-based bootstrap sözleşmesine uygun olarak **doğrudan** `Mirmir-Linux-Shipping Mirmir /Game/...` başlatır; araya `.sh` süreci girmediği için üretici PID doğrulaması korunur. Bu kip editör kurulumunu gerektirmez. Paketleme başarısı kendi başına runtime veya yürüyüş doğrulaması sayılmaz.

Geçerli paket yoksa editör yolu önce `MIRMIR_UE_ROOT`, sonra proje `.local/engine.json` dosyasındaki `engine_root` alanından okunur. Bu yedekte harita, bootstrap yardımcısının `.local/preview.json` içindeki **`preview_map`** alanından alınır (`map_path` alternatifi de okunur). Metadata veya karşılık gelen `.umap` yoksa tepside neden bildirilir; kedisiz `/Engine/Maps/Entry` varsayılanı başlatılmaz.

Editör yedeği `UnrealEditor Mirmir.uproject /Game/.../MirmirPreview -game -windowed -MirmirDesktop -RenderOffscreen -nosplash` kullanır. Harita argümanı proje yolunun hemen ardından gelir. Yalnızca Unreal alt sürecinin ortamına `SDL_VIDEODRIVER=x11` verilir; kullanıcı oturumu veya genel ortamı değiştirilmez. Editör ve düzeltilmiş Shipping koşularının gerçek görüntü kanıtları yukarıda ayrı belirtilmiştir; yerel fare etkileşiminin kullanıcı kabulü ayrıca açıktır.

`scripts/install-launcher.sh` varsayılan olarak gerçek UE masaüstü yolunu açan `mirmir.desktop` ve kendi `--stop` eylemini oluşturur. Eski laboratuvar yalnız açık `--lab` seçeneğiyle ayrı `mirmir-lab.desktop` dosyası olarak kurulabilir; bu eylem `--stop-lab` kullanır. Kurulum betiğinin hazırlanmış olması kullanıcı uygulama menüsüne kurulduğu anlamına gelmez.

Ürün kipinde durum dizinindeki `shell.lock` üzerinde `flock` ikinci kabuk ve motor örneğini engeller. `--stop` PID öldürmez: aktif kilidin oturum belirtecine ve güncel zaman damgasına bağlı atomik `shell-command.json` yazar; kabuk bunu kendi 15 Hz döngüsünde tüketip kapanır. Eski veya başka oturuma ait komut kabul edilmez. Kilitsiz eski dosya çalışan süreç sayılmaz. Kabuk normal kapanışta yalnızca kendi oluşturduğu Unreal alt sürecini bekler; takılırsa yalnızca o alt süreç nesnesini sonlandırır. `--no-engine` testleri ürün kilidini almaz ve izole dizin kullanmalıdır.

## Kare ve komut sözleşmesi

- Kareler: `${XDG_STATE_HOME:-$HOME/.local/state}/mirmir/unreal/frame.png`. Sadece bu dizindeki yerel dosya adları kabul edilir. `runtime.json` içindeki `frame_file` adı bunu değiştirebilir.
- `runtime.json`: `running: true`, `ready: true`, boş olmayan `session_id`, pozitif tamsayı `frame_seq`, `width`, `height`, `png_bytes`, `png_crc32`, `desktop_offset_px: [dx,dy]`; isteğe bağlı `actor_world_cm`. Ürün kipinde `producer_pid`, kabuğun başlattığı Unreal alt süreciyle aynı olmalıdır.
- PNG ve JSON üreticide ayrı atomik yayımlanır. Kabuk PNG'yi tek dosya tanıtıcısıyla bir kez okuyup bayt uzunluğu ve standart CRC32'yi metadata ile eşleştirir; **aynı baytları** `QImage.fromData` ile çözer. Boyutlar doğrulanır; aynı oturumda eski kare sırası veya aynı sıra için çelişkili bilgi kabul edilmez.
- En fazla 15 Hz okuma yapılır. Geçici PNG/metadata eşleşmezliğinde yalnızca daha önce doğrulanmış kare ve ona ait hareket konumu en fazla üç saniyelik tazelik sınırına kadar korunur; eşleşmeyen yeni kare çizilmez. Açık üretici hatası, `running=false` veya `ready=false` kediyi hemen gizler. Bozuk, alpha kanalı olmayan, bütünü opak veya üç saniyeden eski kare gösterilmez. Ekrandaki başka uygulamalar okunmaz.
- Kendi karenin alpha değeri 32'nin altında kalan pikseller Qt pencere/giriş maskesinden çıkarılır. Kurulu NumPy alpha eşiğini vektörel uygular, bitleri paketler; Qt bitmap maskesini C++ tarafında oluşturur. NumPy yoksa daha yavaş Python yedeği bulunur. Maske aygıt piksel oranına göre mantıksal koordinata çevrilir; tüy kenarlarında eşik ve örnekleme yaklaşımı vardır.
- Aktörün `desktop_offset_px` hareketi, aygıt piksel oranına bölünerek kullanıcının başlangıç konumuna eklenir. Pencere kullanılabilir ekran alanına sıkıştırılır. Shift ile sürüklemek başlangıç konumunu taşır.
- Durum: `${XDG_DATA_HOME:-$HOME/.local/share}/mirmir/unreal/desktop-status.json`; `activity`, `satiety`, `energy`, `bond`, `muted` alanları. İhtiyaçlar UE'nin 0–1 ölçeğinde veya açık 0–100 sayılarıyla okunabilir.
- Etkileşimler: aynı veri dizinindeki `commands/` altına atomik küçük JSON; `pet`, `feed`, `play`, `rest`, `mute`, `quit`. Tıklama/okşama `pet` komutunu en sık 0.7 saniyede bir üretir. Menüden kapanış kendi Unreal alt sürecini normal komutla kapatmayı dener, takılırsa yalnızca bu alt süreci sonlandırır.

## Motor gerektirmeyen sınırlı doğrulama

```bash
python native-shell/mirmir_shell.py --self-test
```

Bu test Qt offscreen üzerinde geçerli Shipping paketinin editöre tercih edilmesini, editör olmadan paket seçimini, geçersiz pakette editöre dönüşü, harita metadata kapısını, tek örnek kilidini, sahiplik/zaman damgalı kapatma komutunu, oturum/sıra/boyut/CRC geçerliliğini, eşleşmeyen karenin tazelik sınırıyla bekletilmesini, açık hata/hazır değil durumunda gizlenmeyi, alpha/DPI maskesini, ekran sınırını, atomik komutu, test etiketini ve sevme tekrar sınırını sınar. Tek sayılı satır genişliğinde NumPy ve Python maskeleri karşılaştırılır. Bütün dosyaları geçici dizindedir; sahte test yürütücüleri çalıştırılmaz, gerçek masaüstü penceresi açılmaz, fare hareketi veya tıklama gönderilmez. **Gerçek Unreal çıktısını veya masaüstü tıklamasını kanıtlamaz.** `--no-engine --frame-dir /tmp/isolated-test-frames` yalnızca izole kare denemeleri içindir ve tepside açık test etiketi taşır; kullanıcı ürünü olarak sunulmaz.

Test ayrıca sentetik 512×512 alpha maskesini ölçer. Bu makinede NumPy+Qt bitmap yolu beş örnekte ortalama yaklaşık **0.38 ms**, tek Python yedek örneği yaklaşık **15.8 ms** ölçüldü. Bu yalnızca maske oluşturma maliyetidir; PNG çözme, Unreal render veya ürün FPS ölçümü değildir. Komut yeni örnek sürelerini her koşuda yazar.

Bağımlılık: mevcut sistemde kurulu PyQt6. Qt platformu normal kullanımda `xcb` seçilir; kullanıcı Wayland oturumu veya KWin ayarları değiştirilmez.

Normal kapatma, gerçek Shipping paketinde kaydetme ve yeniden açma ile sınandı;
bağ değeri aynı kaldı. Kapatma isteği yalnız `stop_engine` tarafından bir kez
gönderilir. Komutlar kalıcı bir kuyruk değildir: yeni motor başlamadan önce
eski oturumun küçük, UUID adlı bilinen komut dosyaları atılır; kullanıcı kedi
kaydı korunur. Böylece eski bir `quit` yeni oturumu kapatmaz. Kedinin sağ tık
menüsü tepsi menüsünü açar; sol tuş basılı hareket sevme, Shift+sürükleme
başlangıç konumunu taşıma içindir.
