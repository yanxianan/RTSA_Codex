#pragma once

#include "sources/SourceTypes.h"

#include <QString>

namespace rtsa {

struct SimulationScenarioSaveResult {
    bool success = false;
    QString errorMessage;
};

class SimulationScenarioWriter final {
public:
    SimulationScenarioWriter() = delete;

    static SimulationScenarioSaveResult saveFile(const QString& filePath,
                                                  const QString& scenarioName,
                                                  const SimulationConfig& config);
};

} // namespace rtsa
