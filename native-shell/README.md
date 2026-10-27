# Unreal görüntüsü için yerel masaüstü katmanı

Bu kabuk 3B görüntüyü üretmez. **Gerçek Unreal sürecinin** dışa aktardığı güncel RGBA PNG karelerini saydam, çerçevesiz, üstte kalan ve klavye odağı istemeyen bir PyQt6 penceresinde gösterir. KDE/XWayland üzerinde gerçek çalışması doğrulanmıştır; Windows yolu kaynakta hazırlanmıştır, gerçek Windows çalışması henüz doğrulanmamıştır. Godot veya sabit kedi görüntüsüyle ürün yedeği yoktur. Motor/kare hazır değilse kedi gizlenir, neden sistem tepsisinde görünür.

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

Öncelik, `.local/package.json` içindeki tamamlanmış, mevcut platforma ait **Shipping** paketidir (`Linux` veya `Win64`). Manifestin `packaging_command_succeeded=true` olması, binary ve launcher yollarının arşiv içinde bulunması ve harita yolunun geçerliliği denetlenir. Göreli manifest yolları uygulama köküne göre çözülür; taşınabilir paketin kurulduğu dizin sabit değildir. Linux'ta `Mirmir-Linux-Shipping` çalıştırılabilir olmalıdır; Windows'ta `Mirmir-Win64-Shipping.exe` seçilir. Her ikisinde resmi code-based bootstrap sözleşmesine uygun olarak **doğrudan** binary `Mirmir /Game/...` argümanlarıyla başlatılır; ara launcher süreci üretici PID doğrulamasını bozmaz. Bu kip editör kurulumunu gerektirmez. Paketleme başarısı kendi başına runtime veya yürüyüş doğrulaması sayılmaz.

Geçerli paket yoksa editör yolu önce `MIRMIR_UE_ROOT`, sonra proje `.local/engine.json` dosyasındaki `engine_root` alanından okunur. Bu yedekte harita, bootstrap yardımcısının `.local/preview.json` içindeki **`preview_map`** alanından alınır (`map_path` alternatifi de okunur). Metadata veya karşılık gelen `.umap` yoksa tepside neden bildirilir; kedisiz `/Engine/Maps/Entry` varsayılanı başlatılmaz.

Editör yedeği `UnrealEditor Mirmir.uproject /Game/.../MirmirPreview -game -windowed -MirmirDesktop -RenderOffscreen -nosplash` kullanır (`Win64/UnrealEditor.exe` veya `Linux/UnrealEditor`). Harita argümanı proje yolunun hemen ardından gelir. Linux'ta yalnızca Unreal alt sürecinin ortamına `SDL_VIDEODRIVER=x11` ve PulseAudio varsayılanları verilir; Windows'a bu Linux varsayılanları eklenmez. Kullanıcı oturumu veya genel ortamı değiştirilmez. Windows alt süreci `CREATE_NO_WINDOW` ile konsol penceresi açmadan başlatılır. Dondurulmuş Windows kabuğu uygulama kökünü `sys.executable` yanından bulur. Editör ve düzeltilmiş Shipping koşularının gerçek görüntü kanıtları yukarıda ayrı belirtilmiştir; yerel fare etkileşiminin kullanıcı kabulü ayrıca açıktır.

`scripts/install-launcher.sh` varsayılan olarak gerçek UE masaüstü yolunu açan `mirmir.desktop` ve kendi `--stop` eylemini oluşturur. Eski laboratuvar yalnız açık `--lab` seçeneğiyle ayrı `mirmir-lab.desktop` dosyası olarak kurulabilir; bu eylem `--stop-lab` kullanır. Kurulum betiğinin hazırlanmış olması kullanıcı uygulama menüsüne kurulduğu anlamına gelmez.

