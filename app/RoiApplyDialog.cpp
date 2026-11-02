#include "RoiApplyDialog.h"

#include "FrameSource.h"
#include "ImageUtils.h"
#include "Measurement.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {

const int kPathRole = Qt::UserRole + 1;
const int kFitRole = Qt::UserRole + 2;
const int kThumb = 220; // thumbnail size (longer side)

} // namespace

RoiApplyDialog::RoiApplyDialog(const QRect &roi, const QString &sourcePath, const QStringList &candidates,
                               QWidget *parent)
    : QDialog(parent)
    , m_roi(roi)
    , m_source(sourcePath)
{
    setWindowTitle(tr("Apply ROI to other datasets"));
    resize(1000, 700);
    QVBoxLayout *v = new QVBoxLayout(this);

    QLabel *head = new QLabel(
        tr("<b>ROI</b> x=%1, y=%2, %3 x %4 px &nbsp;from&nbsp; <b>%5</b><br>"
           "Check where the ROI falls on the first frame of every dataset, then apply it to the checked ones. "
           "Datasets where it lies completely outside the image are skipped.")
            .arg(roi.x())
            .arg(roi.y())
            .arg(roi.width())
            .arg(roi.height())
            .arg(QFileInfo(sourcePath).fileName().toHtmlEscaped()));
    head->setWordWrap(true);
    v->addWidget(head);

    QHBoxLayout *tools = new QHBoxLayout;
    QPushButton *bFolder = new QPushButton(tr("Add folder..."));
    QPushButton *bSub = new QPushButton(tr("Add all subfolders of..."));
    bSub->setToolTip(tr("Adds every subfolder of a parent folder as a dataset (one folder per recording)."));
    QPushButton *bVideo = new QPushButton(tr("Add videos..."));
    QPushButton *bAll = new QPushButton(tr("Check all"));
    QPushButton *bNone = new QPushButton(tr("Uncheck all"));
    connect(bFolder, &QPushButton::clicked, this, &RoiApplyDialog::addFolder);
    connect(bSub, &QPushButton::clicked, this, &RoiApplyDialog::addSubfolders);
    connect(bVideo, &QPushButton::clicked, this, &RoiApplyDialog::addVideos);
    connect(bAll, &QPushButton::clicked, this, [this]() { setAllChecked(true); });
    connect(bNone, &QPushButton::clicked, this, [this]() { setAllChecked(false); });
    tools->addWidget(bFolder);
    tools->addWidget(bSub);
    tools->addWidget(bVideo);
    tools->addStretch(1);
    tools->addWidget(bAll);
    tools->addWidget(bNone);
    v->addLayout(tools);

    // thumbnails with the ROI drawn on the first frame
    m_list = new QListWidget;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kThumb, kThumb));
    m_list->setGridSize(QSize(kThumb + 30, kThumb + 70));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setSpacing(6);
    connect(m_list, &QListWidget::itemChanged, this, &RoiApplyDialog::updateCount);
    v->addWidget(m_list, 1);

    QHBoxLayout *bottom = new QHBoxLayout;
    m_count = new QLabel;
    bottom->addWidget(m_count, 1);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_applyButton = box->addButton(tr("Apply ROI"), QDialogButtonBox::AcceptRole);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_applyButton, &QPushButton::clicked, this, &RoiApplyDialog::apply);
    bottom->addWidget(box);
    v->addLayout(bottom);

    for (const QString &c : candidates)
        addCandidate(c);
    updateCount();
}

