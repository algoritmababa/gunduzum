#include "RoiView.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

RoiView::RoiView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(240, 180);
    setCursor(Qt::CrossCursor);
}

void RoiView::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void RoiView::setRoi(const QRect &roi)
{
    m_roi = roi;
    update();
}

QRectF RoiView::imageRect() const
{
    if (m_image.isNull())
        return QRectF();
    const double s = std::min(double(width()) / m_image.width(), double(height()) / m_image.height());
    const double w = m_image.width() * s, h = m_image.height() * s;
    return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
}

QPoint RoiView::toImage(const QPoint &p) const
{
    const QRectF r = imageRect();
    if (r.isEmpty())
        return QPoint();
    const double s = r.width() / m_image.width();
    const int x = int((p.x() - r.left()) / s);
    const int y = int((p.y() - r.top()) / s);
    return QPoint(std::min(std::max(x, 0), m_image.width()), std::min(std::max(y, 0), m_image.height()));
}

void RoiView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    if (m_image.isNull()) {
        p.drawText(rect(), Qt::AlignCenter, tr("Open a recording"));
        return;
    }
    const QRectF r = imageRect();
    p.drawImage(r, m_image);
    QRect roi = m_dragging ? QRect(m_from, m_to).normalized() : m_roi;
    if (roi.isEmpty())
        roi = QRect(0, 0, m_image.width(), m_image.height());
    const double s = r.width() / m_image.width();
    p.setPen(QPen(QColor(255, 60, 60), 2, m_dragging ? Qt::DashLine : Qt::SolidLine));
    p.drawRect(QRectF(r.left() + roi.x() * s, r.top() + roi.y() * s, roi.width() * s, roi.height() * s));
}

void RoiView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_image.isNull())
        return;
    m_dragging = true;
    m_from = m_to = toImage(e->pos());
    update();
}

void RoiView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    m_to = toImage(e->pos());
    update();
}

void RoiView::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_dragging || e->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    m_to = toImage(e->pos());
    const QRect n = QRect(m_from, m_to).normalized();
    const QRect r(n.x(), n.y(), n.width() - 1, n.height() - 1); // QRect(p1, p2) is inclusive
    if (r.width() >= 8 && r.height() >= 8) {
        m_roi = r;
        emit roiSelected(r);
    }
    update();
}
