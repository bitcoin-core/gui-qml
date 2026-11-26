// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_LOCALEINTEGER_H
#define BITCOIN_QML_LOCALEINTEGER_H

#include <QLocale>
#include <QIntValidator>
#include <QString>

#include <optional>

class IntegerInputValidator : public QIntValidator
{
    Q_OBJECT
    Q_PROPERTY(QString locale READ localeName WRITE setLocaleName NOTIFY changed)

public:
    using QIntValidator::QIntValidator;

    QString localeName() const { return locale().name(); }
    void setLocaleName(const QString& name) { setLocale(QLocale{name}); }

    // QIntValidator's fixup removes malformed grouping before editingFinished.
    // Keep the original text so invalid input cannot silently change a setting.
    void fixup(QString&) const override { Q_EMIT inputRejected(); }

Q_SIGNALS:
    void inputRejected() const;
};

// Use the input validator's locale and require a complete, in-range integer.
inline std::optional<int> ParseLocalizedInteger(const QString& text, const QString& locale_name, int minimum, int maximum)
{
    bool ok{false};
    const int value = QLocale{locale_name}.toInt(text, &ok);
    if (!ok || value < minimum || value > maximum) return std::nullopt;
    return value;
}

#endif // BITCOIN_QML_LOCALEINTEGER_H
