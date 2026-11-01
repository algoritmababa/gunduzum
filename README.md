# Turbulence Measurement (minimal sürüm)

Sabit bir hedefin görüntülerinden atmosferik türbülansın görünür etkilerini ölçen bir **ölçüm ve
değerlendirme aracı**. Her kaydı tüm algoritmalarla aynı şekilde ölçer, sonucu ortak bir tabloya
yazar ve istendiğinde hangi algoritmanın etiketli kategorileri (LOW / MEDIUM / HIGH) ayırmaya
**fayda sağladığını** gösterir. Çalışmanın bütünü (aşamalar, geçiş kriterleri, çekim protokolü)
[CALISMA_PLANI.md](CALISMA_PLANI.md) belgesinde.

> Önceki kapsamlı sürüm (ön işleme, registration, parametre optimizasyonu) `v1-full` etiketinde /
> `e6d0584` commit'inde duruyor.

## Algoritmalar

İlk spesifikasyondaki 8 algoritmanın tamamı, 7 ölçüm olarak her kayıtta birlikte çalışır.
Ayarlanacak parametre yoktur, yani her kayıt aynı şekilde ölçülür. Her ölçüm kare başına bir sayı
üretir; pencere özeti `<ad>_mean`, `<ad>_std`, `<ad>_p95` olarak yazılır.

| Etki | Ölçüm | Algoritma | Kare başına değer | Çözünürlük* |
|---|---|---|---|---|
| Kayma | **jitter** | Phase correlation | ardışık iki kare arasındaki ROI kayması (alt-piksel) | 0.1 px |
| Kayma | **centroid** | Centroid jitter | hedefin ağırlık merkezinin kareden kareye kayması (Otsu, parlak/koyu hedef otomatik, yoğunluk ağırlıklı) | 0.1 px |
| Yerel hareket | **flow** | Optical flow (pyramidal LK) | köşe noktalarının ortalama hareketi (ileri-geri kontrollü, parlaklık dengelemeli) | 0.1 px |
| Bulanıklık | **sharpness** | Laplacian variance | Laplacian varyansı / ortalama parlaklık² | %2 |
| Bulanıklık | **gradient** | Gradient magnitude (Sobel) | ortalama gradyan büyüklüğü / ortalama parlaklık | %2 |
| Yapı değişimi | **change** | Temporal structure | önceki kareye ortalama mutlak fark / ortalama parlaklık | 0.002 |
| Parlaklık | **intensity** | Temporal intensity variance + scintillation | ROI ortalama parlaklığı; ek olarak `scintillation_index` = Var/Mean² | %2 |

\* Çözünürlük: algoritmanın güvenilir ölçebildiği en küçük değişim. Kategoriler arası toplam değişim
bunun altında kalırsa analiz onu etki saymaz ("below resolution"). Bu bir ölçüm özelliğidir,
türbülans eşiği değildir. `_std` feature'larında bağıl çözünürlük, ölçümün kendi düzeyine
(`_mean`) göre hesaplanır.

Ön işleme yok: yalnızca ROI kırpma ve gri tonlama. Keskinlik ve kenar ölçümleri parlaklığa
oranlandığı için global ışık ya da pozlama değişimi bu ölçümleri etkilemez.

## Kullanım

1. **Recording**: görüntü klasörü veya video aç.
2. **Label**: kategori (LOW / MEDIUM / HIGH veya yeni bir ad; boş = etiketsiz) ve not
   (saat, hava, pozlama…).
3. **ROI**: görüntü üzerinde sürükle (kayıt bazında hatırlanır).
4. **Measurement**: pencere uzunluğu (0 = tüm kayıt tek pencere) ve sonuç dosyası.
5. **RUN** → her pencere sonuç dosyasına bir satır olarak **eklenir**.

Sekmeler: **Results** (sonuç dosyasının içeriği), **Trend analysis** (aşağıda), **Log**.

İsteğe bağlı: "Also save per-frame values" ile kare bazında değerler ayrı bir CSV'ye yazılır.

## Trend analizi ve algoritma değerlendirmesi (istendiğinde, CSV üzerinden)

**Trend analysis** sekmesinde sonuç CSV'si seçilip **Analyze**'a basılınca, etiketli satırlar
üzerinden **tüm algoritmaların tüm feature'ları** için "LOW → MEDIUM → HIGH yönünde tutarlı bir
değişim var mı?" sorusu cevaplanır ve her algoritmaya bir karne notu verilir:

