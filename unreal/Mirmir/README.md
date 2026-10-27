# Mırmır — Unreal kaynak başlangıcı

Bu dizin Unreal Engine 5 ana ürününün **kaynak başlangıcıdır**. Motor bu
makinede kurulu bulunmadı; UE modülü, UHT, editör açılışı, Groom, ses ve grafik
performansı **derlenmedi/çalıştırılmadı**. Ana ürün yalnızca Unreal Engine
**5.8 stable** hedefler. Eski `desktop-lab/` çalışması durduruldu/donduruldu;
ürünün motoru veya yürüyüş doğrulama yolu değildir.

## Şu anda ne var?

- UE 5.8 hedefleyen `.uproject`, oyun/editör target'ları ve C++ modülü.
- Motor bağımsız ihtiyaç/aktivite çekirdeği: tokluk, enerji, bağ ve merak;
  dinlenme, dolaşma, temizlenme, ilgi isteme, oyun, yemek ve sevilme.
- Blueprint'e açık kedi aktörü, iskeletli model bileşeni, animasyon durumu,
  hareket hızı, aktivite/etkileşim/görsel güncelleme olayları ve isteğe bağlı
  lisanslı ses yuvaları. Oyun davranışı henüz top/olta fiziği değildir.
- `P` sevme, `F` mama, `B` oyun, `M` sessiz, `Esc` çıkış. Modelin fizik varlığı
  visibility trace kabul ediyorsa modele tıklama da sever.
- 30 saniyede ve normal kapanışta sürümlü JSON kaydı. PRNG durumu saklanır.
  Çevrimdışı dinlenme en fazla 8 saat işlenir; ayrılık tokluk, bağ veya merakı
  azaltmaz. Bozuk kayıt korunur ve açık `ResetCat` eylemine kadar kayıt kapatılır.

Paylaşılan kaynak varlıklar projede zaten var: [Cat Walk GLB](../../assets/personal-use/cat-walk.glb)
LostBoyz2078 tarafından hazırlanmış, gerçek in-place yürüyüş klibi içeren kedi
modelidir. **CC BY-NC 4.0: yalnızca yerel kişisel, ticari olmayan prototip**;
[ayrı lisans ve kaynak kaydını](../../assets/personal-use/sources.json) koruyun.
Bu varlık ticari ürün/dağıtım varlığı olarak kabul edilmez.
Yedek [Fripouille GLB](../../desktop-lab/assets/cat/fripouille.glb), guillaume bolis
tarafından hazırlanmış rig'li modeldir (CC BY 4.0), fakat authored yürüyüş içermez.
[Gerçek kedi kayıtları](../../desktop-lab/assets/audio/) Joseph SARDIN /
BigSoundBank kaynaklıdır (CC0). [Lisans kaydını](../../ASSET-LICENSES.md) koruyun.
Bu dosyalar henüz Unreal `.uasset` biçimine içe aktarılmadı ve C++ aktörüne
atanmadı. Cat Walk kaynağında yaklaşık 0.767 saniyelik tek yürüyüş klibi var;
ayrı idle/run/sit animasyonları ve Groom yoktur. Unreal'da import, iskelet
uyumu, ayak teması ve hareket hızı hâlâ test gerektirir. Eski laboratuvarın
prosedürel bacak hareketleri kullanılmaz.
Varsayılan sahne başlangıcı kamerayı, ışığı ve durum yazılarını açar;
**varlıkları atayana kadar kedi görünmez**.
Masaüstü şeffaflığı, KWin katmanı ve uygulamalar arası tıklama geçişi bu Unreal
modülünde uygulanmış değildir.

## Motoru edinme ve çalıştırma

