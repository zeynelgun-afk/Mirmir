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
