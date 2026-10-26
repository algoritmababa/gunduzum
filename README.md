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

Sekmeler: **Results** (sonuç dosyasının içeriği), **Category summary** (her ölçüm için kategori
bazında ortalama ± std, LOW→HIGH yönünde *trend* ve komşu kategorilerin *ayrışması*), **Log**.

İsteğe bağlı: "Also save per-frame values" ile kare bazında değerler ayrı bir CSV'ye yazılır.

## Sonuç tablosu (ortak veri sözleşmesi)

```
recording, category, note, source, window, start_frame, end_frame, frames,
roi_x, roi_y, roi_w, roi_h, measured_at,
jitter_mean, jitter_std, jitter_p95, sharpness_mean, sharpness_std, sharpness_p95,
intensity_mean, intensity_std, intensity_p95, scintillation_index
```

Bir satır = bir kaydın bir penceresi. Tüm çalıştırmalar aynı dosyaya eklenir; analiz uygulamada
(kategori özeti) veya Excel / Python / MATLAB ile yapılabilir. Sütunları farklı bir dosyaya ekleme
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
  turbulence_measure --summary results.csv
  ```
* `tests/tests.pro` → `turbulence_tests`: sentetik verilerle doğrulama (bilinen kayma, blur,
  parlaklık dalgalanması; pencereleme, CSV, kategori özeti).

## Kod yapısı (~2.000 satır; önceki sürüm ~8.400)

```
core/   saf C++11 + OpenCV
  FrameSource    klasör / video, sırayla kare okuma (bellekte yalnızca o anki kare)
  Metrics        Metric arayüzü + jitter / sharpness / intensity
  Measurement    kayıt → pencereler → özet (ROI + gri tonlama, iptal, ilerleme)
  ResultTable    ortak CSV tablosu, kategori özeti
  Stats          mean / std / P95
app/    Qt arayüzü (MainWindow, MeasureWorker, RoiView)
cli/    komut satırı
tests/  otomatik testler
```

**Yeni ölçüm eklemek:** `Metric`'ten türeyen bir sınıf (`name`, `description`, `reset`, `process`)
yazıp `createMetrics()` içine bir satır eklemek yeterli; tabloya sütunları, arayüze açıklaması ve
kategori özetine satırları otomatik gelir. (Sütunlar değiştiği için yeni bir sonuç dosyası
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