[Resmî Linux indirmesi](https://www.unrealengine.com/en-US/linux) Epic hesabına
giriş gerektirir. Hesap girişi/lisans kabulü yapılmadı. Resmî ZIP erişimi
olmadan otomatik motor kurulumu yapılamadı. Kaynak koddan motor derlemesi de
Epic'in GitHub erişimini gerektirir.

Motor resmî kaynaktan açıldıktan sonra, kendi dizininize göre:

```bash
export MIRMIR_UE_ROOT=/absolute/path/to/UnrealEngine
./scripts/build.sh
"$MIRMIR_UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$PWD/Mirmir.uproject"
./scripts/run.sh
./scripts/test-engine.sh
```

`run.sh` normal pencere açar ve 30 FPS **hedefi** koyar; bu bir ölçülmüş
performans değeri değildir. İşletim sistemi oturumunu veya KWin ayarını
değiştirmez. Editör başlangıç haritası `/Engine/Maps/Entry`; kalıcı ürün
haritası oluşturulup test edildikten sonra proje ayarına yazılmalıdır.

Veri: `${XDG_DATA_HOME:-$HOME/.local/share}/mirmir/unreal/cat-v1.json`.
Başlatıcı günlüğü: `${XDG_STATE_HOME:-$HOME/.local/state}/mirmir/unreal/session.log`.
Motor kendi editör/önbellek dosyalarını ayrıca proje `Saved/` ve
`DerivedDataCache/` dizinlerinde oluşturabilir; ürün kaydı bunlardan ayrıdır.
Bozuk kaydı sıfırlamadan önce kopyalamak mümkündür. `ResetCat`, açık bir kayıt
sıfırlama eylemidir; kendi kendine tetiklenmez.

## Gerçek kedi varlığı ekleme

Yeniden model/ses indirmenize gerek yok. İlk içe aktarma için proje içindeki
`../../assets/personal-use/cat-walk.glb` ve üç `.ogg` kaydını kullanın.
`scripts/import-assets.py` bu yerel dosyalara `unreal.AssetImportTask` uygular,
oluşan nesne yollarını, SkeletalMesh/Skeleton/PhysicsAsset durumunu ve
AnimSequence yolları/sürelerini günlüğe yazar. Cat Walk varsa onu önceliklendirir;
yoksa yürüyüş klibi bulunmayan Fripouille'e açık uyarıyla döner. Cat Walk importu
animasyon üretmediyse başarısız sayar. Mevcut `/Game/Mirmir/Shared` içeriği varsa
üzerine yazmadan durur.

Projede Python Editor Script Plugin (`PythonScriptPlugin`) etkin olarak
tanımlandı. Editörde Interchange Editor ve Interchange Framework eklentilerini
kontrol edin; Epic bunların varsayılan olarak etkin olduğunu belgeliyor.
Motor sürümündeki glTF/GLB desteğini doğrulayın. Helper, özel bir importer
veya eklenti indirmez ve kurulu pipeline'ı geçersiz kılmaz.

Motor kurulduktan ve C++ modülü derlendikten sonra, bu dizinden:

```bash
"$MIRMIR_UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" \
  "$PWD/Mirmir.uproject" -run=pythonscript \
  -script="$PWD/scripts/import-assets.py" -unattended -nop4
```

Alternatif: açık editörde Tools → Execute Python Script ile helper'ı seçin.
Bu yalnızca editör içe aktarmasıdır; helper motor olmadığı için **henüz
çalıştırılmadı**. GLB'nin UE'de rig, malzeme ve ölçek bakımından doğru çıktığı
varsayılmaz. Hatalı/kısmi içe aktarma açık hata verir; oluşmuş varlıklar
incelenir, kendiliğinden silinmez. `.ogg` UE'nin desteklediği içe aktarma
biçimlerindendir; sesler yeniden indirilmez veya kayıplı dönüştürülmez.

1. [Lisanslar](../../ASSET-LICENSES.md), `assets/sources.json` ve yaratıcı
   kredisini Unreal dağıtımına taşıyın. CC BY 4.0 model değişikliklerini belirtin.
2. İçe aktarılan iskeletli model, dokular, fizik varlığı ve ölçeği denetleyin:
   Unreal santimetre kullanır. Cat Walk'ta tek in-place yürüyüş klibi vardır;
   Fripouille yedeğinde authored klip yoktur. Animasyonlar **in-place** olmalı;
   kök hareketini kapatın çünkü
   konum hareketi şimdilik aktör tarafından uygulanır.
3. `AMirmirCat` tabanlı `BP_MirmirCat` oluşturun. Class Defaults içinde
   `CatSkeletalMesh` alanına import edilmiş mesh'i, `WalkAnimation` alanına
   aynı skeleton'a ait authored walk AnimSequence'ini atayın. Ayrı idle varsa
   `IdleAnimation` alanına atayın. Fizik varlığını, ölçeği ve +X ileri yönünü
   denetleyin; gerekirse mesh'in yerel dönüşünü düzeltin. `AuthoredWalkSpeed`
   klibin 1.0 oynatma hızındaki cm/s değeridir: `WalkSpeed` ile eşleştirin ve
   gerçek ayak temasını izleyerek ölçün. Değer henüz varlıkla kalibre edilmedi.
   Animation Blueprint seçilmemişse C++ tek-klip yürütücü yalnızca hareket
   sırasında walk çalar, oynatma hızını gerçek hareket hızına uyarlar. Durunca
   idle klibe geçer; idle yoksa referans pozunda bekler. Olduğu yerde yürüme
   veya prosedürel bacak döndürme uygulanmaz. Tek yürüyüş klibi koşma/oturma
   animasyonu sağlamaz.
4. Daha zengin davranışlar için `UMirmirAnimInstance` tabanlı Animation
   Blueprint oluşturun. Atanmış bir AnimBlueprint varsa C++ tek-klip yürütücü
   karışmaz. `Activity`
   için durum makinesi, `Speed` için yürüyüş geçişi bağlayın. Göz, nefes, kulak
   ve kuyruk eklemlerini kendi rig'inize göre ayarlayın. Bu sanat çalışması
   henüz yapılmadı. Hareket geçişleri kaymadan/yumuşakça sınanmalıdır.
5. `AMirmirGameMode` tabanlı `BP_MirmirGameMode` içinde `CatClass` değerini
   `BP_MirmirCat` yapın. Yeni bir ürün haritasında World Settings → GameMode
   Override alanına bu sınıfı verin; proje başlangıç haritasını bu haritaya
   ayarlayın. Varsayılan C++ sınıfı otomatik olarak bu Blueprint'i bulmaz.
6. İçe aktarılan `purr` SoundWave'ini `PurrSound` alanına atayın. İki gerçek
   miyav kaydı gerektiğinde Blueprint olaylarında kullanılabilir; yemek veya
   oyuncak sesi gibi yanlış adlandırılmamalıdır. `FeedSound` ve `PlaySound`
   için uygun ayrı kayıtlar henüz yoktur. Seviyeyi gerçek dinlemede ayarlayın.

Resmî API dayanakları: [AssetImportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetImportTask),
[Python editör kurulumu](https://dev.epicgames.com/documentation/unreal-engine/scripting-the-unreal-editor-using-python),
[Interchange](https://dev.epicgames.com/documentation/unreal-engine/importing-assets-using-interchange-in-unreal-engine),
[ses biçimleri](https://dev.epicgames.com/documentation/unreal-engine/importing-audio-files).

Eski laboratuvar kayıt dosyaları/enum değerleri Unreal'ın taşınabilir formatı
olarak kabul edilmez; otomatik kayıt göçü doğrulanmadı. Ürün geliştirmesi yalnız
Unreal üzerinde sürer.

## İsteğe bağlı Groom

Groom başlangıç bağımlılığı değildir; düşük maliyetli baseline'ı korur.
Gerekirse editörde Hair Strands/Groom ve Alembic Groom Importer eklentilerini
açın. Project Settings → Rendering → Support Compute Skin Cache ayarını açıp
editörü yeniden başlatın. İçe aktarılan Groom için doğru iskelete Groom
Binding oluşturun ve Blueprint'e GroomComponent olarak ekleyin.

Kürk tel sayısı, gölge, simülasyon, LOD ve kürk kartı seçeneklerini AMD entegre
GPU üzerinde ölçmeden ürün varsayımı yapmayın. Yakın görünüm ve masaüstü
profillerinin kalite düzeyleri ayrı ölçülmelidir. [Epic Groom platform
desteği](https://dev.epicgames.com/documentation/unreal-engine/groom-platform-support-in-unreal-engine).

## Doğrulama

Motor gerektirmeyen anlamlı çekirdek testi:

```bash
./scripts/test-core.sh
```

Bu test gerçek C++ derleyici ile ASan/UBSan altında etkileşimlerin etkisini,
giriş tekrar sınırlamasını, düşük enerjide dinlenmeyi, uzun ayrılıkta cezasız
dönüşü, çevrimdışı üst sınırı, kayıt durumu geri yüklemesinin deterministik
olmasını, geçersiz sayılara dayanıklılığı ve uzun simülasyonda sınırları sınar.
JSON dosya I/O testini veya UE modülünün derlenmesini kanıtlamaz.

UE kurulduktan sonra kabul kontrolü:

- [ ] `build.sh` UHT ve Linux C++ derlemesini tamamladı.
- [ ] `test-engine.sh` iki `Mirmir.Behavior` testiyle geçti; JSON bozuk kayıt,
      disk yazma hatası ve yeniden açılış ayrıca gerçek dosya sistemiyle sınandı.
- [ ] Lisanslı model/rig/animasyonlar atandı; gerçek grafik oturumunda göründü.
- [ ] Sevme, mama, oyun, sessiz ve Esc çıkışı gerçek girişle çalıştı.
- [ ] Normal kapanış ve tekrar açılışta bağ ve durum korundu; gelecek tarihli
      kayıt ceza oluşturmadı; bozuk dosya kendi kendine üzerine yazılmadı.
- [ ] Görüntü gerçekçiliği; adım kayması, gözler, nefes, kulak, kuyruk ve
      animasyon geçişleri gözle değerlendirildi.
- [ ] Ayrı masaüstü adaptörü: şeffaflık, üstte kalma, odak çalmama, kedi dışı
      tıklama geçişi, DPI, çoklu ekran ve tam ekran uygulama davranışı doğrulandı.
- [ ] Sürekli kullanımda GPU/CPU/bellek, enerji, sıcaklık ve kare süreleri
      ölçüldü. UE editörü ölçümü ile paketlenmiş uygulama ölçümü ayrıldı.

Saf Wayland desteği varsayılmamalıdır. Epic, editörde saf Wayland giriş
sorunlarını ve oyun hedeflerinin henüz test edilmediğini bildiriyor;
[SDL3/Linux belgesi](https://dev.epicgames.com/documentation/unreal-engine/updating-unreal-engine-on-linux-to-sdl3).
Bu başlangıç şeffaf masaüstü entegrasyonunu çözmüş gibi sunulmaz.
