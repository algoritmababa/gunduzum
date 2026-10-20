#ifndef ROIPREVIEWWIDGET_H
#define ROIPREVIEWWIDGET_H

#include <QImage>
#include <QRect>
#include <QWidget>

// Shows one dataset frame (scaled to fit) with the ROI rectangle on top.
// Dragging with the left mouse button defines a new ROI in image coordinates.
class RoiPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RoiPreviewWidget(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setRoi(const QRect &roi); // empty = full frame
    QSize sizeHint() const override { return QSize(480, 360); }

signals:
    void roiSelected(const QRect &roi);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QRectF imageRect() const;
    QPoint toImage(const QPoint &widgetPos) const;

    QImage m_image;
    QRect m_roi;
    bool m_dragging = false;
    QPoint m_dragStart, m_dragEnd;
};

#endif
