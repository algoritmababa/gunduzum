#ifndef TURB_RESULTTABLE_H
#define TURB_RESULTTABLE_H

#include "Measurement.h"

#include <string>
#include <vector>

namespace turb {

// The shared results table: one CSV row per measured window.
//   recording, category, note, source, window, start_frame, end_frame, frames,
//   roi_x, roi_y, roi_w, roi_h, measured_at, <features...>
// Rows from every run are appended to the same file; analysis can be done here
// (category summary) or in any external tool.
struct Table {
    std::vector<std::string> header;
    std::vector<std::vector<std::string> > rows;

    int column(const std::string &name) const; // -1 if missing
};

std::vector<std::string> resultColumns();

// Appends one row per window. Creates the file with a header if it does not exist;
// refuses to append to a file whose header differs.
bool appendResults(const std::string &csvPath, const Recording &rec, const MeasureOutput &out, std::string *error);

bool loadTable(const std::string &csvPath, Table &table, std::string *error);

// Per feature: mean / std / count of every category, plus whether the means follow the
// category order and whether neighbouring categories are clearly apart.
struct CategoryStats {
    std::string feature;
    std::vector<double> mean, std;
    std::vector<int> count;
    std::string trend;      // "increasing", "decreasing", "mixed", "" (fewer than 2 categories)
    std::string separation; // "clear" (|delta mean| > std_a + std_b for all neighbours), "overlapping", ""
};

// Categories are taken in the given order; unlabeled rows are ignored.
std::vector<CategoryStats> categorySummary(const Table &table, const std::vector<std::string> &categoryOrder);

// Categories in the table: the preferred order first, then the others alphabetically.
std::vector<std::string> categoriesIn(const Table &table, const std::vector<std::string> &preferredOrder);

std::string csvEscape(const std::string &s);
std::vector<std::string> parseCsvLine(const std::string &line);

} // namespace turb

#endif
