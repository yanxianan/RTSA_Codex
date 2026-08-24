#pragma once

#include "sources/SourceTypes.h"

namespace rtsa {

class ISimulationConfigurable {
public:
    virtual ~ISimulationConfigurable() = default;

    virtual void configure(const SimulationConfig& config) = 0;
    virtual SimulationConfig configuration() const = 0;
};

} // namespace rtsa
