# Atmospheric Turbulence Image Analysis Platform

C++ / Qt Widgets / OpenCV 3.x tabanlı, **görüntüden türetilen feature araştırma platformu**.
Sabit bir hedefe ait frame setlerinde klasik görüntü işleme algoritmalarını çalıştırır ve
sonuçları yalnızca **sayısal** olarak (tablo, CSV, JSON) üretir.

> Bu uygulama bir "turbulence meter" değildir. Classifier, LOW/MEDIUM/HIGH kararı, sabit
> eşik değerleri ve grafik/plot **bilinçli olarak yoktur**. Amaç: atmosferik koşul değiştiğinde
> hangi feature'ların anlamlı, tekrarlanabilir ve ayırt edici şekilde değiştiğini bulmak.

## İş akışı

```
DATASET → FRAME READING → PREPROCESSING (ROI ilk adım) → REGISTRATION → ALGORITHM
        → FEATURE EXTRACTION → STATISTICAL SUMMARY → RESULT TABLE → CSV / JSON
        → DATASET COMPARISON (run'lar yan yana, feature matrix CSV)
```

## Proje yapısı

```
TurbulenceAnalysis.pro  GUI uygulamasının TEK qmake projesi (tüm kaynaklar + OpenCV ayarı)
core/                 Analiz çekirdeği: saf C++11 + OpenCV (Qt bağımlılığı yok)
  core.pri              cli/ ve tests/ için OpenCV ayarı + core kaynakları
  Types.*               Roi, ParamSpec/ParamSet, Feature, AlgorithmResult
  FrameSource.*         Görüntü klasörü (natural sort) / video; sıralı okuma, cache yok
  Preprocessor.*        ROI crop → grayscale → float → denoise → contrast → normalization
  Registration.*        None / Translation (phaseCorrelate) / Affine (findTransformECC)
  ReferenceFrame.*      First / Previous / Fixed / Average referans yönetimi
  Algorithm.h           Algoritma arayüzü (çok geçişli, sıralı)
  AlgorithmRegistry.*   Tüm algoritmaların listesi (GUI/CLI buradan okur)
  AnalysisEngine.*      Pipeline, ilerleme callback'i, iptal
  Statistics.*          mean/variance/std/RMS/min/max/P95, per-pixel Welford
  ResultExporter.*      Frame CSV, summary CSV, JSON, karşılaştırma CSV, feature matrix CSV
  algorithms/           8 algoritma
app/                  Qt Widgets GUI (worker thread, dinamik tablolar, ROI seçimi)
cli/                  Komut satırı (batch / script)
tests/                Bilinen ground-truth'lu sentetik veri ile otomatik testler
```

## Derleme

Gereksinimler: Qt 5.4 veya üstü (Widgets; 5.15 ile test edildi), OpenCV 3.x (4.x ile de derlenir), C++11 derleyici.

### Qt Creator

**File → Open File or Project…** ile kökteki **`TurbulenceAnalysis.pro`** dosyasını açın,
bir Qt 5 kiti seçin ve **Run** (Ctrl+R) yapın. Başka proje dosyası gerekmez.

### Komut satırı

```bash
mkdir build && cd build
qmake ../TurbulenceAnalysis.pro
make -j
./TurbulenceAnalysis
```

OpenCV sırasıyla şöyle aranır: qmake argümanı → pkg-config (`opencv` / `opencv4`) →
standart dizinler (`/usr/local`, `/usr`, `/opt/opencv*`). Bulunan ayar qmake çıktısında
`Project MESSAGE: OpenCV: ...` satırında görünür. Bulunamazsa yolları elle verin
(Qt Creator: **Projects → Build → qmake → Additional arguments**):

```bash
qmake ../TurbulenceAnalysis.pro OPENCV_INCLUDE_DIR=/opt/opencv/include \
      OPENCV_LIB_DIR=/opt/opencv/lib
# Windows örneği: OPENCV_INCLUDE_DIR=C:/opencv/build/include
#                 OPENCV_LIB_DIR=C:/opencv/build/x64/vc15/lib OPENCV_LIB_SUFFIX=341
```

Kullanılan OpenCV modülleri: core, imgproc, imgcodecs, videoio, video.
Kod yalnızca OpenCV 3.x'te bulunan API'leri kullanır (`cv::` C++ API, `cv::COLOR_*`,
`cv::CAP_PROP_*` …); OpenCV 2.x C API'si ve OpenCV 4.x'e özgü API kullanılmaz.