void RoiApplyDialog::addCandidate(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || clean == QDir::cleanPath(m_source))
        return;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(kPathRole).toString() == clean)
            return;

    // first frame + where the ROI falls on it
    std::string err;
    std::unique_ptr<turb::FrameSource> src = turb::FrameSource::create(clean.toStdString(), &err);
    cv::Mat first;
    Fit fit = Unreadable;
    QPixmap pix(kThumb, kThumb * 3 / 4);
    pix.fill(QColor("#eaeef2"));
    QString sizeText;
    if (src && src->readAt(0, first)) {
        // every thumbnail has the same size: large frames are reduced, small ones enlarged
        QImage img = toQImage(first, kThumb);
        img = img.scaled(kThumb, kThumb, Qt::KeepAspectRatio,
                         img.width() < kThumb && img.height() < kThumb ? Qt::FastTransformation
                                                                       : Qt::SmoothTransformation);
        const double s = double(img.width()) / first.cols;
        pix = QPixmap::fromImage(img);
        const cv::Rect want(m_roi.x(), m_roi.y(), m_roi.width(), m_roi.height());
        turb::Roi r;
        r.x = m_roi.x();
        r.y = m_roi.y();
        r.width = m_roi.width();
        r.height = m_roi.height();
        const cv::Rect got = m_roi.isEmpty() ? cv::Rect(0, 0, first.cols, first.rows)
                                             : turb::effectiveRoi(r, first.size());
        fit = m_roi.isEmpty() || got == want ? Fits : (got.area() > 0 ? Clamped : Outside);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        if (!m_roi.isEmpty()) {
            // requested ROI (dashed) and the part that will be used (solid)
            p.setPen(QPen(QColor(255, 60, 60), 2, Qt::DashLine));
            p.drawRect(QRectF(want.x * s, want.y * s, want.width * s, want.height * s));
            if (got.area() > 0) {
                p.setPen(QPen(QColor(255, 60, 60), 2));
                p.fillRect(QRectF(got.x * s, got.y * s, got.width * s, got.height * s), QColor(255, 60, 60, 40));
                p.drawRect(QRectF(got.x * s, got.y * s, got.width * s, got.height * s));
            }
        }
        sizeText = QString("%1 x %2").arg(first.cols).arg(first.rows);
    } else {
        QPainter p(&pix);
        p.drawText(pix.rect(), Qt::AlignCenter, tr("cannot read"));
    }

    QString status;
    QColor color;
    switch (fit) {
    case Fits: status = QString::fromUtf8("\xe2\x9c\x93 ") + tr("ROI fits"); color = QColor("#1a7f37"); break;
    case Clamped: status = QString::fromUtf8("\xe2\x9a\xa0 ") + tr("partly outside, clamped"); color = QColor("#9a6700"); break;
    case Outside: status = QString::fromUtf8("\xe2\x9c\x95 ") + tr("outside the image"); color = QColor("#cf222e"); break;
    case Unreadable: status = QString::fromUtf8("\xe2\x9c\x95 ") + tr("cannot read"); color = QColor("#cf222e"); break;
    }
    QListWidgetItem *it = new QListWidgetItem(QIcon(pix), QFileInfo(clean).fileName() + "\n" + sizeText +
                                                              (sizeText.isEmpty() ? "" : "  ") + status);
    it->setData(kPathRole, clean);
    it->setData(kFitRole, int(fit));
    it->setToolTip(clean);
    it->setForeground(color);
    if (fit == Outside || fit == Unreadable) {
        it->setFlags(it->flags() & ~Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
        it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
    } else {
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
    }
    m_list->addItem(it);
}

void RoiApplyDialog::addFolder()
{
    QSettings s;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Image folder"),
                                                          s.value("lastDir", QDir::homePath()).toString());
    if (!dir.isEmpty())
        addCandidate(dir);
    updateCount();
}

void RoiApplyDialog::addSubfolders()
{
    QSettings s;
    const QString parent = QFileDialog::getExistingDirectory(this, tr("Parent folder of the datasets"),
                                                             s.value("lastDir", QDir::homePath()).toString());
    if (parent.isEmpty())
        return;
    const QFileInfoList dirs = QDir(parent).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &d : dirs)
        addCandidate(d.absoluteFilePath());
    updateCount();
}

void RoiApplyDialog::addVideos()
{
    QSettings s;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Videos"), s.value("lastDir", QDir::homePath()).toString(),
        tr("Video (*.avi *.mp4 *.mov *.mkv *.mpg *.mpeg *.wmv *.m4v);;All files (*)"));
    for (const QString &f : files)
        addCandidate(f);
    updateCount();
}

void RoiApplyDialog::setAllChecked(bool checked)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->flags() & Qt::ItemIsUserCheckable)
            m_list->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    updateCount();
}

void RoiApplyDialog::updateCount()
{
    int checked = 0, clamped = 0;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->checkState() == Qt::Checked) {
            ++checked;
            clamped += m_list->item(i)->data(kFitRole).toInt() == Clamped;
        }
    QString text = tr("%1 of %2 dataset(s) checked").arg(checked).arg(m_list->count());
    if (clamped > 0)
        text += tr(" - in %1 of them the ROI is partly outside and will be clamped").arg(clamped);
    if (m_list->count() == 0)
        text = tr("No other datasets yet: add folders or videos.");
    m_count->setText(text);
    m_applyButton->setEnabled(checked > 0);
}

void RoiApplyDialog::apply()
{
    QStringList paths;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->checkState() == Qt::Checked)
            paths << m_list->item(i)->data(kPathRole).toString();
    if (paths.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Apply ROI"),
                              tr("Use this ROI for %1 dataset(s)? Their previously stored ROI is replaced.")
                                  .arg(paths.size())) != QMessageBox::Yes)
        return;
    QSettings s;
    for (const QString &p : paths) {
        if (m_roi.isEmpty())
            s.remove("roi/" + p); // full frame
        else
            s.setValue("roi/" + p, m_roi);
    }
    m_applied = paths;
    accept();
}
