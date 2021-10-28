// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_CHAINMODEL_H
#define BITCOIN_QML_CHAINMODEL_H

#include <memory>

#include <QAbstractListModel>
#include <QQmlParserStatus>

class ChainModelPrivate;
class ChainModel : public QAbstractListModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    std::unique_ptr<ChainModelPrivate> d;

public:
    enum Role {
        BlockHeightRole = Qt::UserRole + 1,
        BlockHashRole,
    };

    explicit ChainModel(QObject* parent = nullptr);
    ~ChainModel();

    // QQmlParserStatus
    void classBegin() override;
    void componentComplete() override;

    // QAbstractListModel
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    int rowCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
};

#endif // BITCOIN_QML_CHAINMODEL_H
