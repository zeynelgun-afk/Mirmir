# Mırmır — Unreal Engine masaüstü kedisi

Ana ürün **Unreal Engine 5.8** ile geliştiriliyor. Kullanıcının çok gerçekçi
görünüm ve doğal yürüyüş hedefi geçerlidir. Resmî 5.8.4 Linux motor indirmesi
başlatıldı; motor kurulup UE modülü derlenene kadar Unreal çalıştırıldı denmez.

İlk Godot masaüstü deneyi kapatıldı. Kullanıcı yürüyüşte sorun bildirdi;
prosedürel bacak hareketi ürün kabulünü geçmedi. Godot sürümü ana ürün veya
Unreal üzerinde alınmış bir sonuç olarak sunulmaz. [Güncel durum](docs/current-status.md).

## Unreal kaynakları

Proje: [Mirmir.uproject](unreal/Mirmir/Mirmir.uproject).

```bash
export MIRMIR_UE_ROOT=/absolute/path/to/UnrealEngine
./unreal/Mirmir/scripts/build.sh
./run.sh --editor
```

Masaüstü başlatma: `./run.sh`; normal pencere oyun önizlemesi: `./run.sh --preview`.
Masaüstü katmanı gerçek Unreal RGBA çıktısını yerel Qt penceresinde gösterir;
motor/kare yoksa kedi göstermez. Godot'a geçmez. Unreal kurulumu, varlık içe aktarma,
Animation Blueprint ve masaüstü katmanı için [Unreal belgesine](unreal/Mirmir/README.md) bakın.

Şu anda kaynaklarda ihtiyaç/aktivite çekirdeği, etkileşimler, XDG kayıtları,
Blueprint/animasyon bağlantı noktaları ve lisanslı kaynaklar için editör
import yardımcısı vardır. Bunlar derlenmiş, doğal yürüyen veya fotogerçekçi
bir Unreal kedi sürümü değildir. Varsayılan kaynak başlangıcı için varlıkların
atanması gerekir. Groom ve gerçek Unreal masaüstü entegrasyon testi bekliyor.

Motor için [resmî Linux sayfasından](https://www.unrealengine.com/en-US/linux)
kararlı 5.8 Linux hazır ZIP'i kullanılacak. Eski 5.0–5.2 clang araçseti
tablosu motor indirmesi değildir. Hesap girişi kullanıcıya aittir.

## Yürüyüş kabulü

Yeni varlıkta “animated” etiketi yeterli değildir. Gerçek yürüyüş klibi,
başlangıç/bitiş döngüsü, dört bacak fazları, pati basışı, kök hareketi ve
gövdenin hızı birlikte incelenecek. Hareket hızı kliple eşleşecek; gerekirse
ayak sabitleme/IK uygulanacak. Bağımsız baş, kulak ve göz animasyonları ayrıca
doğrulanacak. Varlık değerlendirmeleri kaynak ve lisanslarıyla kaydedilir;
ücretli satın alma yapılmadı.

## Otomatik kontroller

```bash
./scripts/check.sh
```

Bu komut motor bağımsız Unreal C++ çekirdeğini Clang/ASan/UBSan ile, yerel
masaüstü katmanının alpha/CRC/DPI testlerini, kaynak varlık hash'lerini ve
betik sözdizimini denetler. Arşiv Godot testleri yalnız `--include-lab` ile
eklenir. UE/UHT derlemesini, gerçek animasyon kalitesini veya Unreal masaüstü
davranışını doğrulamaz.

## Arşiv Godot deneyi

`desktop-lab/` şeffaflık, XWayland giriş şekli, ses ve kayıt için bir
fizibilite çalışmasıdır. Otomatik başlatılmaz ve varsayılan başlatıcı değildir.
Yalnızca açık istekle teknik inceleme için:

```bash
./run.sh --lab
./run.sh --stop-lab
```

Eski veriler `${XDG_DATA_HOME:-~/.local/share}/mirmir/` altında korunur.
Unreal verileri ayrı `mirmir/unreal/` dizinindedir; iki kayıt biçimi arasında
parite veya göç doğrulanmadı. Deneyin [masaüstü ölçümleri](docs/desktop-verification.md)
geçmiş kanıttır. Bunlar kullanıcının yürüyüş kusuru bildirimini geçersiz kılmaz.

Diğer uygulamaların görüntüsü/dosyaları okunmaz; mikrofon açılmaz. Deneyde
yalnızca kendi çizilmiş görüntüsünden giriş poligonu hesaplanmıştır.

[Tasarım](docs/superpowers/specs/2026-10-27-mirmir-design.md) ·
[Uygulama planı](docs/implementation-plan.md) ·
[Model ve ses kaynakları / lisanslar](ASSET-LICENSES.md)
