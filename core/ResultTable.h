#ifndef TURB_RESULTTABLE_H
#define TURB_RESULTTABLE_H

#include "Measurement.h"

#include <string>
#include <vector>

namespace turb {

// The shared results table: one CSV row per measured window.
//   recording, category, note, source, window, start_frame, end_frame, frames,
//   roi_x, roi_y, roi_w, roi_h, measured_at, <features...>
// Rows from every run are appended to the same file; the trend analysis (TrendAnalysis.h)
// or any external tool reads it.
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

std::string csvEscape(const std::string &s);
std::vector<std::string> parseCsvLine(const std::string &line);

} // namespace turb

#endif
