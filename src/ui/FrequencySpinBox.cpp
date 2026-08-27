#include "ui/FrequencySpinBox.h"

#include <algorithm>
#include <cmath>

FrequencySpinBox::FrequencySpinBox(QWidget* parent)
    : QDoubleSpinBox(parent)
    , unitCombo_(new QComboBox())
{
    unitCombo_->addItem(QStringLiteral("Hz"), static_cast<int>(Unit::Hz));
    unitCombo_->addItem(QStringLiteral("kHz"), static_cast<int>(Unit::kHz));
    unitCombo_->addItem(QStringLiteral("MHz"), static_cast<int>(Unit::MHz));
    unitCombo_->addItem(QStringLiteral("GHz"), static_cast<int>(Unit::GHz));
    unitCombo_->setCurrentIndex(static_cast<int>(Unit::MHz));

    updateSpinBoxRangeAndDecimals(Unit::MHz);
    setSuffix(QString());

    connect(this, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &FrequencySpinBox::onSpinValueChanged);
    connect(unitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FrequencySpinBox::onUnitComboIndexChanged);
    connect(this, &QDoubleSpinBox::editingFinished,
            this, &FrequencySpinBox::onEditingFinished);
}

FrequencySpinBox::~FrequencySpinBox()
{
    if (unitCombo_ && !unitCombo_->parent()) {
        delete unitCombo_;
        unitCombo_ = nullptr;
    }
}

