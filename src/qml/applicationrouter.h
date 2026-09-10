// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_APPLICATIONROUTER_H
#define BITCOIN_QML_APPLICATIONROUTER_H

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

/** The only owner of application destinations and history. Pages emit intent. */
class ApplicationRouter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentRoute READ currentRoute NOTIFY currentChanged)
    Q_PROPERTY(QUrl currentSource READ currentSource NOTIFY currentChanged)
    Q_PROPERTY(QVariantMap currentParameters READ currentParameters NOTIFY currentChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY currentChanged)
    Q_PROPERTY(bool shuttingDown READ shuttingDown NOTIFY currentChanged)

public:
    struct Destination {
        QString id;
        QUrl source;
        QByteArray title;
        bool in_menu{true};
        QString selection;
        bool enabled{true};
    };

    explicit ApplicationRouter(QObject* parent = nullptr);
    bool registerDestination(Destination destination);
    void setDestinationEnabled(const QString& id, bool enabled);
    const QVector<Destination>& destinations() const { return m_destinations; }
    QString currentRoute() const;
    QUrl currentSource() const;
    QVariantMap currentParameters() const;
    QString currentTitle() const;
    QString currentSelection() const;
    bool canGoBack() const { return !m_shutting_down && m_history.size() > 1; }
    bool shuttingDown() const { return m_shutting_down; }
    Q_INVOKABLE bool navigate(const QString& id, const QVariantMap& parameters = {});
    Q_INVOKABLE void back();
    void beginShutdown();
    void retranslate();
    static QString title(const Destination& destination);

Q_SIGNALS:
    void currentChanged();
    void routeChanged();
    void destinationsChanged();

private:
    struct Entry { QString id; QVariantMap parameters; };
    const Destination* find(const QString& id) const;
    QVector<Destination> m_destinations;
    QVector<Entry> m_history;
    bool m_shutting_down{false};
};

/** Read-only menu projection; never stores its own selected route. */
class NavigationModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { RouteRole = Qt::UserRole + 1, LabelRole, SelectedRole, EnabledRole };
    explicit NavigationModel(ApplicationRouter& router, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void rebuild();
    ApplicationRouter& m_router;
    QVector<int> m_rows;
};

#endif // BITCOIN_QML_APPLICATIONROUTER_H
