# Çalışma Planı

**Soru:** Atmosferik türbülans değiştiğinde, görüntüden ölçülen hangi sayılar güvenilir ve
anlamlı şekilde değişir?

Çalışma, her biri **tek bir soruyu** cevaplayan küçük aşamalara bölünür. Bir aşamayı geçemeyen
ölçüm bir sonrakine geçmez. Aşamaları birbirine bağlayan tek şey **ortak sonuç tablosudur**
(bkz. README → Sonuç tablosu).

```
[1] Veri  →  [2] Doğrulama  →  [3] Duyarlılık  →  [4] Sağlamlık  →  [5] Karar (en son)
```

Türbülansın üç etkisi üç **bağımsız alt çalışmadır**; her biri aşamalardan ayrı ayrı geçer:

| Alt çalışma | Etki | Ölçüm |
|---|---|---|
| A | Kayma / titreşim | jitter |
| B | Bulanıklık | sharpness |
| C | Parlaklık dalgalanması | intensity / scintillation_index |

Birinin başarısız olması diğerlerini etkilemez ("C işe yaramadı, A çok iyi" geçerli bir sonuçtur).

---

## Aşama 1 — Veri
**Soru:** Kayıtlar birbiriyle karşılaştırılabilir mi?

Çekim protokolü:
- Aynı hedef, aynı mesafe, aynı kamera ve lens.
- **Elle pozlama ve sabit kazanç** (otomatik pozlama kapalı), sabit odak.
- Kamera sağlam bir sehpada; mümkünse rüzgârdan korunmuş.
- Her kayıt aynı uzunlukta (ör. 6000 kare) ve aynı kare hızında.
- Her kategori için **en az 3 ayrı kayıt**, mümkünse farklı günlerden.
- Bir adet **türbülanssız referans kayıt** (kısa mesafe veya kapalı ortam, aynı kamera ayarları).

Her kayıt için not alanına (veya ayrı bir künye tablosuna): tarih/saat, kategori, ışık durumu,
hava (sıcaklık, rüzgâr), pozlama/kazanç, varsa Cn² / r0 ölçümü.

**Çıktı:** kayıtlar + künye. **Geçiş:** protokol uygulandı, künye eksiksiz.

## Aşama 2 — Doğrulama (her ölçüm için)
**Soru:** Ölçüm, ölçtüğünü iddia ettiği şeyi doğru ölçüyor mu?

- Sentetik testler (`turbulence_tests`) geçiyor: bilinen kaymayı, blur'u, parlaklık dalgalanmasını
  doğru yönde ve yaklaşık doğru büyüklükte buluyor.
- **Gürültü tabanı:** türbülanssız referans kayıttaki değer.
- **Tekrarlanabilirlik:** aynı koşuldaki iki kayıt benzer değer veriyor.

**Geçiş:** ölçüm doğru çalışıyor ve referans kayıttaki değeri, türbülanslı kayıtlardakinden belirgin
şekilde küçük (veya sharpness için büyük).

## Aşama 3 — Duyarlılık
**Soru:** Ölçüm türbülans kategorisiyle değişiyor mu?

Uygulamadaki **Category summary** sekmesi bu aşamanın aracıdır:
- *Trend*: kategori ortalamaları LOW → HIGH sırasıyla tutarlı şekilde artıyor/azalıyor mu?
- *Separation*: komşu kategoriler arasındaki fark, kategori içi yayılımdan büyük mü?

**Geçiş:** trend tutarlı (increasing/decreasing) ve ayrışma en azından LOW–HIGH arasında açık.
Sonuç, pencereler değil **kayıtlar** bazında da aynı olmalı (aynı kaydın pencereleri birbirine benzer).

## Aşama 4 — Sağlamlık
**Soru:** Değişimin sebebi gerçekten türbülans mı?

Künyedeki diğer değişkenlerle karşılaştır:
- Aynı kategoride farklı ışık / saat koşulundaki kayıtlar benzer sonuç veriyor mu?
- Ölçüm, türbülansla ilgisi olmayan bir değişkenle (ör. sadece ışık seviyesiyle) daha güçlü mü
  değişiyor?

**Geçiş:** ölçüm türbülans kategorisine duyarlı, diğer koşullara görece duyarsız.

## Aşama 5 — Karar (opsiyonel, en son)
Aşama 4'ü geçen az sayıdaki ölçümle, gerçek verilerden eşik veya basit bir sınıflandırıcı kurulur
ve **optimizasyonda kullanılmamış kayıtlarla** doğrulanır. Bu aşamaya kadar hiçbir eşik
belirlenmez.

---

## İlk adım (önerilen)
1. Alt çalışma A (jitter) ile başla.
2. Protokolle 3 kategori × 3 kayıt + 1 referans kayıt topla.
3. Hepsini aynı sonuç dosyasına ölç (aynı ROI, aynı pencere uzunluğu).
4. Aşama 2 → 3 → 4'ü jitter için uygula; sonra B ve C.

## Kayıt tutma
Her aşamanın sonucu tek satırla not edilir:

| Ölçüm | Doğrulama | Duyarlılık | Sağlamlık | Not |
|---|---|---|---|---|
| jitter_std | | | | |
| sharpness_mean | | | | |
| scintillation_index | | | | |
