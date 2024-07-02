// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_ANDROIDCUSTOMDATADIR_H
#define BITCOIN_QML_ANDROIDCUSTOMDATADIR_H

#include <QFile>
#include <QStandardPaths>
#include <QDir>

class AndroidCustomDataDir : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString dataDir READ dataDir WRITE setDataDir NOTIFY dataDirChanged)

public:
    explicit AndroidCustomDataDir(QObject * parent = nullptr);

    QString dataDir() const { return m_data_dir; }
    void setDataDir(const QString & new_data_dir);
    QString readCustomDataDir();

Q_SIGNALS:
    void dataDirChanged();

private:
    QString m_data_dir;
    QString m_default_data_dir;
};

#endif // BITCOIN_QML_ANDROIDCUSTOMDATADIR_H
