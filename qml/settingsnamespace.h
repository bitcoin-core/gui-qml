// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_SETTINGSNAMESPACE_H
#define BITCOIN_QML_SETTINGSNAMESPACE_H
#include <QSettings>
#include <QString>
#include <memory>
/** Scalar identity captured before engine creation. Each engine keeps its own
 * store; pre-init onboarding and final chain namespaces must never share one. */
struct UiPreferencesNamespace {
    QString organization, application;
    QSettings::Format format{QSettings::NativeFormat};
    QSettings::Scope scope{QSettings::UserScope};
    QString file; // Optional explicit INI path for independent integration tests.
    static UiPreferencesNamespace Capture();
};
// Native-only construction. Capturing the scalar identity performs no I/O.
inline std::unique_ptr<QSettings> OpenPreferences(const UiPreferencesNamespace& identity)
{
    if(!identity.file.isEmpty())return std::make_unique<QSettings>(identity.file,identity.format);
    return std::make_unique<QSettings>(identity.format,identity.scope,identity.organization,identity.application);
}
#endif // BITCOIN_QML_SETTINGSNAMESPACE_H
