#ifndef TURB_RESULTTABLE_H
#define TURB_RESULTTABLE_H

#include "Measurement.h"

#include <string>
#include <vector>

namespace turb {

// The shared results table: one CSV row per measured window.
//   recording, category, note, source, window, start_frame, end_frame, frames,
//   roi_x, roi_y, roi_w, roi_h, preprocess, measured_at, <features...>
// Rows from every run are appended to the same file; the trend analysis (TrendAnalysis.h)
// or any external tool reads it.
struct Table {
    std::vector<std::string> header;
    std::vector<std::vector<std::string> > rows;

    int column(const std::string &name) const; // -1 if missing
};

std::vector<std::string> resultColumns();

// Appends one row per window. Creates the file with a header if it does not exist.
// A file of the previous version (no 'preprocess' column) is upgraded in place: the column is
// inserted with "none" for its rows. Refuses to append to a file whose header differs otherwise.
bool appendResults(const std::string &csvPath, const Recording &rec, const MeasureOutput &out, std::string *error);

bool loadTable(const std::string &csvPath, Table &table, std::string *error);

// Labels: every recording (source path) carries one category in all of its rows.
// A column of the last row of a recording, e.g. its category or note
// ("" and *found = false when the recording has no rows).
std::string lastValue(const Table &table, const std::string &source, const std::string &column,
                      bool *found = nullptr);
// Rows of a recording whose category differs from the given one.
int countRowsToRelabel(const Table &table, const std::string &source, const std::string &category);
// Sets the category of every row of a recording in the results file; returns the number of
// changed rows, -1 on error.
int relabelSource(const std::string &csvPath, const std::string &source, const std::string &category,
                  std::string *error);

// Preprocessing variants in a table, in order of appearance ("none" for a table without the column).
std::vector<std::string> preprocessVariants(const Table &table);
// The rows measured with one preprocessing variant (a table without the column counts as "none").
Table filterPreprocess(const Table &table, const std::string &variant);

std::string csvEscape(const std::string &s);
// Numbers in the table always use '.' as the decimal separator, whatever the system language
// (strtod / atof follow the locale: with a Turkish or German locale "0.0123" would read as 0).
// Returns NaN for "NaN", empty or unparsable fields.
double parseNumber(const std::string &s);
std::string formatNumber(double v);
std::vector<std::string> parseCsvLine(const std::string &line);

} // namespace turb

#endif
