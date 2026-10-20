#include "RoiPreviewWidget.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

RoiPreviewWidget::RoiPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(200, 150);
    setCursor(Qt::CrossCursor);
}

void RoiPreviewWidget::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void RoiPreviewWidget::setRoi(const QRect &roi)
{
    m_roi = roi;
    update();
}

QRectF RoiPreviewWidget::imageRect() const
{
    if (m_image.isNull())
        return QRectF();
    const double s = std::min(double(width()) / m_image.width(), double(height()) / m_image.height());
    const double w = m_image.width() * s, h = m_image.height() * s;
    return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
}

QPoint RoiPreviewWidget::toImage(const QPoint &p) const
{
    const QRectF r = imageRect();
    if (r.isEmpty())
        return QPoint();
    const double s = r.width() / m_image.width();
    const int x = int((p.x() - r.left()) / s);
    const int y = int((p.y() - r.top()) / s);
    return QPoint(std::min(std::max(x, 0), m_image.width()), std::min(std::max(y, 0), m_image.height()));
}

void RoiPreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    if (m_image.isNull()) {
        p.drawText(rect(), Qt::AlignCenter, tr("No dataset loaded"));
        return;
    }
    const QRectF r = imageRect();
    p.drawImage(r, m_image);
    const double s = r.width() / m_image.width();

    QRect roi = m_roi;
    if (m_dragging)
        roi = QRect(m_dragStart, m_dragEnd).normalized();
    if (roi.isEmpty())
        roi = QRect(0, 0, m_image.width(), m_image.height());
    const QRectF wr(r.left() + roi.x() * s, r.top() + roi.y() * s, roi.width() * s, roi.height() * s);
    p.setPen(QPen(QColor(255, 60, 60), 2, m_dragging ? Qt::DashLine : Qt::SolidLine));
    p.drawRect(wr);
}

void RoiPreviewWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_image.isNull())
        return;
    m_dragging = true;
    m_dragStart = m_dragEnd = toImage(e->pos());
    update();
}

void RoiPreviewWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    m_dragEnd = toImage(e->pos());
    update();
}

void RoiPreviewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_dragging || e->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    m_dragEnd = toImage(e->pos());
    QRect r(m_dragStart, m_dragEnd);
    r = r.normalized();
    // QRect(p1, p2) is inclusive; use exclusive width/height
    r = QRect(r.x(), r.y(), r.width() - 1, r.height() - 1);
    if (r.width() >= 4 && r.height() >= 4) {
        m_roi = r;
        emit roiSelected(r);
    }
    update();
}
