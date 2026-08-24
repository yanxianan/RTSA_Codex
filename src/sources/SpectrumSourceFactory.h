#pragma once

#include "sources/ISpectrumSource.h"

#include <QString>

#include <cstdint>
#include <memory>

namespace rtsa {

enum class SpectrumSourceKind : std::uint8_t {
    Simulated,
    Dma
};

struct SourceCreationResult {
    std::unique_ptr<ISpectrumSource> source;
    QString errorMessage;
};

bool parseSpectrumSourceKind(const QString& token,
                             SpectrumSourceKind& kind,
                             QString& errorMessage);
QString spectrumSourceKindToken(SpectrumSourceKind kind);
SourceCreationResult createSpectrumSource(SpectrumSourceKind kind);

} // namespace rtsa
