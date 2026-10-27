# Unreal görüntüsü için yerel masaüstü katmanı

Bu kabuk 3B görüntüyü üretmez. **Gerçek Unreal sürecinin** dışa aktardığı güncel RGBA PNG karelerini KDE/XWayland üzerinde saydam, çerçevesiz, üstte kalan ve klavye odağı istemeyen bir PyQt6 penceresinde gösterir. Godot veya sabit kedi görüntüsüyle ürün yedeği yoktur. Motor/kare hazır değilse kedi gizlenir, neden sistem tepsisinde görünür.

```bash
python native-shell/mirmir_shell.py
python native-shell/mirmir_shell.py --stop
```

Motor yolu önce `MIRMIR_UE_ROOT`, sonra proje `.local/engine.json` dosyasındaki `engine_root` alanından okunur. Harita, bootstrap yardımcısının `.local/preview.json` içindeki **`preview_map`** alanından alınır (`map_path` alternatifi de okunur). Metadata veya karşılık gelen `.umap` yoksa tepside neden bildirilir; kedisiz `/Engine/Maps/Entry` varsayılanı başlatılmaz.

Başlatma `UnrealEditor Mirmir.uproject /Game/.../MirmirPreview -game -windowed -MirmirDesktop -RenderOffscreen -nosplash` kullanır. Harita argümanı proje yolunun hemen ardından gelir. Yalnızca Unreal alt sürecinin ortamına `SDL_VIDEODRIVER=x11` verilir; kullanıcı oturumu veya genel ortamı değiştirilmez. Henüz gerçek motorla çalıştırıldığı veya yerel tıklama akışının doğrulandığı varsayılmamalıdır.

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

Bu test Qt offscreen üzerinde harita metadata kapısını, tek örnek kilidini, sahiplik/zaman damgalı kapatma komutunu, oturum/sıra/boyut/CRC geçerliliğini, eşleşmeyen karenin tazelik sınırıyla bekletilmesini, açık hata/hazır değil durumunda gizlenmeyi, alpha/DPI maskesini, ekran sınırını, atomik komutu, test etiketini ve sevme tekrar sınırını sınar. Tek sayılı satır genişliğinde NumPy ve Python maskeleri karşılaştırılır. Bütün dosyaları geçici dizindedir; gerçek masaüstü penceresi açılmaz, fare hareketi veya tıklama gönderilmez. **Gerçek Unreal çıktısını veya masaüstü tıklamasını kanıtlamaz.** `--no-engine --frame-dir /tmp/isolated-test-frames` yalnızca izole kare denemeleri içindir ve tepside açık test etiketi taşır; kullanıcı ürünü olarak sunulmaz.

Test ayrıca sentetik 512×512 alpha maskesini ölçer. Bu makinede NumPy+Qt bitmap yolu beş örnekte ortalama yaklaşık **0.38 ms**, tek Python yedek örneği yaklaşık **15.8 ms** ölçüldü. Bu yalnızca maske oluşturma maliyetidir; PNG çözme, Unreal render veya ürün FPS ölçümü değildir. Komut yeni örnek sürelerini her koşuda yazar.

Bağımlılık: mevcut sistemde kurulu PyQt6. Qt platformu normal kullanımda `xcb` seçilir; kullanıcı Wayland oturumu veya KWin ayarları değiştirilmez.
