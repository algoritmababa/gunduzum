# Turbulence Measurement (minimal sürüm)

Sabit bir hedefin görüntülerinden, atmosferik türbülansın üç görünür etkisini ölçen küçük bir
**ölçüm aracı**. Karar vermez; her kaydı aynı şekilde ölçer ve sonucu ortak bir tabloya yazar.
Çalışmanın bütünü (aşamalar, geçiş kriterleri, çekim protokolü) [CALISMA_PLANI.md](CALISMA_PLANI.md)
belgesinde.

> Önceki kapsamlı sürüm (8 algoritma, ön işleme, registration, parametre optimizasyonu)
> `v1-full` etiketinde / `e6d0584` commit'inde duruyor.

## Ne ölçer?

| Etki | Ölçüm (kare başına) | Pencere özeti |
|---|---|---|
| Kayma / titreşim | **jitter**: ardışık iki kare arasındaki kayma (phase correlation, alt-piksel) | `jitter_mean`, `jitter_std`, `jitter_p95` |
| Bulanıklık | **sharpness**: ROI'nin Laplacian varyansı | `sharpness_mean`, `sharpness_std`, `sharpness_p95` |
| Parlaklık dalgalanması | **intensity**: ROI ortalama parlaklığı | `intensity_mean`, `intensity_std`, `intensity_p95`, `scintillation_index` (Var/Mean²) |

Ön işleme yok: yalnızca ROI kırpma ve gri tonlama. Ayar yok: herkes aynı ölçümü yapar.

## Kullanım

1. **Recording**: görüntü klasörü veya video aç.
2. **Label**: kategori (LOW / MEDIUM / HIGH veya yeni bir ad; boş = etiketsiz) ve not
   (saat, hava, pozlama…).
3. **ROI**: görüntü üzerinde sürükle (kayıt bazında hatırlanır).
4. **Measurement**: pencere uzunluğu (0 = tüm kayıt tek pencere) ve sonuç dosyası.
5. **RUN** → her pencere sonuç dosyasına bir satır olarak **eklenir**.

Sekmeler: **Results** (sonuç dosyasının içeriği), **Trend analysis** (aşağıda), **Log**.

İsteğe bağlı: "Also save per-frame values" ile kare bazında değerler ayrı bir CSV'ye yazılır.

## Trend analizi (istendiğinde, CSV üzerinden)

**Trend analysis** sekmesinde bir sonuç CSV'si seçilip **ANALYZE**'a basılınca, etiketli satırlar
üzerinden her ölçüm için "LOW → MEDIUM → HIGH yönünde tutarlı bir değişim var mı?" sorusu
cevaplanır. Kategori sırası, Label alanındaki listenin sırasıdır; etiketsiz satırlar ve sıra dışı
kategoriler kullanılmaz.

| Sütun | Anlamı |
|---|---|
| **Verdict** | consistent increase / consistent decrease / ordered, not significant / significant, not monotonic / no consistent trend / insufficient data |
| **rho** | Spearman sıra korelasyonu: değer ile kategori sırası (+1 hep artıyor, −1 hep azalıyor, 0 ilişki yok) |
| **p (Holm)** | Permütasyon testi (etiketler 10.000 kez karıştırılır, sabit tohum → tekrarlanabilir); tüm ölçümler birlikte test edildiği için Holm düzeltmeli |
| **d** | İlk ve son kategori arasındaki fark / kategori içi yayılım (Cohen's d) |
| kategori sütunları | ortalama ± std (n) |

"Consistent" kararı için iki koşul birlikte gerekir: kategori ortalamaları sırayla artıyor/azalıyor
**ve** düzeltilmiş p < alpha (varsayılan 0.05).

* **Analiz birimi kayıttır** (varsayılan): her kaydın pencereleri ortalanır, çünkü aynı kaydın
  pencereleri birbirine benzer ve ayrı örnek sayılırsa sonuç olduğundan güçlü görünür.
  "Window" seçeneği vardır ama iyimserdir.
* **Ne kadar veri gerekir?** Her kategoride en az 2 kayıt gerekir. 10 ölçüm birlikte test edildiği
  için, kategori başına 2 kayıtla (6 kayıt) hiçbir sonuç anlamlı çıkamaz; **kategori başına 3 kayıt**
  (9 kayıt) ilk anlamlı sonuçları verebilir. Az veri durumunda uygulama bunu not olarak yazar.
* Sonuç **Save analysis CSV…** ile kaydedilir (`<sonuç>_trend.csv`).

## Sonuç tablosu (ortak veri sözleşmesi)

```
recording, category, note, source, window, start_frame, end_frame, frames,
roi_x, roi_y, roi_w, roi_h, measured_at,
jitter_mean, jitter_std, jitter_p95, sharpness_mean, sharpness_std, sharpness_p95,
intensity_mean, intensity_std, intensity_p95, scintillation_index
```

Bir satır = bir kaydın bir penceresi. Tüm çalıştırmalar aynı dosyaya eklenir; analiz uygulamada
(trend analizi) veya Excel / Python / MATLAB ile yapılabilir. Sütunları farklı bir dosyaya ekleme
reddedilir.

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
* `tests/tests.pro` → `turbulence_tests`: sentetik verilerle doğrulama (bilinen kayma, blur,
  parlaklık dalgalanması; pencereleme, CSV, trend analizi: gerçek eğilimler bulunuyor,
  karıştırılmış etiketlerde yanlış eğilim çıkmıyor, az veride "insufficient data").

## Kod yapısı (~2.400 satır; önceki sürüm ~8.400)

```
core/   saf C++11 + OpenCV
  FrameSource    klasör / video, sırayla kare okuma (bellekte yalnızca o anki kare)
  Metrics        Metric arayüzü + jitter / sharpness / intensity
  Measurement    kayıt → pencereler → özet (ROI + gri tonlama, iptal, ilerleme)
  ResultTable    ortak CSV tablosu
  TrendAnalysis  kategori sırasıyla tutarlı değişim analizi (Spearman, permütasyon, Holm, d)
  Stats          mean / std / P95
app/    Qt arayüzü (MainWindow, MeasureWorker, RoiView)
cli/    komut satırı
tests/  otomatik testler
```

**Yeni ölçüm eklemek:** `Metric`'ten türeyen bir sınıf (`name`, `description`, `reset`, `process`)
yazıp `createMetrics()` içine bir satır eklemek yeterli; tabloya sütunları, arayüze açıklaması ve
trend analizine satırları otomatik gelir. (Sütunlar değiştiği için yeni bir sonuç dosyası
başlatılmalıdır.)

## Bilinen sınırlar

* **jitter**, ROI'nin bütününün kaymasını ölçer; kamera/platform titreşimi de buna dahildir.
  Hanning penceresi alt-piksel kestirimini sıfıra doğru yanlılar (sentetik testte büyük
  kaymalarda en fazla ~0.5 px).
* **sharpness** sahnenin kontrastına ve ışığa bağlıdır; farklı ışık koşullarındaki kayıtları
  karşılaştırırken dikkat.
* **scintillation_index** nokta kaynakta en anlamlıdır; geniş ROI'de ortalama alma etkisi
  dalgalanmayı azaltır. Kameranın otomatik pozlaması kapalı olmalıdır.
* Pencere uzunluğuna tam bölünmeyen kalan kareler atılır.
