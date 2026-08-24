#pragma once

#include "core/SpectrumFrame.h"

#include <QString>

namespace rtsa {

struct ExportResult {
    bool success = false;
    QString filePath;
    QString errorMessage;
    std::size_t exportedBins = 0;
};

class SpectrumExporter final {
public:
    SpectrumExporter() = delete;

    // Thread-safe: the immutable frame snapshot may be written by a worker thread.
    static ExportResult writeCsv(const ConstSpectrumFramePtr& frame, const QString& filePath);
};

} // namespace rtsa