Ürün kipinde canlı kare dizinindeki `shell.lock` ikinci kabuk ve motor örneğini engeller: Linux'ta `flock`, Windows'ta `LockFileEx` kullanılır. Windows'un zorunlu bayt kilidi JSON'un dışındaki 4096. bayta konur; sahiplik bilgisi okunabilir kalır. `--stop` PID öldürmez: aktif kilidin oturum belirtecine ve güncel zaman damgasına bağlı atomik `shell-command.json` yazar; kabuk bunu kendi 33 ms döngüsünde tüketip kapanır. Bellek dizinine geçiş sırasında eski state dizinindeki aktif kullanıcı kilidi de aranır: normal açılış ikinci kedi başlatmaz ve `--stop` önceki kabuğa güvenli kapatma komutu iletebilir. Kilit dosyası başka kullanıcıya aitse veya simgesel bağlantı/reparse point ise izlenmez; Windows sahipliği mevcut süreç kullanıcısının SID'siyle karşılaştırılır. Yabancı PID okunmaz veya sonlandırılmaz. Eski/başka oturuma ait komut kabul edilmez. Kilitsiz eski dosya çalışan süreç sayılmaz. Kabuk normal kapanışta yalnızca kendi oluşturduğu Unreal alt sürecini bekler; takılırsa yalnızca o alt süreç nesnesini sonlandırır. `--no-engine` testleri ürün kilidini almaz ve izole veri/state dizinleri kullanmalıdır.

## Arada fareye ilgi

Tepsideki **Fareye arada ilgi göster** seçeneği varsayılan açıktır; `--no-cursor-interest` ile kapatılabilir. Başlangıçta 8–14 saniye beklenir. Daha sonra yalnızca 1.5 saniyelik kısa pencerelerde, en fazla 5 Hz hızla `QCursor.pos()` anlık konumu okunur; pencereler arasında 12–20 saniye ara vardır. Farenin kendisi hareket ettirilmez. Başka ekrandaysa, Shift ile kedi taşınıyorsa veya gerçek motorun güncel karesi yoksa hedef gönderilmez. `--no-engine` ve Qt offscreen testleri gerçek fareyi hiç okumaz.

Yalnızca kedinin kendi penceresine göre bağıl hedef, özel canlı dizindeki tek **`cursor.json`** dosyasının üzerine atomik yazılır. Ekranın mutlak fare koordinatları, başka pencere içeriği, ekran görüntüsü veya konum geçmişi tutulmaz; günlük satırı üretilmez. Dosya ilgi penceresi bitince, kapatmada veya hata durumunda kaldırılır. Ani süreç kapanışına karşı her örnek ayrıca 750 ms sonra geçersizdir. Bu veri kısa baş/bakış merakı içindir; yerel kabuk buradan bir yürüme komutu üretmez.

```json
{"schema_version":1,"session_id":"motor-oturumu","producer_pid":123,
 "active":true,"sampled_at_unix_ms":1000,"expires_at_unix_ms":1750,
 "target_offset_px":[120,-40],"target_normalized":[0.46875,-0.15625]}
```

`target_offset_px` pencere merkezinden fiziksel piksel farkıdır: Qt mantıksal farkı aygıt piksel oranıyla çarpılır; sağ/aşağı pozitif, x/y kendi karenin yarı boyutuna sıkıştırılır (512 karede ±256). `target_normalized` aynı farkın yarı kare boyutuna bölünmüş halidir, her bileşen −1..1 arasındadır. Üretici mevcut session/PID, örnek zamanı, son geçerlilik ve sınırları doğrulamalıdır; eski oturum verisi kullanılmaz. Motor tarafındaki baş/bakış dönüşü ve gerçek görüntü kabulü ayrı kontrol gerektirir.

## Sabit yatak ve mama kabı

