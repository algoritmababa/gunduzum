#ifndef DATASETCONFIG_H
#define DATASETCONFIG_H

#include "AnalysisEngine.h"

#include <QString>

// Per-dataset experiment settings (ROI, preprocessing, registration, range, last algorithm)
// stored as a JSON side-car next to the dataset so the ROI travels with the data:
//   <image folder>/turbulence_dataset.json   or   <video file>.turbulence.json
namespace DatasetConfig {

QString sidecarPath(const QString &datasetPath);

bool save(const QString &file, const turb::AnalysisConfig &config, QString *error);
// The dataset path inside the file is ignored when `keepDatasetPath` is set
// (lets a config be applied to another dataset).
bool load(const QString &file, turb::AnalysisConfig &config, bool keepDatasetPath, QString *error);

} // namespace DatasetConfig

#endif
