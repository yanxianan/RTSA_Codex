#include "services/SpectrumExporter.h"

#include "core/AmplitudeUnits.h"
#include "core/FrequencyMapper.h"

#include <QByteArray>
#include <QSaveFile>

#include <cmath>

namespace rtsa {
namespace {

bool writeAll(QSaveFile& file, const QByteArray& data)
{
    return file.write(data) == data.size();
}

ExportResult failure(const QString& filePath, const QString& message)
{
    return ExportResult { false, filePath, message, 0 };
}

} // namespace

ExportResult SpectrumExporter::writeCsv(const ConstSpectrumFramePtr& frame,
                                        const QString& filePath)
{
    if (!frame || !frame->isConsistent() || !frame->hasFiniteBins()
        || !std::isfinite(frame->metadata.centerFrequencyHz)
        || !std::isfinite(frame->metadata.spanHz)
        || frame->metadata.spanHz <= 0.0) {
        return failure(filePath, QStringLiteral("No valid spectrum frame is available."));
    }
    if (filePath.trimmed().isEmpty()) {
        return failure(filePath, QStringLiteral("The export path is empty."));
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return failure(filePath, file.errorString());
    }

    const auto& metadata = frame->metadata;
    QByteArray header;
    header.reserve(320);
    header += "# RTSA spectrum snapshot\n";
    header += "# sequence," + QByteArray::number(metadata.sequence) + "\n";
    header += "# timestamp_ns," + QByteArray::number(metadata.timestampNs) + "\n";
    header += "# center_frequency_hz," + QByteArray::number(metadata.centerFrequencyHz, 'f', 6) + "\n";
    header += "# span_hz," + QByteArray::number(metadata.spanHz, 'f', 6) + "\n";
    header += "# bin_count," + QByteArray::number(metadata.binCount) + "\n";
    header += "# amplitude_unit," + QByteArray(amplitudeUnitSymbol(metadata.unit)) + "\n";
    header += "# calibrated," + QByteArray(metadata.calibrated ? "true" : "false") + "\n";
    header += "bin,frequency_hz," + QByteArray(amplitudeCsvColumnName(metadata.unit)) + "\n";
    if (!writeAll(file, header)) {
        return failure(filePath, file.errorString());
    }

    QByteArray line;
    line.reserve(96);
    for (std::size_t index = 0; index < frame->bins.size(); ++index) {
        line.clear();
        line += QByteArray::number(static_cast<qulonglong>(index));
        line += ',';
        line += QByteArray::number(
            FrequencyMapper::frequencyForBin(metadata, index), 'f', 6);
        line += ',';
        line += QByteArray::number(static_cast<double>(frame->bins[index]), 'f', 6);
        line += '\n';
        if (!writeAll(file, line)) {
            return failure(filePath, file.errorString());
        }
    }

    if (!file.commit()) {
        return failure(filePath, file.errorString());
    }
    return ExportResult { true, filePath, {}, frame->bins.size() };
}

} // namespace rtsa
