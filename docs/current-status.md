# Güncel yön ve açık işler — 2026-10-27

Kullanıcı, Unreal seçimine rağmen Godot prototipinin masaüstüne konmasını
eleştirdi ve yürüyüşte problem gözledi. Bu gerçek kullanıcı bildirimi,
otomatik durum ve pencere testlerinden daha yüksek önceliklidir.

## Karar

- Ana geliştirme yalnızca Unreal Engine 5.8 üzerinden sürecek.
- Godot masaüstü deneyi kapatıldı. Otomatik başlatma kurulmadı; uygulama
  menüsündeki eski Godot başlatıcısı devre dışı bırakıldı.
- Kök `run.sh` artık Unreal çalıştırır. Eski deney yalnızca açık `--lab`
  seçeneğiyle açılır. Kullanıcı kaydı korunur.
- Önceki Godot ölçümleri Unreal'ın çalıştığını veya yürüyüşün doğal olduğunu
  kanıtlamaz. Bu ölçümler geçmiş laboratuvar kanıtları olarak korunur.

## Yürüyüş kusuru

Fripouille modelinin orijinal GLB'sinde yürüyüş klibi yoktu. Laboratuvarda
kullanılan küçük prosedürel bacak dönüşleri, basış fazını ve gövdenin
masaüstünde ilerleyişini eşleştirmiyordu. Pati sabitleme/IK ve sanatçı tarafından
hazırlanmış gerçek hareket bulunmadığından yürüyüş ürün kabulünü geçmedi.

Yalnızca motoru değiştirmek bu kusuru çözmez. Yeni Unreal diliminde gerçek
animasyonlu varlık; döngü, hız, kök hareketi, ayak teması ve dört bacak
fazlarıyla birlikte doğrulanacak. İlk teknik incelemede gerçek animasyon
kanalları bulunması, görüntünün doğal olduğunu tek başına kanıtlamaz.

Kişisel kullanım için LostBoyz2078'in CC BY-NC 4.0 lisanslı “Cat Walk” modeli
`assets/personal-use/cat-walk.glb` olarak hazırlandı. Gerçek 24 karelik yürüyüş
döngüsü dokuz fazda bir varlık görüntüleyicisinde incelendi. Bu Unreal
oynatımı veya son görsel kalite kabulü değildir. Siyah/beyaz kısa tüylü
modelde Groom, bağımsız koşma ve oturma klibi yoktur. Unreal importu, oyun
hızıyla adım eşleştirmesi ve nihai gerçekçilik henüz bekliyor.

## Motor erişimi

Kurulu Unreal bulunamadı. Resmi Linux indirmesi mevcut Brave oturumunda
canlı açıldı ve “Epic Account Required” gösterdi. Kullanıcıya gerekli hesap
girişi bildirildi; oturum bekleniyor. Eski 5.0–5.2 clang araçseti tablosu
motor indirmesi değildir. İndirme hedefi güncel kararlı Unreal Engine 5.8
Linux hazır ZIP paketidir.

Motor erişimi sonrası sıra: arşivi açma → uyumlu SDK/derleme → UE projesini
açma → animasyonlu kediyi içe aktarma → gerçek yürüyüş kabulü → Unreal
masaüstü katmanı → kürk ve görsel cilalama. Bunlar henüz tamamlanmadı.
