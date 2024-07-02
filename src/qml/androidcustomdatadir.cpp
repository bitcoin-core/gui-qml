// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/androidcustomdatadir.h>

#include <common/args.h>
#include <qml/util.h>
#include <qml/guiconstants.h>
#include <qt/guiutil.h>

#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QDir>

AndroidCustomDataDir::AndroidCustomDataDir(QObject * parent)
    : QObject(parent)
{
    m_default_data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

void AndroidCustomDataDir::setDataDir(const QString & new_data_dir)
{
    if (m_data_dir == new_data_dir) {
        return;
    }

    m_data_dir = new_data_dir;
    gArgs.SoftSetArg("-datadir", fs::PathToString(GUIUtil::QStringToPath(m_data_dir)));
    gArgs.ClearPathCache();
    Q_EMIT dataDirChanged();
}

QString AndroidCustomDataDir::readCustomDataDir()
{
    QFile file(m_default_data_dir + "/filepath.txt");
    QString storedPath;

    if (file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);
        storedPath = in.readAll().trimmed();
        file.close();
        // Process the retrieved path
        qDebug() << "Retrieved path: " << storedPath;
    }
    return storedPath;
}
