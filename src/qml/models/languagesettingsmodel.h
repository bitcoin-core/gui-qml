// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_LANGUAGESETTINGSMODEL_H
#define BITCOIN_QML_MODELS_LANGUAGESETTINGSMODEL_H

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class ArgsManager;
class TranslationManager;
namespace interfaces { class Node; }

class LanguageSettingsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString languageSummary READ languageSummary NOTIFY languageChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(QVariantMap status READ status NOTIFY languageChanged)

public:
    LanguageSettingsModel(interfaces::Node& node, ArgsManager& args, TranslationManager& translations, QObject* parent = nullptr);
    QString language() const;
    void setLanguage(const QString& language);
    QString languageSummary() const { return languageLabel(language()); }
    QStringList availableLanguages() const { return m_available_languages; }
    QVariantMap status() const;
    Q_INVOKABLE QString languageLabel(const QString& locale_tag) const;

Q_SIGNALS:
    void languageChanged();

private:
    interfaces::Node& m_node;
    ArgsManager& m_args;
    TranslationManager& m_translations;
    QStringList m_available_languages;
};

#endif // BITCOIN_QML_MODELS_LANGUAGESETTINGSMODEL_H