Unreal yaşam alanını kediden ayrı `props.png` karesine çizer. Kabuk bu gerçek
RGBA çıktısını ikinci, saydam ve alpha maskeli bir pencereye koyar; yatağı veya
kabı kendisi çizmez. Yaşam alanı `home` başlangıç konumunda sabit kalır; kedinin
`desktop_offset_px` hareketi bu pencereye eklenmez. Shift ile iki pencereden
birini sürüklemek ortak başlangıç noktasını taşır.
`props.desktop_offset_px`, yaşam alanının kendi kamera merkezinden kaynaklanan
sabit piksel düzeltmesidir; aygıt piksel oranına bölünerek `home` üzerine eklenir.
Bu ayrı düzeltme kedinin gezinti konumundan bağımsızdır.

Üst `runtime.json` karesi taze, hazır ve kabuğun kendi motor PID'sine ait
olmalıdır. `props` alt kaydı ayrıca boyut, sıra, bayt sayısı ve CRC ile eşleşir.
Statik PNG eski tarihli olabilir; taze ana yayın ve aynı oturum eşleşmesi
gereklidir. Geçersiz ana yayın veya kapatma yaşam alanını da gizler. `--no-engine`
kipinde yaşam alanı gösterilmez. Yatağa sol tıklamak dinlenme, kaba sol tıklamak
mama komutunu yollar; boş alpha alanı alttaki uygulamaya kalır.

Menüdeki son eylem satırı yalnız motorun `interaction` kaydındaki yeni
`serial/action/accepted/message` onayıyla güncellenir. Komut dosyasının yazılması
eylemin gerçekleştiği sayılmaz. Sevme bu satırı bildirimlerle doldurmaz.
Pencerenin ekran kenarına sıkıştırılması kullanıcının başlattığı oyunu otomatik
dinlenme komutuyla kesmez; gezinti için mevcut sınır koruması sürer.

`shell-status.json` yaşam alanının hazır/görünür durumunu, sıra ve pencere
geometrisini ayrı raporlar. Offscreen testler sabit konumu, CRC/oturum kapılarını,
statik kare yaşını, click bölgelerini, eylem onaylarını ve oyun/sınır regresyonunu
sınar; bunlar gerçek Unreal yatma/yeme hareketinin görsel kabulü değildir.

## Windows sınırı

Qt'nin saydam RGBA penceresi ve alpha32 giriş maskesi korunur. Windows'ta yalnız kendi HWND'sine `WS_EX_LAYERED`, `WS_EX_TOOLWINDOW`, `WS_EX_NOACTIVATE` uygulanır; bütün kediyi tıklanmaz yapan `WS_EX_TRANSPARENT` eklenmez. Qt maskesi dışındaki ve sıfır alpha içeren alanların alttaki pencereye geçmesi Win32 layered pencere davranışına dayanır ([Microsoft](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)). Kilidin JSON dışına konması da [LockFileEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex) sözleşmesine dayanır.

Varsayılan Windows veri/durum kökleri `LOCALAPPDATA/Mirmir` altındadır; açık XDG kökleri ve `--frame-dir` korunur. Canlı dizin `LOCALAPPDATA/Mirmir/runtime/<veri-state-hash>` olur; bu Windows'ta **tmpfs değildir**. Dizinin mevcut kullanıcıya ait olması ve reparse point olmaması denetlenir, izinler kullanıcı profilinden devralınır; geniş bir Windows ACL denetimi yapılmaz. Kaynakta Windows platformu, dosya yolları, kilit ve süreç seçenekleri bulunması Windows ürünü doğrulandığı anlamına gelmez. Gerçek Win64 UE paketi, PyInstaller/kurulum, çoklu DPI, odak, alpha/tıklama ve ses gerçek Windows bilgisayarda henüz sınanmadı.

## Kare ve komut sözleşmesi

