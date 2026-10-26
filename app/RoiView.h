#ifndef ROIVIEW_H
#define ROIVIEW_H

#include <QImage>
#include <QRect>
#include <QWidget>

// Shows a frame scaled to fit with the ROI on top; dragging with the left mouse button
// selects a new ROI (image coordinates).
class RoiView : public QWidget
{
    Q_OBJECT

public:
    explicit RoiView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setRoi(const QRect &roi); // empty = full frame
    QSize sizeHint() const override { return QSize(640, 480); }

signals:
    void roiSelected(const QRect &roi);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QRectF imageRect() const;
    QPoint toImage(const QPoint &p) const;

    QImage m_image;
    QRect m_roi;
    bool m_dragging = false;
    QPoint m_from, m_to;
};

#endif