### İsteğe bağlı: CLI ve testler

Bunlar ayrı, bağımsız projelerdir (GUI için gerekmez); aynı şekilde açılıp derlenir:
`cli/cli.pro` → `turbulence_cli`, `tests/tests.pro` → `turbulence_tests`.

## GUI kullanımı

1. **Dataset**: görüntü klasörü veya video açın. Frame rate (timestamp için), frame aralığı ve adım.
2. **ROI**: önizlemede sürükleyerek veya x/y/w/h ile. "show preprocessed ROI" ile ön işleme sonucunu görün.
3. **Preprocessing**: denoise (Gaussian/Median/Bilateral), contrast (HistEq/CLAHE), normalization.
4. **Registration**: None / Translation / Affine; referans First / Previous / Fixed.
5. **Algorithm**: tek algoritma seçilir (SINGLE ALGORITHM MODE). Parametre formu algoritmanın
   `ParamSpec` listesinden otomatik oluşturulur.
6. **RUN**: analiz ayrı bir `QThread` üzerinde çalışır; GUI'ye yalnızca progress, frame no,
   durum, sonuç ve hata sinyalleri gelir (≈20 Hz ile sınırlandırılmış). Cancel desteklenir.

Sekmeler:

* **Summary** – algoritmanın döndürdüğü feature listesi (isim/değer/birim/anahtar) ve feature vector.
  Export: frame-level CSV, summary CSV (`feature,value`), JSON.
* **Run info** – metadata (dataset, ROI, preprocessing, registration, parametreler, süre, OpenCV sürümü) ve uyarılar.
* **Dataset comparison** – her run bir sütun; başlığa çift tıklayarak koşul etiketi verin
  (ör. "öğle, 14:00, güneşli"). Comparison CSV ve feature matrix CSV (her run bir satır –
  ileride feature selection / classifier girdisi).

**Save dataset config**: ROI, preprocessing, registration, aralık ve algoritma parametreleri
veri setinin yanına kaydedilir (`<klasör>/turbulence_dataset.json` veya `<video>.turbulence.json`)
ve dataset açılınca otomatik yüklenir.

## CLI

```bash
turbulence_cli --list
turbulence_cli --params phase_correlation
turbulence_cli --dataset /data/run_0930 --algorithm phase_correlation \
    --roi 100,100,400,300 --fps 100 --param reference=1 --out results/run_0930
# → results/run_0930_phase_correlation_frames.csv, ..._summary.csv, results/run_0930.json
```

Birden fazla `--algorithm` verilirse aynı frame akışında bağımsız çalışırlar (multiple mode).

## Algoritmalar

