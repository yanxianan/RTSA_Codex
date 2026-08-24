#pragma once

#include "core/SpectrumFrame.h"
#include "sources/SourceTypes.h"

#include <QObject>
#include <QString>

#include <functional>

namespace rtsa {

// Stable boundary between acquisition backends and the processing/display path.
// A future DmaSpectrumSource implements this contract; no plot or trace code needs
// to know whether frames came from a simulator or AXI DMA.
class ISpectrumSource : public QObject {
    Q_OBJECT

public:
    // False means the downstream pipeline classified the rejection; source
    // transport counters must not count the same frame a second time.
    using FrameSink = std::function<bool(const SpectrumFramePtr&)>;

    explicit ISpectrumSource(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~ISpectrumSource() override = default;

    ISpectrumSource(const ISpectrumSource&) = delete;
    ISpectrumSource& operator=(const ISpectrumSource&) = delete;

    virtual void setFrameSink(FrameSink sink) = 0;
    virtual bool start(AcquisitionMode mode = AcquisitionMode::Continuous) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual SourceState state() const noexcept = 0;
    virtual SourceStatistics statistics() const = 0;

signals:
    void stateChanged(int state);
    void errorOccurred(const QString& message);
};

} // namespace rtsa
