// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_FEEBUMPMODEL_H
#define BITCOIN_QML_MODELS_FEEBUMPMODEL_H

#include <qml/models/feebumpinterface.h>

#include <QHash>
#include <QList>
#include <QPointer>

#include <functional>

/** A wallet's fee bumps (FeeBumpInterface). It owns every attempt it
 * creates, forwards the wallet's display unit and publication retries to
 * them, and closes them when the wallet unloads. */
class FeeBumpModel final : public FeeBumpInterface
{
    Q_OBJECT
public:
    //! Creates a configured attempt owned by `parent`.
    using Create = std::function<FeeBumpOperation*(QObject* parent)>;
    FeeBumpModel(std::function<bool()> available, Create create, QObject* parent);

    bool available() const override { return !m_closed && m_available && m_available(); }
    FeeBumpOperation* openFeeBump(const QString& txid, unsigned target_blocks) override;
    void presented(FeeBumpOperation* attempt) override;
    //! A new attempt for `txid`, held by a C++ caller that drives it itself
    //! (tools and tests); release() it like a page does.
    FeeBumpOperation* attemptFor(const QString& txid);
    void release(FeeBumpOperation* attempt) override;

    void setDisplayUnit(int unit);
    void retryPublications();
    void close();
    void refresh() { Q_EMIT availableChanged(); }
    //! The attempt kept for `txid`, if any (tests).
    FeeBumpOperation* kept(const QString& txid) const { return m_kept.value(txid); }

private:
    void forget(FeeBumpOperation* attempt);

    const std::function<bool()> m_available;
    const Create m_create;
    QList<QPointer<FeeBumpOperation>> m_attempts;
    //! Attempts held by a controller.
    QHash<FeeBumpOperation*, int> m_holds;
    //! Per transaction, the attempt past its commit point not yet shown.
    QHash<QString, QPointer<FeeBumpOperation>> m_kept;
    QHash<FeeBumpOperation*, QString> m_txids;
    int m_display_unit{0};
    bool m_closed{false};
};

#endif // BITCOIN_QML_MODELS_FEEBUMPMODEL_H
