# Mırmır hareket cilası

Mevcut tasarımın devamıdır. Kullanıcı gerçekçilik ve geliştirmeyi onay almadan
sürdürme yetkisini korudu; bu dilim Unreal5.8.4 üzerinde uygulanır.

Öncelik: oynayan mevcut kediyi koruyarak duruş geçişlerini ve akıcılığı artırmak.
Uyumlu sanatçı klipleri en doğal seçenektir; başka bir iskeleti aynı skeleton
olarak atamak kabul edilmez. Tam modeli değiştirmek görsel/rig uyumunu bozar;
tam prosedürel bacak hareketi de önceki kullanıcı yürüyüş kusurunu yineleyebilir.
Bu nedenle native animasyon grafiğinde gerçek klip/poz harmanlama, yumuşak
hareket başlangıç/bitişi ve kaynaklı klip araması birlikte yürütülür.

- Animasyon: UE native graph, sahip aktörden oyun thread'inde alınan durum;
  animasyon thread'inde UObject/IPC okuması yok. Walkrate gerçek hız/68;
  atanmamış idle için mevcut1/30 örneği açıkça held-pose fallback'tir.
  Klipler arasında yaklaşık0.2–0.3s poz harmanı; yeni idle/sit/sleep klipleri
  ancak lisans, rig ve gerçek motor görüntüsü kontrolünden sonra atanır.
- Akıcılık: hareket/dönüş/etkileşim sırasında30fps hedefi; sakin duruşta
  yaklaşık8fps ve kısa canlılık payı. Native kabuk en fazla30Hz okur;
  faz koruyan deadline, CRC/session/PID ve alpha kapıları korunur.
- Yaşam hissi: head/neck/tail gibi bağımsız üst gövde hareketleri eklenirse
  küçük/bounded ve additive olur. Gerçek oturma/uyuma klibi diye sunulmaz;
  baş/omurga oranları, mesh ek parçaları ve pati temasına etkisi incelenir.
- Dağıtım: önce izole XDG dizininde gerçek editor/render testi, sonra yeni
  Shipping arşivi. Önceki çalışan paket korunur. Kullanıcı kaydı taşınmaz,
  sıfırlanmaz; normal kapatma sonrası yeni sürüm açılır.

Kabul: gerçek sınırlı30/8fps telemetrisi, kesilmeden akan poz geçişleri,
yürüyüş hız oranı, alpha ve dosya eşleşmesi, ses/sessiz kip, kapatma/yeniden
başlatma. Kaynak değişikliği gerçekUE derlemesiyle ve kısaShipping koşusuyla
sınanır. Kullanıcı doğal yürüyüş/görsel kalite kabulü varsayılmaz. Isı/enerji
uzun süreli ölçümü, Groom, gerçek sit/sleep ve top/olta fiziği ayrı açık işlerdir.

## Kullanıcının genişlettiği görünüm kapsamı

Kullanıcı bilgisayardan ayrılırken çalışmanın onaysız sürmesini, siyah kediye
alternatif Van/British görünümü ve kullanıcı seçimini açıkça istedi.
Varsayılan beyaz ağırlıklı Van görünümü, anatomik sol mavi/sağ amber göz;
British görünümü gri-mavi tüy, iki amber göz, daha geniş gövde/yuvarlak baş;
mevcut siyah-beyaz görünüm menüden seçilebilir. Bunlar aynı lisanslı temel
geometri üzerindeki görsel profillerdir; ayrı safkan tarama modelleri değildir.

Orijinal materyal/mesh/atlaslar korunur; ayrı PBR template'ler ve runtimeMID
parametreleri kullanılır. Göz bebeği ve yüz/deri ayrıntıları korunur. İsteğe
bağlı6kürk kabuğu kısa tüy yaklaşımıdır, Groom diye sunulmaz; kalite ve
performans gerçek kareyle kabul edilmeden varsayılan kabul edilmez.

Görünüm tercihi eski v1 kayda optional alanla eklenir; ihtiyaçlar/bağ/ses
ayarları değişmez. Bilinmeyen eski görünüm Van olur; geçersiz runtimekomutu
reddedilir. Menü motorun onayladığı seçimi gösterir. Kareler için tmpfs
runtime dizini, gerçek kayıt/günlükler için XDGdata/state kullanılır;
oturum kapatılınca yok olacak PNG/lease dosyaları sürekli SSD'ye yazılmaz.

Beyaz tüy parlaklığını korumak için floatHDRreadback, pozlama/filmic dönüşüm
ve tek sRGB dönüşümü uygulanır; U8'de erken kırpılan parlaklık geri kazanılmış
gibi sunulmaz. Alpha/session/CRC/PID kapıları aynı kalır.