QWidget* FrequencySpinBox::createCompoundWidget(QWidget* parent)
{
    if (containerWidget_) {
        return containerWidget_;
    }

    containerWidget_ = new QWidget(parent ? parent : this->parentWidget());
    auto* layout = new QHBoxLayout(containerWidget_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    setParent(containerWidget_);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    unitCombo_->setParent(containerWidget_);
    unitCombo_->setFixedWidth(64);
    unitCombo_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    unitCombo_->setObjectName(objectName().isEmpty() ? QStringLiteral("frequencyUnit")
                                                    : objectName() + QStringLiteral("Unit"));

    layout->addWidget(this, 1);
    layout->addWidget(unitCombo_, 0);
    return containerWidget_;
}

double FrequencySpinBox::unitMultiplier(const Unit unit) noexcept
{
    switch (unit) {
    case Unit::Hz:
        return 1.0;
    case Unit::kHz:
        return 1.0e3;
    case Unit::MHz:
        return 1.0e6;
    case Unit::GHz:
        return 1.0e9;
    }
    return 1.0;
}

int FrequencySpinBox::defaultDecimals(const Unit unit) noexcept
{
    switch (unit) {
    case Unit::Hz:
        return 2;
    case Unit::kHz:
        return 3;
    case Unit::MHz:
        return 6;
    case Unit::GHz:
        return 6;
    }
    return 3;
}

double FrequencySpinBox::defaultStep(const Unit unit) noexcept
{
    switch (unit) {
    case Unit::Hz:
        return 1.0;
    case Unit::kHz:
        return 1.0;
    case Unit::MHz:
        return 1.0;
    case Unit::GHz:
        return 0.01;
    }
    return 1.0;
}

FrequencySpinBox::Unit FrequencySpinBox::bestUnitForFrequency(const double hz) noexcept
{
    const double absHz = std::abs(hz);
    if (absHz >= 1.0e9 - 1e-9) {
        return Unit::GHz;
    }
    if (absHz >= 1.0e6 - 1e-9) {
        return Unit::MHz;
    }
    if (absHz >= 1.0e3 - 1e-9) {
        return Unit::kHz;
    }
    return Unit::Hz;
}

QString FrequencySpinBox::unitString(const Unit unit)
{
    switch (unit) {
    case Unit::Hz:
        return QStringLiteral("Hz");
    case Unit::kHz:
        return QStringLiteral("kHz");
    case Unit::MHz:
        return QStringLiteral("MHz");
    case Unit::GHz:
        return QStringLiteral("GHz");
    }
    return QStringLiteral("Hz");
}

FrequencySpinBox::Unit FrequencySpinBox::unit() const
{
    return static_cast<Unit>(unitCombo_->currentIndex());
}

void FrequencySpinBox::setUnit(const Unit unit)
{
    if (this->unit() == unit) {
        return;
    }
    const QSignalBlocker blocker(unitCombo_);
    unitCombo_->setCurrentIndex(static_cast<int>(unit));
    updateSpinBoxRangeAndDecimals(unit);
    const double displayVal = currentHz_ / unitMultiplier(unit);
    const QSignalBlocker spinBlocker(this);
    setValue(displayVal);
}

void FrequencySpinBox::setFrequencyRangeHz(const double minHz, const double maxHz)
{
    minHz_ = minHz;
    maxHz_ = maxHz;
    updateSpinBoxRangeAndDecimals(unit());
}

void FrequencySpinBox::updateSpinBoxRangeAndDecimals(const Unit unit)
{
    const double mult = unitMultiplier(unit);
    const QSignalBlocker blocker(this);
    setRange(minHz_ / mult, maxHz_ / mult);
    setDecimals(defaultDecimals(unit));
    setSingleStep(defaultStep(unit));
}

void FrequencySpinBox::setFrequencyHz(double hz, const bool autoSelectUnit)
{
    hz = std::clamp(hz, minHz_, maxHz_);
    currentHz_ = hz;

    Unit targetUnit = unit();
    if (autoSelectUnit) {
        const double valInCur = std::abs(hz) / unitMultiplier(targetUnit);
        if (valInCur < 1.0 - 1e-9 || (targetUnit < Unit::MHz && valInCur >= 1000.0 - 1e-9)) {
            targetUnit = bestUnitForFrequency(hz);
        }
    }

    if (unit() != targetUnit) {
        const QSignalBlocker comboBlocker(unitCombo_);
        unitCombo_->setCurrentIndex(static_cast<int>(targetUnit));
        updateSpinBoxRangeAndDecimals(targetUnit);
    }

    const double displayVal = hz / unitMultiplier(targetUnit);
    const QSignalBlocker spinBlocker(this);
    setValue(displayVal);

    emit frequencyChanged(hz);
}

void FrequencySpinBox::stepBy(const int steps)
{
    if (steps == 0) {
        return;
    }

    const Unit curUnit = unit();
    const double curVal = value();
    const double step = singleStep();
    const double targetVal = curVal + static_cast<double>(steps) * step;

    // Case 1: Stepping down below 1.0 into a smaller unit (e.g. 5 kHz -> 1 kHz -> 999 Hz)
    if (steps < 0 && curVal >= 0.0 && targetVal < 1.0 - 1e-9 && curUnit > Unit::Hz) {
        const auto lowerUnit = static_cast<Unit>(static_cast<int>(curUnit) - 1);
        const double lowerMult = unitMultiplier(lowerUnit);
        const double curMult = unitMultiplier(curUnit);

        const double valInLower = curVal * (curMult / lowerMult);
        const double stepInLower = defaultStep(lowerUnit);

        double newTargetInLower = valInLower + static_cast<double>(steps) * stepInLower;
        if (newTargetInLower < minHz_ / lowerMult) {
            newTargetInLower = minHz_ / lowerMult;
        }

        isSyncing_ = true;
        {
            const QSignalBlocker comboBlocker(unitCombo_);
            unitCombo_->setCurrentIndex(static_cast<int>(lowerUnit));
            updateSpinBoxRangeAndDecimals(lowerUnit);
        }
        {
            const QSignalBlocker spinBlocker(this);
            setValue(newTargetInLower);
        }
        currentHz_ = newTargetInLower * lowerMult;
        isSyncing_ = false;

        emit frequencyChanged(currentHz_);
        emit valueChanged(newTargetInLower);
        return;
    }

    // Case 2: Stepping up across 1000.0 into a higher unit (e.g. 999 Hz -> 1.000 kHz)
    if (steps > 0 && curVal >= 0.0 && targetVal >= 1000.0 - 1e-9 && curUnit < Unit::GHz) {
        const auto higherUnit = static_cast<Unit>(static_cast<int>(curUnit) + 1);
        const double higherMult = unitMultiplier(higherUnit);
        const double curMult = unitMultiplier(curUnit);

        const double valInHigher = curVal * (curMult / higherMult);
        const double stepInHigher = defaultStep(higherUnit);

        double newTargetInHigher = (curVal >= 1000.0 - 1e-9)
            ? (valInHigher + static_cast<double>(steps) * stepInHigher)
            : (1.0 + static_cast<double>(steps - 1) * stepInHigher);

        if (newTargetInHigher > maxHz_ / higherMult) {
            newTargetInHigher = maxHz_ / higherMult;
        }

        isSyncing_ = true;
        {
            const QSignalBlocker comboBlocker(unitCombo_);
            unitCombo_->setCurrentIndex(static_cast<int>(higherUnit));
            updateSpinBoxRangeAndDecimals(higherUnit);
        }
        {
            const QSignalBlocker spinBlocker(this);
            setValue(newTargetInHigher);
        }
        currentHz_ = newTargetInHigher * higherMult;
        isSyncing_ = false;

        emit frequencyChanged(currentHz_);
        emit valueChanged(newTargetInHigher);
        return;
    }

    // Normal stepping within the same unit
    QDoubleSpinBox::stepBy(steps);
    currentHz_ = value() * unitMultiplier(curUnit);
    emit frequencyChanged(currentHz_);
}

void FrequencySpinBox::onSpinValueChanged(const double val)
{
    if (isSyncing_) {
        return;
    }
    isSyncing_ = true;
    currentHz_ = val * unitMultiplier(unit());
    emit frequencyChanged(currentHz_);
    isSyncing_ = false;
}

void FrequencySpinBox::onUnitComboIndexChanged(const int index)
{
    if (isSyncing_) {
        return;
    }
    isSyncing_ = true;

    const auto newUnit = static_cast<Unit>(index);
    updateSpinBoxRangeAndDecimals(newUnit);

    const double newDisplayVal = currentHz_ / unitMultiplier(newUnit);
    const QSignalBlocker blocker(this);
    setValue(newDisplayVal);

    isSyncing_ = false;
    emit frequencyChanged(currentHz_);
    emit editingFinished();
}

void FrequencySpinBox::onEditingFinished()
{
    if (isSyncing_) {
        return;
    }
    isSyncing_ = true;

    const QString text = cleanText().trimmed();
    static const QRegularExpression regex(
        QStringLiteral(R"(^\s*([+-]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s*([a-zA-Z]+)?\s*$)"));
    const auto match = regex.match(text);
    if (match.hasMatch() && !match.captured(2).isEmpty()) {
        const double val = match.captured(1).toDouble();
        const QString unitStr = match.captured(2).toLower();
        if (unitStr == QStringLiteral("g") || unitStr == QStringLiteral("ghz")) {
            isSyncing_ = false;
            setFrequencyHz(val * 1.0e9, false);
            setUnit(Unit::GHz);
            return;
        }
        if (unitStr == QStringLiteral("m") || unitStr == QStringLiteral("mhz")) {
            isSyncing_ = false;
            setFrequencyHz(val * 1.0e6, false);
            setUnit(Unit::MHz);
            return;
        }
        if (unitStr == QStringLiteral("k") || unitStr == QStringLiteral("khz")) {
            isSyncing_ = false;
            setFrequencyHz(val * 1.0e3, false);
            setUnit(Unit::kHz);
            return;
        }
        if (unitStr == QStringLiteral("h") || unitStr == QStringLiteral("hz")) {
            isSyncing_ = false;
            setFrequencyHz(val, false);
            setUnit(Unit::Hz);
            return;
        }
    }

    const double curVal = value();
    const Unit curUnit = unit();
    if (std::abs(curVal) > 1e-9) {
        if (curVal < 1.0 && curUnit > Unit::Hz) {
            const double physicalHz = curVal * unitMultiplier(curUnit);
            const Unit newUnit = bestUnitForFrequency(physicalHz);
            if (newUnit != curUnit) {
                isSyncing_ = false;
                setFrequencyHz(physicalHz, true);
                return;
            }
        } else if (curVal >= 1000.0 && curUnit < Unit::GHz) {
            const double physicalHz = curVal * unitMultiplier(curUnit);
            const Unit newUnit = bestUnitForFrequency(physicalHz);
            if (newUnit != curUnit) {
                isSyncing_ = false;
                setFrequencyHz(physicalHz, true);
                return;
            }
        }
    }

    isSyncing_ = false;
}
