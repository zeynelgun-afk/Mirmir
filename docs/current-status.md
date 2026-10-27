# Güncel durum — 2026-10-27

Ana ürün Unreal Engine **5.8.4** kullanıyor. Kullanıcının gerçekçilik ve doğal
yürüyüş hedefi sürüyor. İlk Godot prototipi kapalıdır; kullanıcı tarafından
bildirilen yürüyüş kusuru geçmiş otomatik pencere testleriyle geçersiz sayılmaz.

## Tamamlanan ve gerçek çalıştırmayla doğrulananlar

- Resmî `Linux_Unreal_Engine_5.8.4.zip` indirmesi tamamlandı; ZIP CRC kontrolüyle
  `/home/zeynel/Applications/UnrealEngine-5.8.4` altına kuruldu.
- Paket içindeki v26 Clang 20.1.8 araçsetiyle gerçek UE/UHT C++ derlemesi geçti.
  İndirilen eski v20 Clang13 arşivi motor değildir, kurulmadı ve korundu.
- Gerçek iskelet, dört mesh parçası, 0.8 saniyelik yürüyüş döngüsü ve CC0
  miyavlama/mırlama kayıtları Unreal'a aktarıldı. Blueprint ve önizleme haritası
  editör API'siyle oluşturulup kaydedildi.
- Unreal otomasyonunda iki davranış testi başarılı; Linux süreç çıkışı0.
  C++ çekirdeği ASan/UBSan ve yerel kabuk/hash kontrolleri başarılı.
- Gerçek Vulkan/Unreal sahne yakalamasında alpha0–255 kedi kareleri alındı.
  Qt/XWayland kabuğu bunları gerçek motora ait PID, oturum, sıra, boyut ve CRC
  eşleşmesiyle canlı masaüstünde gösterdi. Godot veya sabit resim yedeği yoktur.
- Yaklaşık21 saniyelik gerçek örneklemede 307 kare / 14.61FPS; sevme, oyun,
  dinlenme, mama ve sessize alma komutları motor durumunda gözlendi.
- Yerel X11 pencere incelemesinde kedi biçimli bounding maskesi ve klavye odağı
  istemeyen pencere doğrulandı; aktif pencere kimliği açılış boyunca değişmedi.
  Gerçek kullanıcı tıklama deneyimi ayrıca kullanıcı gözlemine açıktır.
- Shipping build/cook/stage/pak/package/archive aşamaları geçti. Son sürümde
  yavaş dönüş animasyon alt sınırı ve Shipping kare sınırı düzeltmeleri bulunur.
  Son bağımsız runtime/ses doğrulaması `unreal-runtime-verification.json` içinde kayıtlıdır.

## Yürüyüş

Kaynak veride düşük yükseklikli pati kemiği örnekleri yaklaşık68cm/s adım
referansı verdi. Gerçek UE ölçümünde düz ilerleme bakış yönüyle eşleşiyor;
123 düşük yükseklikli distal kemik aralığında artık hareketin medyanı2.06cm/s,
p95'i17.08cm/s. Bu sonuç pati tabanı kilidi veya sıfır kayma anlamına gelmez.
Ölçüm ve yöntem `unreal-motion-verification.json` içinde kayıtlıdır.

Doğrulamada eski0.1 animasyon hız alt sınırının çok yavaş dönüşleri zorladığı
bulundu;0.01'e indirildi. Hareket gerçek bakış yönünde ve dönüşte yavaşlayarak
ilerler. Gösterim ve masaüstü hareketi aynı ortografik piksel/cm ölçeğini kullanır.

Eksik bekleme klibi için mevcut yürüyüşün1/30 saniyedeki pozu tutulur. Yerinde
pivottaki tutulan poz, duruş geçişleri ve klibin kısa döngü sınırı görsel
cilalama ister. Ayrı oturma, uyuma, temizlenme veya koşma klipleri yoktur.
Groom/fotogerçekçi kürk ve pati sabitleme/IK henüz uygulanmadı.

## Ses ve ürün sınırları

Offscreen Unreal varsayılan SDL dummy ses çıkışı ve odağı kaybedince sessize
alma davranışı tespit edildi. Yalnız uygulamanın alt sürecine gerçek PulseAudio
sürücüsü seçilir; proje arka plan ses çarpanı1'dir. Son pakette yalnız kendi PulseAudio çıkış akışı ölçüldü: RMS0.01137, tepe0.09781.
Mikrofon veya diğer uygulamaların sesi alınmadı; fiziksel hoparlörde duyma
kullanıcı tarafından ayrıca değerlendirilebilir.

Bu çalışan ilk Unreal dilimidir. Nihai fotogerçekçilik, kapsamlı oyun içeriği
ve kullanıcı tarafından doğal yürüyüş kabulü tamamlandı diye sunulmaz. Model
kişisel/ticari olmayan kullanım için CC BY-NC4.0; sesler CC0 lisanslıdır.
Kullanıcı verileri korunur; otomatik oturum başlangıcı kurulmadı.

## Bağımsız sürüm son kontrolü

Final Shipping örneğinde 26 saniyede381 kare, 14.63FPS gözlendi; motor kare
sınırı15 olarak doğrulandı. On yavaş hareket örneği dahil animasyon
hızının gerçek hız/68 oranından en büyük farkı2.81e-8 oldu. Dururken yürüyüş
oynatımı kapalıdır. Kısa başlangıç örneğinde yaklaşık440MiB RSS ve tek
çekirdeğe göre%66 CPU görüldü; uzun süreli enerji ölçümü yapılmadı.

İlk Shipping koşusunda Mesa ACO shader ön hazırlık havuzunda çöktü. Yalnız
projenin PSO ön hazırlığı kapatıldı; gerçek çizilen kedi için gereken
pipeline'lar çalışıyor. Sistem sürücüleri ve oturum ayarları değiştirilmedi.

Normal kapatma, kaydetme ve tekrar açma gerçek paketle sınandı: bağ değeri
aynı kaldı, son kapanışta bekleyen komut sayısı0 oldu. İki kez gönderilen
kapat komutunun sonraki açılışı bitirmesi giderildi. Uygulama menüsü kuruldu
ve gerçek Shipping sürümü kullanıcının mevcut masaüstünde açık bırakıldı.
