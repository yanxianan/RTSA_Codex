#pragma once

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QWidget>

class FrequencySpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    enum class Unit {
        Hz = 0,
        kHz = 1,
        MHz = 2,
        GHz = 3
    };
    Q_ENUM(Unit)

    explicit FrequencySpinBox(QWidget* parent = nullptr);
    ~FrequencySpinBox() override;

    [[nodiscard]] QComboBox* unitComboBox() const noexcept { return unitCombo_; }
    QWidget* createCompoundWidget(QWidget* parent = nullptr);

    [[nodiscard]] double frequencyHz() const noexcept { return currentHz_; }
    void setFrequencyHz(double hz, bool autoSelectUnit = true);

    [[nodiscard]] double valueMHz() const noexcept { return currentHz_ / 1.0e6; }
    void setValueMHz(double mhz) { setFrequencyHz(mhz * 1.0e6, true); }

    void setFrequencyRangeHz(double minHz, double maxHz);
    [[nodiscard]] double minimumFrequencyHz() const noexcept { return minHz_; }
    [[nodiscard]] double maximumFrequencyHz() const noexcept { return maxHz_; }

    [[nodiscard]] Unit unit() const;
    void setUnit(Unit unit);

    void stepBy(int steps) override;

    static double unitMultiplier(Unit unit) noexcept;
    static int defaultDecimals(Unit unit) noexcept;
    static double defaultStep(Unit unit) noexcept;
    static Unit bestUnitForFrequency(double hz) noexcept;
    static QString unitString(Unit unit);

protected:
    void changeEvent(QEvent* event) override;

signals:
    void frequencyChanged(double hz);

private slots:
    void onSpinValueChanged(double val);
    void onUnitComboIndexChanged(int index);
    void onEditingFinished();

private:
    void updateSpinBoxRangeAndDecimals(Unit unit);

    QComboBox* unitCombo_ = nullptr;
    QWidget* containerWidget_ = nullptr;
    double minHz_ = 0.0;
    double maxHz_ = 25.0e9;
    double currentHz_ = 1000.0e6;
    bool isSyncing_ = false;
};
