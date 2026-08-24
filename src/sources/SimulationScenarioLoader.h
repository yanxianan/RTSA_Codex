#pragma once

#include "sources/SourceTypes.h"

#include <QByteArray>
#include <QString>

namespace rtsa {

struct SimulationScenarioLoadResult {
    bool success = false;
    QString scenarioName;
    SimulationConfig configuration;
    QString errorMessage;
};

class SimulationScenarioLoader final {
public:
    SimulationScenarioLoader() = delete;

    static SimulationScenarioLoadResult loadFile(const QString& filePath);
    static SimulationScenarioLoadResult loadData(const QByteArray& jsonData);
};

} // namespace rtsa
