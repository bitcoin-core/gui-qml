// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TRANSLATIONMANAGER_H
#define BITCOIN_QML_TRANSLATIONMANAGER_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTranslator>
#include <QVector>

class ArgsManager;
class QQmlEngine;

/** Application-owned catalogs shared by onboarding, the shell and native menus. */
class TranslationManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
public:
    explicit TranslationManager(QObject* parent = nullptr);
    ~TranslationManager() override;
    QString language() const { return m_language; }
    void setLanguage(const QString& language);
    void attachEngine(QQmlEngine& engine);
    static QString ResolveLanguage(ArgsManager& args);

Q_SIGNALS:
    void languageChanged();

private:
    QString m_language;
    bool m_installed{false};
    QTranslator m_core_translator;
    QTranslator m_qml_translator;
    QVector<QPointer<QQmlEngine>> m_engines;
};

#endif // BITCOIN_QML_TRANSLATIONMANAGER_H
