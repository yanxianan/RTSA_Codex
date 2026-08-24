#pragma once

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QObject>
#include <QSlider>
#include <QWidget>

namespace rtsa {

/**
 * @brief 工业级焦点门控滚轮事件过滤器 (Focus-Gated Wheel Event Filter)
 *
 * 解决在包含大量输入控件的滚动面板中，鼠标上下滑动误触修改控件数值的问题。
 * - 未获得焦点的输入控件（!hasFocus()）忽略滚轮事件，让事件自然冒泡至父级 QScrollArea 实现页面滚动；
 * - 只有用户显式点击获取焦点（hasFocus() == true）后，才允许滚轮调节控件数值。
 */
class UnfocusedWheelFilter final : public QObject {
public:
    explicit UnfocusedWheelFilter(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    static void installRecursively(QWidget* root)
    {
        if (!root) {
            return;
        }
        auto* filter = new UnfocusedWheelFilter(root);
        applyFilter(root, filter);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Wheel) {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget && !widget->hasFocus()) {
                event->ignore();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static void applyFilter(QWidget* root, UnfocusedWheelFilter* filter)
    {
        for (auto* child : root->findChildren<QWidget*>()) {
            if (qobject_cast<QAbstractSpinBox*>(child)
                || qobject_cast<QComboBox*>(child)
                || qobject_cast<QSlider*>(child)) {
                child->setFocusPolicy(Qt::StrongFocus);
                child->installEventFilter(filter);
            }
        }
    }
};

} // namespace rtsa
