#ifndef DATASETS_H
#define DATASETS_H

#include "ResultTable.h"

#include <QRect>
#include <QString>
#include <QStringList>

// Datasets of a folder and the label / note / ROI remembered for every dataset.
//
// Labels are stored per dataset path (QSettings "category/<path>", "note/<path>", "roi/<path>").
// When a dataset was moved (e.g. all datasets copied into one folder), its old entries are found
// by its name, so labels already given are kept; they are never deleted.
namespace datasets {

// Datasets inside a folder: subfolders with images and video files, also one level deeper
// (folder/LOW/rec1). The folder itself counts as a dataset when it contains images.
// Sorted naturally by path.
QStringList find(const QString &folder);

bool isVideoFile(const QString &path);

enum class Origin {
    Saved,       // stored for this path
    Results,     // taken from the rows of this path in the results file
    SameName,    // taken from a dataset with the same name at another place (moved)
    Conflict,    // datasets with this name carry different labels: not taken over
    None         // never labeled
};

struct Label {
    QString category;   // empty = unlabeled
    QString note;
    QRect roi;          // empty = full frame / not set
    Origin origin = Origin::None;
    QString from;       // SameName: the old path the label came from
    bool labeled() const { return !category.isEmpty(); }
};

// Looks the label up (see Origin, in that order). Labels found at another place are stored
// for this path, so the lookup is done once.
Label lookup(const QString &path, const turb::Table &results);

// How often the dataset was measured with the given preprocessing (rows of the results file).
// When no row has its current path, rows of the same dataset name at another place count
// (the dataset was moved or copied into the dataset folder).
int measuredRows(const QString &path, const turb::Table &results, const QString &preprocess);

// Results rows a new measurement of this dataset replaces: its own path and the paths of the
// same dataset at its old place (same name and same category: it was moved or copied).
QStringList replacedSources(const QString &path, const QString &category, const turb::Table &results);

} // namespace datasets

#endif