Tüm istatistikler popülasyon istatistiğidir (N'e bölünür). P95 lineer interpolasyonlu yüzdeliktir.
NaN değerler istatistiklere katılmaz; CSV'de `NaN`, JSON'da `null` yazılır.

| # | Algoritma | Frame-level seri | Öne çıkan feature'lar |
|---|-----------|------------------|------------------------|
| 1 | Temporal Intensity Variance | I(t) = ROI ortalaması, I(t)−I(t−1) | mean/var/std, detrend edilmiş RMS fluctuation, P95, ortalama \|ΔI\|, var(ΔI), piksel bazlı zamansal varyans |
| 2 | Scintillation Index | I(t), (I−Ī)/Ī | SI = Var(I)/Mean(I)², RMS/P95 normalize fluctuation, piksel bazlı ortalama SI (sıfıra bölme korumalı) |
| 3 | Laplacian Variance | var(∇²I) | mean/var/std/RMS/min/max/P95 sharpness, ortalama \|ΔS\|, CV |
| 4 | Gradient Magnitude Variance | ortalama \|∇I\| (Sobel), uzaysal var(\|∇I\|) | G(t) istatistikleri, temporal gradient variation, ortalama uzaysal gradient varyansı |
| 5 | Phase Correlation | dx, dy, \|d\|, peak response | mean/std/RMS dx-dy, mean/std/RMS/max/P95 displacement, valid ratio |
| 6 | Centroid Jitter | Cx, Cy, alan, ΔX, ΔY, J | centroid mean/std, jitter mean/std/RMS/max/P95, frame-to-frame jitter, alan, tespit oranı |
| 7 | Optical Flow (pyramidal LK) | takip edilen/geçerli vektör, ortalama akış | flow mean/var/std/RMS/max/P95, mean/std Fx-Fy, valid vector ratio |
| 8 | Image Structure Analysis | MAD, MSD, \|I_t−I_(t−1)\|, uzaysal var, P95/max fark, korelasyon | MAD, MSD, RMS fark, uzaysal/zamansal varyans, P95/max fark, referans korelasyonu |

Referans seçenekleri (5 ve 8): First / Previous / Fixed / Average. Fixed ve Average için
ön bir geçiş yapılır (yalnızca referans görüntü veya toplam görüntü tutulur). Centroid Jitter'da
referans: ilk geçerli frame / ortalama centroid / sabit frame. Optical Flow: Previous (her frame'de
yeniden feature tespiti) veya First (ilk frame'den takip).

Her algoritma ayrıca bir **feature vector** (classifier'a ileride verilebilecek alt küme) tanımlar;
GUI'de gösterilir, JSON'a ve feature matrix CSV'ye yazılır.

## Performans / bellek

* Frame'ler sıralı okunur; RAM'de yalnızca o anki frame + gerekiyorsa tek referans tutulur.
* ROI ilk adımda uygulanır (yalnızca Mat header, kopya yok); sonraki tüm işlemler ROI üzerinde.
* Tamponlar frame'ler arasında yeniden kullanılır.
* Ölçüm (640×480 MJPG video, 6000 frame, ROI 400×300, tek çekirdek): yoğunluk/Laplacian/gradient
  ≈ 10 s, phase correlation ≈ 36 s, optical flow ≈ 36 s, structure ≈ 48 s; tepe RSS ≈ 80 MB ve
  frame sayısından bağımsız.

## Araştırma açısından önemli notlar

* **`cv::phaseCorrelate` girdilerini değiştirir**: görüntü boyutu DFT için zaten optimal ise
  pencere (Hanning) girdilere *yerinde* çarpılır (OpenCV 3.x ve 4.x). Saklanan referans frame
  bozulmasın diye `phaseCorrelateSafe()` kopyalar üzerinde çalışır.
* **Hanning penceresi sub-pixel kestirimi sıfıra doğru yanlılar** (pencere ROI'ye sabit). Sentetik
  testte maksimum hata pencere ile ≈0.34 px, pencere olmadan ≈0.19 px. Küçük jitter
  ölçümlerinde `hanning=0` denemesi önerilir.
* **İkili maske centroid'i piksele kilitlenir** (~0.5 px kuantizasyon). Varsayılan ağırlıklandırma
  "Intensity weighted" (w = max(I − eşik, 0)); sentetik testte hata < 0.06 px.
* **Registration ROI içinde tahmin edilir**: kamera hareketiyle birlikte türbülansın neden olduğu
  tüm-ROI tilt'ini de siler. Displacement/centroid analizlerinde dikkatli kullanın.
* **Per-frame normalization** (min/max, z-score) mutlak yoğunluk dalgalanmasını yok eder;
  yoğunluk/scintillation feature'ları bu durumda uyarı notu üretir.

## Testler

```bash
tests/turbulence_tests [çalışma_klasörü]
```

Bilinen sub-pixel kaydırma, kazanç ve blur ile sentetik 16-bit görüntü dizileri üretir ve
şunları doğrular: istatistik fonksiyonları, natural sort, phase correlation (tüm referans modları),
translation/affine registration, centroid (binary/Otsu/adaptive, ROI offset), optical flow,
yoğunluk/scintillation formülleri ve sıfıra bölme koruması, blur ile Laplacian/gradient düşüşü,
structure analysis, multiple-algorithm modu, frame aralığı/adımı, exporter'lar ve iptal.

## Genişletme

Yeni algoritma: `turb::Algorithm`'dan türetin (`parameterSpecs()`, `begin()`, `processFrame()`,
`finish()`), `AlgorithmRegistry` kurucusuna `add<YeniAlgoritma>(öncelik)` ekleyin. GUI parametre
formu, sonuç tablosu, CSV/JSON çıktıları ve karşılaştırma tablosu otomatik olarak çalışır.

Multiple algorithm mode motor tarafında hazırdır (`AnalysisConfig::algorithms` birden çok giriş
alır, her frame geçiş başına bir kez çözülür); GUI'de `MainWindow::kAllowMultipleAlgorithms`
ile açılır.
