// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETNAMESCONTROLLER_H
#define BITCOIN_QML_WALLETNAMESCONTROLLER_H

#include <qml/guipresentation.h>
#include <qml/snapshotquery.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class WalletQmlModel;
namespace interfaces { class Node; }

/** The app's wallet names and files: display names for open wallets, the
 * home folder for file dialogs, and showing a wallet's file location. It
 * reads and writes the names off the GUI thread and applies them to the open
 * wallets it is given. */
class WalletNamesController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl homeUrl READ homeUrl NOTIFY namesChanged)
    Q_PROPERTY(bool locationPending READ locationPending NOTIFY locationChanged)
    //! Why the latest wallet location could not be shown.
    Q_PROPERTY(QString locationError READ locationError NOTIFY locationChanged)
    Q_PROPERTY(bool nameWritePending READ nameWritePending NOTIFY nameWriteChanged)
    Q_PROPERTY(bool nameWriteSucceeded READ nameWriteSucceeded NOTIFY nameWriteChanged)
    Q_PROPERTY(QString nameWriteError READ nameWriteError NOTIFY nameWriteChanged)

public:
    using OpenLocalPath = std::function<bool(const QString&)>;
    struct Services {
        interfaces::Node* node{nullptr};
        std::shared_ptr<execution::Scheduler> scheduler;
        presentation::Post post;
        //! The open wallet after \p wallet, or the first for null; null at the end.
        std::function<WalletQmlModel*(WalletQmlModel* wallet)> next_wallet;
        //! The controller's placeholder for "no wallet".
        WalletQmlModel* empty_wallet{nullptr};
    };
    WalletNamesController(Services services, QObject* parent);

    QUrl homeUrl() const { return QUrl::fromLocalFile(m_home_path); }
    Q_INVOKABLE QString homePath() const { return m_home_path; }
    //! The display name for wallet \p name.
    Q_INVOKABLE QString displayName(const QString& name) const;
    //! Saves a display name for wallet \p name; false when it can't start.
    Q_INVOKABLE bool setDisplayName(const QString& name, const QString& display_name);
    bool nameWritePending() const { return m_write_pending; }
    bool nameWriteSucceeded() const;
    QString nameWriteError() const;

    //! Shows \p wallet's file location in the system file browser.
    Q_INVOKABLE bool openLocation(WalletQmlModel* wallet);
    Q_INVOKABLE void clearLocationError();
    bool locationPending() const { return m_location_pending; }
    QString locationError() const { return m_location_error; }

    //! Reads the saved names again and applies them to the open wallets.
    void refresh();
    //! Applies the saved display name to one wallet.
    void apply(WalletQmlModel* wallet) const;
    void close();
    void setOpenLocalPathForTesting(OpenLocalPath open) { m_open_local_path = std::move(open); }

Q_SIGNALS:
    void namesChanged();
    void nameWriteChanged();
    void locationChanged();

private:
    struct Aliases { std::map<QString, QString> names; QString home; };
    struct WriteResult { QString error; bool saved{false}; };
    struct WriteReceipt { std::mutex mutex; bool ready{false}, saved{false}; QString error; };
    struct LocationResult { QString path, error; };
    void publishAliases();
    void applyAliases(quint64 generation);
    void publishWrite(quint64 id);
    void publishLocation(quint64 id);
    void setLocationError(const QString& error);

    Services m_services;
    bool m_closed{false};
    QString m_home_path;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const Aliases>>> m_query;
    std::shared_ptr<const Aliases> m_names;
    std::optional<WalletQmlModel*> m_cursor;
    quint64 m_generation{0}, m_write_id{0}, m_location_id{0};
    std::shared_ptr<execution::OwnedCommand<WriteResult>> m_write;
    std::shared_ptr<WriteReceipt> m_write_receipt;
    bool m_write_pending{false};
    QString m_write_error;
    std::shared_ptr<execution::OwnedCommand<LocationResult>> m_location;
    QPointer<WalletQmlModel> m_location_wallet;
    bool m_location_pending{false};
    QString m_location_error;
    OpenLocalPath m_open_local_path;
};

#endif // BITCOIN_QML_WALLETNAMESCONTROLLER_H
