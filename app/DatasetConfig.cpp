#include "DatasetConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace DatasetConfig {

QString sidecarPath(const QString &datasetPath)
{
    QFileInfo fi(datasetPath);
    if (fi.isDir())
        return fi.absoluteFilePath() + "/turbulence_dataset.json";
    return fi.absoluteFilePath() + ".turbulence.json";
}

bool save(const QString &file, const turb::AnalysisConfig &c, QString *error)
{
    QJsonObject root;
    root["format"] = "turbulence-dataset-config";
    root["version"] = 1;
    root["dataset"] = QString::fromStdString(c.dataset.path);
    root["frame_rate"] = c.dataset.frameRate;
    root["range"] = QJsonObject{{"start", c.dataset.startFrame},
                                {"end", c.dataset.endFrame},
                                {"step", c.dataset.frameStep}};
    root["roi"] = QJsonObject{{"x", c.preprocess.roi.x},
                              {"y", c.preprocess.roi.y},
                              {"width", c.preprocess.roi.width},
                              {"height", c.preprocess.roi.height}};
    root["preprocessing"] = QJsonObject{{"denoise", int(c.preprocess.denoise)},
                                        {"denoise_kernel", c.preprocess.denoiseKernel},
                                        {"denoise_sigma", c.preprocess.denoiseSigma},
                                        {"contrast", int(c.preprocess.contrast)},
                                        {"clahe_clip", c.preprocess.claheClipLimit},
                                        {"clahe_tiles", c.preprocess.claheTileGrid},
                                        {"normalization", int(c.preprocess.normalization)},
                                        {"description", QString::fromStdString(c.preprocess.describe())}};
    root["registration"] = QJsonObject{{"mode", int(c.registration.mode)},
                                       {"reference", int(c.registration.reference)},
                                       {"fixed_index", c.registration.fixedFrameIndex},
                                       {"description", QString::fromStdString(c.registration.describe())}};
    QJsonArray algs;
    for (const turb::AlgorithmRun &run : c.algorithms) {
        QJsonObject params;
        for (const auto &kv : run.params.values())
            params[QString::fromStdString(kv.first)] = kv.second;
        algs.append(QJsonObject{{"id", QString::fromStdString(run.id)}, {"parameters", params}});
    }
    root["algorithms"] = algs;

    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool load(const QString &file, turb::AnalysisConfig &c, bool keepDatasetPath, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = pe.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    if (!keepDatasetPath && root.contains("dataset"))
        c.dataset.path = root["dataset"].toString().toStdString();
    c.dataset.frameRate = root["frame_rate"].toDouble(c.dataset.frameRate);

    const QJsonObject range = root["range"].toObject();
    c.dataset.startFrame = range["start"].toInt(0);
    c.dataset.endFrame = range["end"].toInt(-1);
    c.dataset.frameStep = range["step"].toInt(1);

    const QJsonObject roi = root["roi"].toObject();
    c.preprocess.roi.x = roi["x"].toInt(0);
    c.preprocess.roi.y = roi["y"].toInt(0);
    c.preprocess.roi.width = roi["width"].toInt(0);
    c.preprocess.roi.height = roi["height"].toInt(0);

    const QJsonObject pre = root["preprocessing"].toObject();
    c.preprocess.denoise = turb::PreprocessConfig::Denoise(pre["denoise"].toInt(0));
    c.preprocess.denoiseKernel = pre["denoise_kernel"].toInt(3);
    c.preprocess.denoiseSigma = pre["denoise_sigma"].toDouble(1.0);
    c.preprocess.contrast = turb::PreprocessConfig::Contrast(pre["contrast"].toInt(0));
    c.preprocess.claheClipLimit = pre["clahe_clip"].toDouble(2.0);
    c.preprocess.claheTileGrid = pre["clahe_tiles"].toInt(8);
    c.preprocess.normalization = turb::PreprocessConfig::Normalization(pre["normalization"].toInt(0));

    const QJsonObject reg = root["registration"].toObject();
    c.registration.mode = turb::RegistrationConfig::Mode(reg["mode"].toInt(0));
    c.registration.reference = turb::RegistrationConfig::Reference(reg["reference"].toInt(0));
    c.registration.fixedFrameIndex = reg["fixed_index"].toInt(0);

    c.algorithms.clear();
    for (const QJsonValue &v : root["algorithms"].toArray()) {
        const QJsonObject o = v.toObject();
        turb::AlgorithmRun run;
        run.id = o["id"].toString().toStdString();
        const QJsonObject params = o["parameters"].toObject();
        for (auto it = params.begin(); it != params.end(); ++it)
            run.params.set(it.key().toStdString(), it.value().toDouble());
        c.algorithms.push_back(run);
    }
    return true;
}

} // namespace DatasetConfig