- Canlı PNG, `runtime.json`, `shell-status.json` ve kilitler varsayılan olarak **`$XDG_RUNTIME_DIR/mirmir/unreal` tmpfs** dizinindedir. Runtime kökünün mutlak, kullanıcıya ait, özel izinli ve gerçekten tmpfs üzerinde olması doğrulanır; alt dizinler de özel/kullanıcıya ait olmalıdır. Uygun runtime yoksa eski XDG state dizinine dönülür. `--frame-dir` açık tercihi en güçlüdür.
- Özel `XDG_STATE_HOME` veya `XDG_DATA_HOME` kullanan denemeler aynı global runtime altında çakışmaz: kanonik veri/state yollarının hash'i `mirmir/unreal/instances/<hash>` ad alanını belirler. Günlükler `${XDG_STATE_HOME:-$HOME/.local/state}/mirmir/unreal` altında kalır; buradaki **`frame-location.json`** canlı kare dizinini ve saklama türünü bildirir. İşaretçi her karede yazılmaz.
- Native kabuk, seçilen mutlak kare yolunu yalnız alt süreç ortamındaki `MIRMIR_FRAME_DIR` ile Unreal'a verir. Exporter bu mutlak tercihi okur; bu değişkeni vermeyen doğrudan/izole Editor testleri önceki `XDG_STATE_HOME/mirmir/unreal` yolunu korur. `frame.png` ve `runtime.json` dosya adları, atomik yayın ve CRC sözleşmesi aynıdır; ekranın başka içeriği okunmaz.
- `runtime.json`: `running: true`, `ready: true`, boş olmayan `session_id`, pozitif tamsayı `frame_seq`, `width`, `height`, `png_bytes`, `png_crc32`, `desktop_offset_px: [dx,dy]`; isteğe bağlı `actor_world_cm`. Ürün kipinde `producer_pid`, kabuğun başlattığı Unreal alt süreciyle aynı olmalıdır.
- PNG ve JSON üreticide ayrı atomik yayımlanır. Kabuk PNG'yi tek dosya tanıtıcısıyla bir kez okuyup bayt uzunluğu ve standart CRC32'yi metadata ile eşleştirir; **aynı baytları** `QImage.fromData` ile çözer. Boyutlar doğrulanır; aynı oturumda eski kare sırası veya aynı sıra için çelişkili bilgi kabul edilmez.
- Qt 33 ms hassas zamanlayıcıyla yeni kareleri yoklar. Üretici kaynakta hareket/tepki için 30, durağan durum için 8 FPS hedefler; bunlar kare sırası/zamanıyla ölçülmesi gereken hedeflerdir. Geçici PNG/metadata eşleşmezliğinde yalnızca daha önce doğrulanmış kare ve ona ait hareket konumu en fazla üç saniyelik tazelik sınırına kadar korunur; eşleşmeyen yeni kare çizilmez. Açık üretici hatası, `running=false` veya `ready=false` kediyi hemen gizler. Bozuk, alpha kanalı olmayan, bütünü opak veya üç saniyeden eski kare gösterilmez.
- Kendi karenin alpha değeri 32'nin altında kalan pikseller Qt pencere/giriş maskesinden çıkarılır. Kurulu NumPy alpha eşiğini vektörel uygular, bitleri paketler; Qt bitmap maskesini C++ tarafında oluşturur. NumPy yoksa daha yavaş Python yedeği bulunur. Maske aygıt piksel oranına göre mantıksal koordinata çevrilir; tüy kenarlarında eşik ve örnekleme yaklaşımı vardır.
- Aktörün `desktop_offset_px` hareketi, aygıt piksel oranına bölünerek kullanıcının başlangıç konumuna eklenir. Pencere kullanılabilir ekran alanına sıkıştırılır. Shift ile sürüklemek başlangıç konumunu taşır.
- Menü ihtiyacı ve görünümü öncelikle eşleşmiş `runtime.json` içindeki `needs` ve üst düzey `appearance`/`appearance_ready` alanlarından okunur. Böylece her karede SSD'deki eski durum dosyası okunmaz. Alanları eksik eski motorlar için canlı dizindeki veya eski veri dizinindeki `desktop-status.json` yedeği korunur. İhtiyaçlar UE'nin 0–1 ölçeğinde veya açık 0–100 sayılarıyla okunabilir.
- Etkileşimler: aynı veri dizinindeki `commands/` altına atomik küçük JSON; `pet`, `feed`, `play`, `rest`, `mute`, `quit`. Alpha maskesi içinde işaretçi standart Qt el simgesidir; sol tuş basılıyken kapalı el, bırakılınca yeniden işaret eden el olur. Maskenin dışında pencere olay almaz. Tıklama/okşama `pet` komutunu en sık 0.7 saniyede bir üretir; yerel fare olayının konumu isteğe bağlı `stroke_normalized:[x,y]` olarak −1..1 sınırında gönderilir (merkez 0,0; sağ/aşağı pozitif). Global koordinat veya okşama geçmişi saklanmaz. Bu standart işaretçi, fotogerçekçi insan eli görseli değildir; 3B tepki motor tarafındadır. Menüden kapanış kendi Unreal alt sürecini normal komutla kapatmayı dener, takılırsa yalnızca bu alt süreci sonlandırır.

