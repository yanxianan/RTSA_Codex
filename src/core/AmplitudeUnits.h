#pragma once

#include "core/SpectrumFrame.h"

namespace rtsa {

constexpr const char* amplitudeUnitSymbol(const AmplitudeUnit unit) noexcept
{
    switch (unit) {
    case AmplitudeUnit::Dbm:
        return "dBm";
    case AmplitudeUnit::Dbc:
        return "dBc";
    case AmplitudeUnit::LinearPower:
        return "Power";
    case AmplitudeUnit::Dbfs:
    default:
        return "dBFS";
    }
}

constexpr const char* amplitudeCsvColumnName(const AmplitudeUnit unit) noexcept
{
    switch (unit) {
    case AmplitudeUnit::Dbm:
        return "amplitude_dbm";
    case AmplitudeUnit::Dbc:
        return "amplitude_dbc";
    case AmplitudeUnit::LinearPower:
        return "power_linear";
    case AmplitudeUnit::Dbfs:
    default:
        return "amplitude_dbfs";
    }
}

} // namespace rtsa