| Algoritma notu | Anlamı |
|---|---|
| ✓ **Useful** | En az bir feature tutarlı değişiyor: test edilmiş, anlamlı ve çözünürlüğün üstünde. |
| ◔ **Promising** | Sıralı değişim var ama test için kayıt yetersiz (ör. kategori başına 1 kayıt). |
| ~ **Not confirmed** | Test edildi; anlamlı ve sıralı bir değişim yok. |
| ✕ **No benefit** | Kategorilerle tutarlı bir değişim yok ya da değişim çözünürlüğün altında. |

Kategori sırası, Label alanındaki listenin sırasıdır; etiketsiz satırlar ve sıra dışı kategoriler
kullanılmaz.

Ekran:

* **Özet şeridi**: kaç kayıt (kategori başına), algoritma notlarının sayısı ve her algoritma için
  renkli bir düğme (tıklayınca en iyi feature'ına gider); veri yetersizse sarı uyarı.
* **Ölçüm listesi** (sol): algoritmalara göre gruplu, en faydalı algoritma en üstte; her grubun
  başlığında not ve en iyi feature; her feature için sonuç etiketi, `LOW → MEDIUM → HIGH` değer akışı
  ve kategorilerin karışıp karışmadığı (● categories apart / ◑ n overlapping).
* **Ayrıntı** (sağ, seçilen ölçüm):
  * sonucu düz cümleyle anlatan renkli kutu;
  * dört gösterge: eğilim gücü (ρ), tesadüf olasılığı (p, Holm düzeltmeli), LOW–HIGH farkı (d),
    toplam değişim (%) ve algoritmanın çözünürlüğü;
  * **kategori kartları** ve aralarında yüzde değişimli oklar (genel eğilimle aynı yöndeyse yeşil,
    tersiyse kırmızı); her kartta ortalama ± std, CV, aralık ve **kategori içi tutarlılık**
    ("consistent" = kategorinin hiçbir kaydı komşu kategorinin değer aralığına girmiyor);
  * **kayıtlar tablosu**: her kaydın değeri, komşu kategoriye karışan kayıtlar ⚠ ile işaretli,
    **kayıt içi kararlılık** (aynı kaydın pencereleri arasındaki değişim, CV %) ve not.

"Consistent" kararı için iki koşul birlikte gerekir: kategori ortalamaları sırayla artıyor/azalıyor
**ve** düzeltilmiş p < α (varsayılan 0.05). İstatistikler: Spearman sıra korelasyonu, permütasyon
testi (10.000 karıştırma, sabit tohum → tekrarlanabilir), Holm düzeltmesi, Cohen's d.

* **Analiz birimi kayıttır** (varsayılan): her kaydın pencereleri ortalanır, çünkü aynı kaydın
  pencereleri birbirine benzer ve ayrı örnek sayılırsa sonuç olduğundan güçlü görünür.
* **Ne kadar veri gerekir?** Sınır yok; **kategori başına tek kayıtla** da analiz çalışır:
  * Ortalamalar sırayla artıyor/azalıyorsa sonuç **"Increase / Decrease (untested)"** olur. Yön ve
    değişim gösterilir, ama istatistiksel test yapılmaz; çünkü tek kayıtla fark kategoriden değil
    kaydın kendisinden (gün, ışık, kamera) gelebilir.
  * Kategori içi yayılım, aralık ve karışma o kaydın **pencerelerinden** hesaplanır (pencere
    uzunluğunu kayıttan kısa seçin, ör. 300 kare).
  * Test için her kategoride en az 2 kayıt gerekir; 10 ölçüm birlikte test edildiği için anlamlı
    ("consistent") sonuç ancak **kategori başına ~3 kayıtla** çıkabilir.
* Sonuç **Save CSV…** ile kaydedilir (`<sonuç>_trend.csv`); komut satırı: `--analyze`.

## Sonuç tablosu (ortak veri sözleşmesi)

```
recording, category, note, source, window, start_frame, end_frame, frames,
roi_x, roi_y, roi_w, roi_h, measured_at,
jitter_*, centroid_*, flow_*, sharpness_*, gradient_*, change_*   (her biri _mean, _std, _p95)
intensity_mean, intensity_std, intensity_p95, scintillation_index
```

Bir satır = bir kaydın bir penceresi. Tüm çalıştırmalar aynı dosyaya eklenir; analiz uygulamada
(trend analizi) veya Excel / Python / MATLAB ile yapılabilir. Sütunları farklı bir dosyaya ekleme
reddedilir (algoritmalar eklendiğinde sütunlar değiştiği için yeni bir sonuç dosyası başlatın).

## Derleme

Gereksinimler: Qt 5.4+ (Widgets), OpenCV 3.x (4.x ile de derlenir), C++11.

**Qt Creator:** `TurbulenceAnalysis.pro` dosyasını aç → Qt 5 kiti → Run.

OpenCV sırasıyla aranır: qmake argümanı → pkg-config (`opencv` / `opencv4`) → standart dizinler.
Bulunamazsa: **Projects → Build → qmake → Additional arguments**:
`OPENCV_INCLUDE_DIR=/yol/include OPENCV_LIB_DIR=/yol/lib`.

İsteğe bağlı projeler:

* `cli/cli.pro` → `turbulence_measure` (toplu ölçüm, aynı tabloya yazar):
  ```bash
  turbulence_measure --input /veri/kayit1 --category LOW --roi 100,100,400,300 \
                     --window 300 --results results.csv
  turbulence_measure --analyze results.csv [--unit recording|window] [--out trend.csv]
  ```
* `tests/tests.pro` → `turbulence_tests`: sentetik verilerle doğrulama. Bilinen kayma, blur ve
  parlaklık dalgalanması; pencereleme, CSV, trend analizi. **Algoritma özgüllüğü**: her senaryoda
  yalnızca tek bir etki (titreşim / blur / parlaklık titremesi) değişir. Analiz o etkiye duyarlı
  algoritmaları *useful*, diğerlerini *useful değil* bulmalıdır:

  | Senaryo | Useful | Useful değil |
  |---|---|---|
  | sadece titreşim | jitter, centroid, flow, change | sharpness, gradient, intensity |
  | sadece blur | sharpness, gradient | jitter, centroid, flow, intensity |
  | sadece parlaklık titremesi | intensity, change | jitter, centroid, sharpness, gradient |
  | karıştırılmış etiketler | – | hepsi |

## Kod yapısı

```
core/   saf C++11 + OpenCV
  FrameSource    klasör / video, sırayla kare okuma (bellekte yalnızca o anki kare)
  Metrics        Metric arayüzü + 7 ölçüm (8 algoritma), çözünürlükleri
  Measurement    kayıt → pencereler → özet (ROI + gri tonlama, iptal, ilerleme)
  ResultTable    ortak CSV tablosu
  TrendAnalysis  kategori sırasıyla tutarlı değişim analizi (Spearman, permütasyon, Holm, d,
                 çözünürlük) ve algoritma karnesi
  Stats          mean / std / P95
app/    Qt arayüzü (MainWindow, MeasureWorker, RoiView, TrendView)
cli/    komut satırı
tests/  otomatik testler
```

**Yeni ölçüm eklemek:** `Metric`'ten türeyen bir sınıf (`name`, `description`, `reset`, `process`)
yazıp `createMetrics()` içine bir satır eklemek yeterli; tabloya sütunları, arayüze açıklaması ve
trend analizine satırları otomatik gelir. (Sütunlar değiştiği için yeni bir sonuç dosyası
başlatılmalıdır.)

## Bilinen sınırlar

* **jitter / centroid / flow** kamera ve platform titreşimini de ölçer (ROI'nin bütünü kayar).
  Hanning penceresi phase correlation'ın alt-piksel kestirimini sıfıra doğru yanlılar (sentetik
  testte büyük kaymalarda en fazla ~0.5 px).
* **centroid** ROI'de belirgin bir hedef ister; hedef yoksa değer NaN olur.
* **Çapraz duyarlılık**: hareket, ROI'ye giren ve çıkan dokuyu değiştirdiği için diğer ölçümlerin
  kareden kareye dalgalanmasını (`_std`) da az miktarda artırır. Çözünürlük kuralı bu küçük etkileri
  dışarıda bırakır. Gerçek veride farklı etkiler birlikte değişebilir; bu durumda karışan etkiler
  ancak CALISMA_PLANI'ndaki "sağlamlık" aşamasında ayrılabilir.
* **scintillation_index** nokta kaynakta en anlamlıdır; geniş ROI'de ortalama alma etkisi
  dalgalanmayı azaltır. Kameranın otomatik pozlaması kapalı olmalıdır.
* Pencere uzunluğuna tam bölünmeyen kalan kareler atılır.
