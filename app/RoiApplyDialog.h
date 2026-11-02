#ifndef ROIAPPLYDIALOG_H
#define ROIAPPLYDIALOG_H

#include <QDialog>
#include <QRect>
#include <QStringList>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// Applies the ROI chosen on one recording to other recordings (optional).
// Before anything is stored, every candidate shows its first frame with the ROI drawn on it
// and whether the ROI fits, is clamped or lies outside that image; only checked recordings
// whose image the ROI touches are updated.
class RoiApplyDialog : public QDialog
{
    Q_OBJECT

public:
    RoiApplyDialog(const QRect &roi, const QString &sourcePath, const QStringList &candidates,
                   QWidget *parent = nullptr);

    // Recordings the ROI was applied to (after accept()).
    QStringList appliedTo() const { return m_applied; }

private slots:
    void addFolder();
    void addSubfolders();
    void addVideos();
    void setAllChecked(bool checked);
    void apply();
    void updateCount();

private:
    enum Fit { Fits, Clamped, Outside, Unreadable };
    void addCandidate(const QString &path);

    QRect m_roi;
    QString m_source;
    QListWidget *m_list;
    QLabel *m_count;
    QPushButton *m_applyButton;
    QStringList m_applied;
};

#endif