## Yeni dönüş, imleç ve dağıtım dilimi

Kullanıcı mevcut dönüşlerin yapay kaldığını bildirdi; web ve benzer GitHub
projelerinin incelenmesini istedi. Güncel tanıda ileri hız sıfırken gövde
rotasyonu devam ediyor. Yeni yaklaşımda doğrusal hız ile açısal hız
`omega = v * curvature` üzerinden bağlıdır; dönüş yarıçapı, yanal ivme ve
açısal ivme sınırlıdır. Arkadaki hedefe ileri yürüyerek bir yay çizilir.
Orijinal yürüyüş korunur; gerçek rig üzerinde sınırlı temas düzeltmesi
görsel olarak doğrulanır. Bu, tam dinamik kas/kuvvet simülasyonu değildir.
Kaynaklar ve projeler [araştırma kaydındadır](../../research/locomotion-2026-10-27.md).

Kullanıcı imleci arada takip etmesini açıkça istedi. Kabuk kısa ilgi
pencerelerinde yalnız kendi kare merkezine göre güncel imleç farkını özel
runtime dizinindeki tek dosyaya yazar. Global koordinat geçmişi, ekran,
klavye veya başka uygulama verisi alınmaz. Motor oturum/PID/TTL kontrolüyle
ortografik hedefi çözer. Önce sınırlı baş ilgisi, uygun etkinlikte kısa ve
mesafeli yaklaşma; dinlenme/yeme/sevme/oyun kesilmez. Kontrol kaybolunca
hedef söner, devamlı fare kovalamaya dönüşmez.

Kullanıcı yeni GitHub deposuna gönderim ve **halka açık** görünürlüğü açıkça
istedi: `zeynelgun-afk/Mirmir`. Kurulum belgesi, kaynak lisansları ve gerçek
çalışma sınırları eklenir. Linux tek staging ağacından portable, Debian,
Fedora/openSUSE ve Arch ailesi paketleri hedeflenir. Windows kullanıcı hesabına
kurulan Inno Setup sihirbazı, gerçek Win64 oyun+kabuk paketiyle oluşturulur.
Mevcut Linux motoru Windows oyun ikilisi yerine geçmez; Windows build ortamı
bulunmadığında sihirbaz kaynağı ile çalıştırılmış EXE açıkça ayrılır.

Git'e motor SDK/editor/toolchain, kullanıcı kayıtları/günlükleri veya sırlar
gönderilmez. Paket yalnız oyuna ait cooked runtime ve gerekli kabuğu içerir.
MIT proje kodu, GPL3 ayrı PyQt süreç kabuğu ve CC BY-NC4.0 kedi varlıklarının
koşulları ayrı kaydedilir. Yeni kullanıcı kaydı taşınmaz/sıfırlanmaz.

## Yatak, mama ve eylem geri bildirimi

Kullanıcı mama/oyun seçimlerine görünür tepki alamadığını bildirdi ve kedinin
arada gidip yatacağı kalıcı bir yatak istedi. İncelemede mama sayısal olarak
işlenirken yeme hareketi olmadığı, ayrıca native ekran sınırı düzeltmesinin
Play durumuna otomatik Rest gönderip topu hemen kapatabildiği bulundu.

0.4 diliminde özgün düzenlenmiş Engine basic şekillerinden yatak ve mama kabı,
ayrı sabit home-kamera RGBA yakalamasıyla masaüstünde kalır. Kedi kendi hareketli
kare akışını kullanır; sabit eşya penceresi yalnız gerçek Unreal çıktısı,
alpha/CRC ve aynı oturum/PID kapılarıyla gösterilir. Yatağa tıklamak dinlenme,
kaba tıklamak mama eylemidir. Sistem tepsisindeki eylemler korunur.

Kedi dinlenirken yatağa, beslenirken kaba yürür. Uyumlu ek artistik klip
bulunmadığı için sınırlı prosedürel dinlenme/yeme düzeltmesi gerçek motor
karelerinde incelenir; var olmayan uyuma animasyonu veya tam fiziksel kedi
simülasyonu iddia edilmez. Eylem kabulü motor serial/sonuç/mesajıyla gösterilir.
İstek yazmak tamamlanma kanıtı değildir. Feed/Play, hızlı önceki pet yüzünden
sessiz düşmez; sevme devam eden yeme/oyunu istemeden sonlandırmaz.

Önceki IK A/B ölçümünde stance kayması azalırken hızlı release hız sıçraması
görüldü. Yeni katman düzeltme hızını sınırlar ve salınıma yumuşak bırakır;
sonuç gerçek kare/ölçüm ile yeniden değerlendirilir. Yayın ve kurulum paketleri
bu son doğrulanmış Shipping arşivinden yeniden oluşturulur.
