// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BUTTONKEYACTIVATION_H
#define BITCOIN_QML_BUTTONKEYACTIVATION_H

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>

// Qt Quick buttons activate on Space only. Desktop buttons also take Return
// and Enter, so those are passed to the focused button as a Space press.
// Check boxes, switches and radio buttons keep Space only.
class ButtonKeyActivation : public QObject
{
public:
    ButtonKeyActivation() { QCoreApplication::instance()->installEventFilter(this); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::KeyPress) return QObject::eventFilter(watched, event);
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter) return QObject::eventFilter(watched, event);
        auto* item = qobject_cast<QQuickItem*>(watched);
        if (!item || !item->window() || item->window()->activeFocusItem() != item || !isPushButton(item)) {
            return QObject::eventFilter(watched, event);
        }
        if (!key->isAutoRepeat() && item->isEnabled()) {
            QKeyEvent press{QEvent::KeyPress, Qt::Key_Space, key->modifiers(), QStringLiteral(" ")};
            QKeyEvent release{QEvent::KeyRelease, Qt::Key_Space, key->modifiers(), QStringLiteral(" ")};
            QCoreApplication::sendEvent(item, &press);
            QCoreApplication::sendEvent(item, &release);
        }
        return true;
    }

private:
    static bool isPushButton(const QObject* item)
    {
        return item->inherits("QQuickAbstractButton")
            && !item->inherits("QQuickCheckBox") && !item->inherits("QQuickCheckDelegate")
            && !item->inherits("QQuickSwitch") && !item->inherits("QQuickSwitchDelegate")
            && !item->inherits("QQuickRadioButton") && !item->inherits("QQuickRadioDelegate");
    }
};

#endif // BITCOIN_QML_BUTTONKEYACTIVATION_H
