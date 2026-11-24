// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/imagesavemodel.h>
#include <test/qt_test_registry.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class ImageSaveModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void savesOwnedImageAndReportsWriteFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ImageSaveModel model;
        QImage image{64, 64, QImage::Format_ARGB32};
        image.fill(Qt::red);
        QSignalSpy finished{&model, &ImageSaveModel::finished};
        const auto path = directory.filePath("request.png");
        QVERIFY(model.save(image, QUrl::fromLocalFile(path)));
        QVERIFY(model.pending());
        QVERIFY(!model.save(image, QUrl::fromLocalFile(path)));
        image.fill(Qt::blue);
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.last().at(0).toBool());
        QVERIFY(!model.pending());
        const QImage saved{path};
        QCOMPARE(saved.size(), QSize(64, 64));
        QCOMPARE(saved.pixelColor(0, 0), QColor(Qt::red));
        QVERIFY(model.save(image, QUrl::fromLocalFile(directory.filePath("missing/request.png"))));
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!finished.last().at(0).toBool());
        QVERIFY(!model.pending());
    }
};

BITCOINQML_REGISTER_QT_TEST(ImageSaveModelTests)
#include <test/test_imagesavemodel.moc>
