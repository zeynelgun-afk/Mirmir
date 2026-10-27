# Mırmır — Unreal Engine masaüstü kedisi

Unreal Engine **5.8.4** kuruldu; proje derlendi, animasyonlu kedi ve gerçek
miyavlama/mırlama kayıtları içe aktarıldı. Gerçek Unreal RGBA görüntüsü,
şeffaf yerel masaüstü penceresinde çalıştırıldı. Bu ilk çalışan Unreal
sürümüdür; fotogerçekçi kürk ve kapsamlı davranış animasyonları henüz yoktur.

## Çalıştırma

```bash
./run.sh
./run.sh --stop
```

Başlatıcı hazır Linux Shipping paketini kullanır; editör açılmaz. Paket
bulunamazsa yerel motorun oyun kipini kullanır. Kurulu yol bilgileri
`.local/engine.json`, `.local/preview.json` ve `.local/package.json` içindedir.
Uygulama menüsünde **Mırmır — Masaüstü Kedisi** başlatıcısı kurulabilir:

```bash
./scripts/install-launcher.sh
```

- Kediyi tıklayarak veya sol tuş basılı üzerinde gezdirerek sev.
- Sağ tık / sistem tepsisi menüsü: mama, oyun, dinlenme, ses ve kapat.
- **Shift + sürükle:** kedinin masaüstündeki başlangıç yerini taşı.
- Kedi klavye odağı istemez; boş saydam alan pencere maskesinden çıkarılır.

Kayıtlar `~/.local/share/mirmir/unreal/`, günlükler ve canlı kareler
`~/.local/state/mirmir/unreal/` altında tutulur; XDG değişkenleri desteklenir.
Diğer uygulamaların görüntüsü/dosyaları veya mikrofon okunmaz. Oturum ve KWin
ayarları değiştirilmez; oturum açılışında otomatik başlatma kurulmadı.

## Mevcut sınırlar

Modelde sanatçının hazırladığı gerçek yürüyüş vardır. Animasyon hızı gerçek
ilerleme hızına bağlanır; dönüşte hareket bakış yönünü izler. Eksik bekleme
klibi için yürüyüşün düşük pati yüksekliği olan bir pozu tutulur. Bu, uyuma,
oturma veya temizlenme animasyonu değildir. Yerinde dönüş ve duruş geçişleri
hâlâ görsel geliştirme ister. Pati tabanı IK kilidi ve Groom uygulanmadı.

Kişisel kullanım modeli **CC BY-NC 4.0** lisanslıdır; kaynaklar ve değişiklikler
[lisans dosyasında](ASSET-LICENSES.md) kayıtlıdır. Sesler CC0 kayıtlardır.
[Güncel durum](docs/current-status.md) ve
[gerçek Unreal hareket ölçümü](docs/unreal-motion-verification.json) kanıtı
ve sınırları ayrı tutar.

## Geliştirme ve doğrulama

```bash
./run.sh --editor
./run.sh --preview
./unreal/Mirmir/scripts/build.sh -MaxParallelActions=2
./unreal/Mirmir/scripts/test-engine.sh
./scripts/check.sh
./unreal/Mirmir/scripts/package-game.sh
```

`check.sh`: C++ çekirdeği ASan/UBSan, yerel kabuk alpha/CRC/DPI/tek örnek
kontrolleri, varlık hash'leri ve betik sözdizimi. `test-engine.sh`: gerçek
Unreal otomasyon testleri. Hiçbiri tek başına fotogerçekçiliği veya kullanıcı
beğenisini kanıtlamaz. Ayrıntılar [Unreal](unreal/Mirmir/README.md) ve
[yerel kabuk](native-shell/README.md) belgelerinde.

İlk Godot fizibilite deneyi arşivdir; varsayılan ürün değildir ve kapalıdır.
Eski kayıtları korunur. Yalnız açık `./run.sh --lab` seçeneğiyle çalışır;
`./run.sh --stop-lab` kapatır. Geçmiş Godot ölçümleri Unreal kanıtı sayılmaz.

[Tasarım](docs/superpowers/specs/2026-10-27-mirmir-design.md) ·
[Uygulama planı](docs/implementation-plan.md)
