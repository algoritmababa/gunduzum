#include "Datasets.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSettings>

#include <algorithm>

namespace datasets {

namespace {

const QStringList &imagePatterns()
{
    static const QStringList p = {"*.png", "*.tif", "*.tiff", "*.bmp", "*.jpg", "*.jpeg", "*.pgm",
                                  "*.ppm", "*.pbm", "*.jp2", "*.exr"};
    return p;
}

const QStringList &videoPatterns()
{
    static const QStringList p = {"*.avi", "*.mp4", "*.mov", "*.mkv", "*.mpg", "*.mpeg", "*.wmv", "*.m4v"};
    return p;
}

bool hasImages(const QString &dir)
{
    QDir d(dir);
    d.setNameFilters(imagePatterns());
    d.setFilter(QDir::Files);
    return !d.entryList().isEmpty();
}

// image subfolders and video files directly inside dir
void collect(const QString &dir, QStringList &out, int depth)
{
    QDir d(dir);
    d.setNameFilters(videoPatterns());
    for (const QFileInfo &f : d.entryInfoList(QDir::Files))
        out << QDir::cleanPath(f.absoluteFilePath());
    for (const QFileInfo &sub : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString p = QDir::cleanPath(sub.absoluteFilePath());
        if (hasImages(p))
            out << p;
        else if (depth > 1)
            collect(p, out, depth - 1);
    }
}

QString clean(const std::string &p)
{
    return QDir::cleanPath(QString::fromStdString(p));
}

QString nameOf(const QString &path)
{
    return QFileInfo(QDir::cleanPath(path)).fileName();
}

QString field(const std::vector<std::string> &row, int c)
{
    return c >= 0 && size_t(c) < row.size() ? QString::fromStdString(row[size_t(c)]) : QString();
}

} // namespace

bool isVideoFile(const QString &path)
{
    const QString suffix = "*." + QFileInfo(path).suffix().toLower();
    return videoPatterns().contains(suffix);
}

QStringList find(const QString &folder)
{
    QStringList out;
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return out;
    if (hasImages(folder))
        out << QDir::cleanPath(folder);
    collect(folder, out, 2);
    QCollator natural;
    natural.setNumericMode(true);
    natural.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(out.begin(), out.end(), [&](const QString &a, const QString &b) { return natural.compare(a, b) < 0; });
    out.removeDuplicates();
    return out;
}

Label lookup(const QString &rawPath, const turb::Table &results)
{
    const QString path = QDir::cleanPath(rawPath);
    QSettings s;
    Label l;
    l.roi = s.value("roi/" + path).toRect();

    // 1. stored for this path
    if (s.contains("category/" + path)) {
        l.category = s.value("category/" + path).toString();
        l.note = s.value("note/" + path).toString();
        l.origin = Origin::Saved;
        return l;
    }

    // 2. rows of this path in the results file
    const int cSrc = results.column("source"), cCat = results.column("category"), cNote = results.column("note"),
              cRec = results.column("recording");
    for (const std::vector<std::string> &r : results.rows)
        if (cSrc >= 0 && size_t(cSrc) < r.size() && clean(r[size_t(cSrc)]) == path) {
            l.category = field(r, cCat);
            l.note = field(r, cNote);
            l.origin = Origin::Results;
        }

    // 3. a dataset with the same name somewhere else (moved / copied into the dataset folder)
    if (l.origin == Origin::None) {
        const QString name = nameOf(path);
        QSet<QString> categories;
        QString fromPath, fromNote;
        s.beginGroup("category");
        const QStringList keys = s.allKeys();
        s.endGroup();
        for (const QString &k : keys)
            if (nameOf(k) == name) {
                const QString c = s.value("category/" + k).toString();
                if (c.isEmpty())
                    continue;
                categories.insert(c);
                fromPath = k;
                fromNote = s.value("note/" + k).toString();
            }
        for (const std::vector<std::string> &r : results.rows) {
            const QString c = field(r, cCat);
            if (!c.isEmpty() && (field(r, cRec) == name || nameOf(field(r, cSrc)) == name)) {
                if (!categories.contains(c) && fromPath.isEmpty()) {
                    fromPath = field(r, cSrc);
                    fromNote = field(r, cNote);
                }
                categories.insert(c);
            }
        }
        if (categories.size() == 1) {
            l.category = *categories.begin();
            l.note = fromNote;
            l.origin = Origin::SameName;
            l.from = fromPath;
            if (l.roi.isEmpty())
                l.roi = s.value("roi/" + fromPath).toRect();
        } else if (categories.size() > 1) {
            l.origin = Origin::Conflict;
            return l; // nothing is stored: the user decides
        }
    }

    if (l.origin != Origin::None) {
        // remember it for this path (old entries stay as they are)
        s.setValue("category/" + path, l.category);
        s.setValue("note/" + path, l.note);
        if (!l.roi.isEmpty() && !s.contains("roi/" + path))
            s.setValue("roi/" + path, l.roi);
    }
    return l;
}

int measuredRows(const QString &rawPath, const turb::Table &results, const QString &preprocess)
{
    const QString path = QDir::cleanPath(rawPath);
    const int cSrc = results.column("source"), cRec = results.column("recording"),
              cPre = results.column("preprocess");
    auto variantOk = [&](const std::vector<std::string> &r) {
        const QString v = field(r, cPre);
        return (v.isEmpty() ? QString("none") : v) == preprocess;
    };
    int byPath = 0, byName = 0;
    const QString name = nameOf(path);
    for (const std::vector<std::string> &r : results.rows) {
        if (!variantOk(r))
            continue;
        if (clean(field(r, cSrc).toStdString()) == path)
            ++byPath;
        else if (field(r, cRec) == name)
            ++byName; // the same dataset at its old place (moved or copied)
    }
    return byPath > 0 ? byPath : byName;
}

QStringList replacedSources(const QString &rawPath, const QString &category, const turb::Table &results)
{
    const QString path = QDir::cleanPath(rawPath);
    QStringList out;
    out << path;
    const int cSrc = results.column("source"), cRec = results.column("recording"), cCat = results.column("category");
    const QString name = nameOf(path);
    for (const std::vector<std::string> &r : results.rows) {
        const QString src = field(r, cSrc);
        if (field(r, cRec) == name && field(r, cCat) == category && !out.contains(src))
            out << src;
    }
    return out;
}

} // namespace datasets
