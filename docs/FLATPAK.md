# Flatpak doğrudan dağıtımı

Bu yol, **AI katkılı, kendi depomuzdan indirilen bir `.flatpak` paketidir**;
Flathub başvurusu veya Flathub manifesti değildir. Uygulama kimliği
`io.github.zeynelgun_afk.Mirmir` olur. GitHub kullanıcı adındaki tire kimlikte
alt çizgiye dönüştürülür; bu kullanım [Flatpak kimlik kurallarına](https://docs.flatpak.org/en/latest/conventions.html#application-ids) uygundur.

## Flathub sınırı

2026-10-27 tarihinde kontrol edilen [Flathub gereksinimleri](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy),
başvurularda bilinen AI katkısının kapsamını açıklamayı ister; Flathub
manifestlerinde AI üretimi veya AI yardımı kabul etmez. AI ajanlarının başvuru
PR'ı veya inceleme etkileşimi üretmesi/otomasyonu da yasaktır. Bu nedenle bu
depodaki paketleme kodu Flathub'a gönderilmez, başvuru ve yanıt otomasyonu yoktur.
Uygulama kodu, belgeleri ve bu paketleme yolu önemli ölçüde AI üretimi içerir;
Unreal ve üçüncü taraf varlıkların kendi kaynak/lisansları ayrıdır.

İleride Flathub isteyen insan bakımcı, kuralları yeniden okumalı; bağımsız
manifesti kendisi hazırlamalı, gerçek kaynak ve dağıtım lisanslarını incelemeli,
AI kapsamını doğru açıklamalı ve başvuru/inceleme/bakımı kendisi yürütmelidir.
İnsan hazırlığı kabul garantisi değildir. Buradaki doğrudan paket Flathub'da
listelenmiş, doğrulanmış veya mağaza tarafından incelenmiş sayılmaz.

## Üretme

Önce gerçek Linux Shipping oyunu, özel CPython/PyQt ve lisansları içeren taşınabilir
`Mirmir` dizini hazırlanır. Ardından:

```bash
python packaging/flatpak/build-bundle.py \
  --stage /mutlak/yol/Mirmir --version 0.4.0
```

Betik dışarı çıkan symlinkleri ve mutlak paket içi yolları reddeder; gerçek ELF
Shipping binary'sini, Python'u ve lisans dizinini arar. Hazır dosyaları `/app`
altına yerleştirip `flatpak build-finish`, `build-export`, `build-bundle` çalıştırır.
Derleme yapılmadığı için bu yeniden paketleme adımında SDK veya flatpak-builder
gerekmemesi, oyunun kaynak derlemesi olmadığı anlamına gelir; temel alınan
Shipping oyununun ayrıca derlenmiş olması gerekir. Komutların işlevleri
[resmi başvuru belgesinde](https://docs.flatpak.org/en/latest/flatpak-command-reference.html) açıklanır.

Çıktı `.local/dist/flatpak/<sürüm>-<zaman>/` altında bundle, `SHA256SUMS`,
`result.json`, yerel OSTree repo ve işlem günlüğüdür. Şu aşamada paket GPG ile
imzalanmaz; SHA256 indirme bütünlüğü sağlar, yayıncı kimliğini doğrulamaz.

## Kurma ve çalıştırma

```bash
flatpak install --user ./Mirmir-0.4.0-x86_64.flatpak
flatpak run io.github.zeynelgun_afk.Mirmir
flatpak run io.github.zeynelgun_afk.Mirmir --stop
flatpak uninstall --user io.github.zeynelgun_afk.Mirmir
```

`org.freedesktop.Platform/x86_64/25.08` ve ilgili GPU uzantısı gerekir. Bundle
runtime'ı içermez; kurulum eksik runtime için internet isteyebilir. Paket uygulama
izinlerinde ağa erişim yoktur. Runtime kaynağının Flathub olması uygulamanın
Flathub'da yayınlandığı anlamına gelmez. Tek dosyalık paket ve bağımlılık sınırı
[Flatpak belgesinde](https://docs.flatpak.org/en/latest/single-file-bundles.html) açıklanır.

## Sandbox ve doğrulama

İzinler yalnız X11, paylaşılan IPC, DRI/GPU, PulseAudio ve
`org.kde.StatusNotifierWatcher` ile konuşmadır. Host/home dosya erişimi, genel
session/system bus veya network izni verilmez. X11 arayüzü masaüstü konumlandırma
ve arada anlık fare ilgisi için gerekir; Wayland üzerinde XWayland kullanılır.
X11 güvenlik sınırı güçlü ekran izolasyonu sağlamaz. PulseAudio izni Flatpak
tarafında ses girişine de olanak verir; uygulama mikrofonu kullanmaz. İzinlerin
gerçek kapsamı [resmi sandbox belgesinde](https://docs.flatpak.org/en/latest/sandbox-permissions.html) bulunur.

Kalıcı veri `~/.var/app/<kimlik>/` altında, canlı kareler uygulamanın kendi
`XDG_RUNTIME_DIR/app/<kimlik>` tmpfs altındadır. Normal masaüstü Mırmır'ın kaydı,
kilidi ve kare yolu paylaşılmaz. Başka uygulama veya runtime ayarları değiştirilmez.

Paketleme başarı kaydı tek başına çalışır ürün kanıtı değildir. Son doğrulamada
sandbox içinde gerçek Shipping PID/oturum, CRC eşleşmiş RGBA, kendi karelerinden
alpha maskesi, IPC ve düzgün kapanma gözlenmelidir. Gerçek test sonucu elde
edilmeden bundle için render veya kullanıcı tıklaması geçti denmez.

Kurulu **yalnız bu Flatpak kimliğini** özel test veri/state ad alanında açmak için:

```bash
python packaging/flatpak/verify-sandbox.py
```

0.4 yaşam alanı karesini ve hızlı mama/oyun komutlarını da zorunlu denetlemek için
`--require-props` eklenir. Flatpak XDG taban değişkenlerini başlangıçta yeniden
kurduğundan test, launcher'ın doğruladığı 32 haneli `MIRMIR_VERIFY_NAMESPACE`
değerini sandbox içinde uygular; normal Flatpak kaydı ve masaüstü ürün kaydıyla
aynı dizini kullanmaz.

Kullanıcının masaüstü oturumu kilitliyken KWin normal pencereleri map etmeyebilir.
Araç kilidi açmaz, oturumu veya pencere yöneticisi ayarını değiştirmez. Açık
`--allow-locked-session` tercihiyle render/CRC/IPC/kapanış kontrolleri sürdürülebilir;
raporda `actual_desktop_overlay_verified=false` ve kilitli oturum nedeniyle
doğrulanmayan görünürlük ayrıca kalır. Bu tercih gerçek masaüstü görünümünün,
odak/tıklamanın veya kullanıcı deneyiminin geçtiği anlamına gelmez.

Bu araç fare ilgisini kapatır, gerçek işaretçiye dokunmaz. Kendi gerçek karelerini
CRC ile eşleştirip ham PNG'yi saklar, alpha 0–255 ve Shipping durumunu denetler;
yalnız kendi kuyruğuna sev/dinlen komutları yazar ve kendi `--stop` yolunu sınar.
Takılmada yalnız oluşturduğu Flatpak instance kimliği kapatılır. Normal masaüstü
kedisinin veri dizini, süreci ve kilidi kullanılmaz. Artifaktlar
`.local/flatpak-verification/<test-kimliği>/` altında kalır; bu otomasyon gerçek
kullanıcı tıklamasını veya hoparlör sesini doğrulamaz.