## Motor gerektirmeyen sınırlı doğrulama

```bash
python native-shell/mirmir_shell.py --self-test
```

Bu test Qt offscreen üzerinde özel tmpfs seçimini/ad alanını, geçersiz runtime yedeğini, eski kilidin bulunup doğru dizine kapatma komutu yazılmasını, runtime durumunun eski dosyaya üstünlüğünü, Linux/Win64 göreli Shipping/editör seçimini, sahiplik/zaman damgalı kapatma komutunu, eşleşmiş kareleri ve alpha/DPI maskesini sınar. Fare ilgisi için negatif ekran başlangıcı ve DPR dönüşümü, hedef sınırları/TTL, kısa pencere ve gecikme sonrası örnek birikmemesi doğrulanır; test sırasında gerçek QCursor okuması yapılırsa test başarısız olur. Bütün dosyaları geçici dizindedir; sahte test yürütücüleri çalıştırılmaz, gerçek masaüstü penceresi açılmaz, fare hareketi veya tıklama gönderilmez. **Gerçek Unreal çıktısını, masaüstü tıklamasını veya Windows API çalışmasını kanıtlamaz.** `--no-engine` denemelerinde hem `XDG_DATA_HOME` hem `XDG_STATE_HOME` geçici dizine verilmeli, `--frame-dir` açık seçilmelidir; tepside test etiketi görünür.

Test ayrıca sentetik 512×512 alpha maskesini ölçer. Bu makinede NumPy+Qt bitmap yolu beş örnekte ortalama yaklaşık **0.38 ms**, tek Python yedek örneği yaklaşık **15.8 ms** ölçüldü. Bu yalnızca maske oluşturma maliyetidir; PNG çözme, Unreal render veya ürün FPS ölçümü değildir. Komut yeni örnek sürelerini her koşuda yazar.

Bağımlılık: PyQt6; NumPy isteğe bağlı hızlandırmadır. Qt platformu Linux normal kullanımda `xcb`, Windows'ta `windows` seçilir; kullanıcı Wayland oturumu veya KWin ayarları değiştirilmez. Paket kökünde `assets/icon.svg` varsa tepsi simgesi olarak kullanılır.

Normal kapatma, gerçek Shipping paketinde kaydetme ve yeniden açma ile sınandı;
bağ değeri aynı kaldı. Kapatma isteği yalnız `stop_engine` tarafından bir kez
gönderilir. Komutlar kalıcı bir kuyruk değildir: yeni motor başlamadan önce
eski oturumun küçük, UUID adlı bilinen komut dosyaları atılır; kullanıcı kedi
kaydı korunur. Böylece eski bir `quit` yeni oturumu kapatmaz. Kedinin sağ tık
menüsü tepsi menüsünü açar; sol tuş basılı hareket sevme, Shift+sürükleme
başlangıç konumunu taşıma içindir.
