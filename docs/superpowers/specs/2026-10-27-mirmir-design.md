# Mırmır — gerçekçi masaüstü kedisi

Tarih: 2026-10-27. Kullanıcı kısa tüylü gerçekçi tekir yönünü ve geliştirmeyi onay istemeden sürdürmeyi kabul etti.

## Deneyim

Mırmır, pencere çerçevesi olmadan masaüstünde yaşayan tek bir kedidir. Gerçek anatomili bir model, yumuşak animasyon geçişleri, göz ve baş takibi, bağımsız kulak ve kuyruk tepkileri, nefes alma ve gerçek kedi sesleri canlılık hissini birlikte oluşturur. Masaüstü modunda çalışma alanını kapatmaz ve klavye odağını çalmaz. Etkileşim alanının dışında kalan tıklamalar alttaki uygulamaya ulaşır.

Fareyle sevme, mama verme, top/olta oyunu ve uyku davranışları ilk oynanabilir dilimi oluşturur. Enerji, tokluk, sosyal bağ ve merak davranış seçiminde kullanılır. Kedinin ilgisi kendiliğinden değişir; sabit animasyon listesini sırayla oynatmak yeterli sayılmaz. Ayrıyken ihmal cezası, ölüm veya kayıp yoktur. Bir geri alma/sıfırlama yolu ve açık bir kapatma eylemi bulunur.

## Motor ve iki ayrı teslimat

Ana ürün Unreal Engine 5'tir. Gerçekçi kürk için Groom; hayvan animasyonları için iskelet, animasyon grafiği ve gerektiğinde prosedürel düzeltmeler kullanılır. Unreal'ın bu makinede bulunmadığı ve resmi prebuilt indirmesinin Epic hesabı gerektirdiği doğrulandı. Kullanıcı hesabına giriş yapılamadığı için motorun kurulmuş veya Unreal projesinin derlenmiş olduğu iddia edilmeyecek.

`unreal/`: hesap erişimi sağlandığında açılabilecek Unreal proje kaynakları, davranış/etkileşim çekirdeği, varlık içe aktarma yönergesi ve performans doğrulama adımları.

`desktop-lab/`: bu bilgisayarda kurulu Godot 4.7.1 ile gerçekten çalıştırılacak ayrı masaüstü fizibilite prototipi. Bu bir Unreal sürümü değildir; ekran katmanı, kayıt, etkileşim ve oynanabilir davranışlar bu katmanda doğrulanır. Kullanılabilir lisanslı kedi varlığı kalitesi, gerçekçi nihai kürk hedefinden ayrı raporlanır.

## Bileşenler

- Davranış: ihtiyaçlar, güven, aktivite seçimi ve zaman aşımı. Görüntüleme motorundan bağımsız tasarlanır; rastgelelik tekrar üretilebilir olmalıdır.
- Görsel karakter: model/iskelet, animasyon katmanı, göz/baş/kuyruk hareketi; varlık yoksa hata açıkça bildirilir.
- Masaüstü adaptörü: şeffaflık, üstte kalma, DPI, ekran sınırları ve giriş alanı. İlk test XWayland üzerinde yapılır; kullanıcı oturumu değiştirilmez.
- Etkileşim: sevme, mama ve oyuncaklar; görünür durum ve menü. Erişilebilir kapatma kısayolu ve sistem menüsü.
- Ses: gerçek kayıtlar varsa lisanslarıyla birlikte; yoksa sentez yalnızca prototip sesi olarak belirtilir. Sessize alma ve ses seviyesi.
- Kayıt: sürümlü JSON, bozuk kayıttan güvenli dönüş, XDG dizinleri; otomatik başlatma varsayılan olarak kapalı.

## Performans ve gerçekçilik

Makine: yaklaşık 32 GB RAM, AMD Strix Radeon 880M/890M entegre GPU, KDE/Wayland; ekran ölçeği 2. Sürekli çalışan masaüstü modunda sınırlı render alanı ve 30 FPS hedeflenir; hareketsizken render hızı düşürülebilir. Hedefler ölçüm değildir. Tam ekran efektleri, pahalı sahne aydınlatması ve sürekli yoğun kürk simülasyonu başlangıç varsayımı değildir. Yakın görünümde kalite artırılabilir.

Gerçekçilik için model ve rig kalitesi birinci sınıf bağımlılıktır. Ücretsiz varlık uygun değilse mevcut sınır açıkça yazılır; düşük ayrıntılı geometriye fotogerçekçi denmez. Ücretli satın alma yapılmaz.

## Doğrulama

1. İhtiyaçların sınırları, etkileşim etkileri, bozuk kayıt ve uzun çevrimdışı süre davranışı otomatik test edilir.
2. Godot projesi editör içe aktarması ve başsız denetimden geçer; sonra gerçek grafik oturumunda açılır.
3. Şeffaf pencere ve X11 giriş şekli sorgulanır; uygulama dışı tıklama, odak ve ekran sınırları gerçek oturumda denetlenir.
4. Render ekran görüntüsüyle modelin görünürlüğü kontrol edilir. Kayıt ve kapat/yeniden aç akışı sınanır.
5. Unreal kurulmadıkça derleme, Groom performansı ve Unreal masaüstü entegrasyonu doğrulanmamış olarak kalır.

## Sonraki ürün dilimleri

Önce yaşayan karakter ve masaüstü katmanı; sonra gerçekçi rig/kürk ve animasyon cilası; ardından olta/top oyunları, tercihlerin öğrenilmesi ve yaşam alanları; son olarak ses çeşitliliği ve performans profilleri. Bunlar tek oturumda bitmiş bir ürün olarak sunulmaz.
