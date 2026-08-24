#include "sources/SpectrumSourceFactory.h"

#include "sources/SimulatedSpectrumSource.h"

namespace rtsa {

bool parseSpectrumSourceKind(const QString& token,
                             SpectrumSourceKind& kind,
                             QString& errorMessage)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("simulated")) {
        kind = SpectrumSourceKind::Simulated;
        errorMessage.clear();
        return true;
    }
    if (normalized == QStringLiteral("dma")) {
        kind = SpectrumSourceKind::Dma;
        errorMessage.clear();
        return true;
    }

    errorMessage = QStringLiteral(
        "未知数据源“%1”；可选值为 simulated 或 dma。").arg(token);
    return false;
}

QString spectrumSourceKindToken(const SpectrumSourceKind kind)
{
    switch (kind) {
    case SpectrumSourceKind::Dma:
        return QStringLiteral("dma");
    case SpectrumSourceKind::Simulated:
    default:
        return QStringLiteral("simulated");
    }
}

SourceCreationResult createSpectrumSource(const SpectrumSourceKind kind)
{
    switch (kind) {
    case SpectrumSourceKind::Simulated:
        return SourceCreationResult { std::make_unique<SimulatedSpectrumSource>(), {} };
    case SpectrumSourceKind::Dma:
        return SourceCreationResult {
            {},
            QStringLiteral("DMA 数据源尚不可用：请先提供 PL 帧协议和 Linux DMA 用户态 API。")
        };
    default:
        return SourceCreationResult { {}, QStringLiteral("不受支持的数据源类型。") };
    }
}

} // namespace rtsa
