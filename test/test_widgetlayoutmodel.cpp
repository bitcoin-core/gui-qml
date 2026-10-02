// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/widgetlayoutmodel.h>

#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSettings>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTimer>
#include <atomic>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <limits>

namespace {
QVariant Size(int columns, int rows) { return QVariantMap{{"columns", columns}, {"rows", rows}}; }
QVariantList Catalog()
{
    return {
        QVariantMap{{"id", "clock"}, {"title", "Clock"}, {"sizes", QVariantList{Size(2, 2), Size(3, 3)}}, {"defaultSize", 1}},
        QVariantMap{{"id", "small"}, {"title", "Small"}, {"sizes", QVariantList{Size(1, 1), Size(2, 1), Size(2, 2), Size(3, 2), Size(3, 3)}}},
        QVariantMap{{"id", "wide"}, {"title", "Wide"}, {"sizes", QVariantList{Size(2, 1), Size(3, 2)}}},
        QVariantMap{{"id", "fixed"}, {"title", "Fixed"}, {"sizes", QVariantList{Size(1, 1)}}},
    };
}
QRect Rect(const QVariantMap& rect)
{
    return QRect(rect.value("column").toInt(), rect.value("row").toInt(), rect.value("columns").toInt(), rect.value("rows").toInt());
}
QVariantList Snapshot(const WidgetLayoutModel& model)
{
    QVariantList result;
    for (const auto& id : {"clock", "small", "wide", "fixed"}) result.append(model.geometry(id));
    return result;
}
void VerifyLayout(const WidgetLayoutModel& model)
{
    QList<QRect> occupied;
    QSet<QString> seen;
    for (int i = 0; i < model.rowCount(); ++i) {
        const QString id = model.data(model.index(i), WidgetLayoutModel::InstanceIdRole).toString();
        QVERIFY(!seen.contains(id));
        seen.insert(id);
        const QRect rect = Rect(model.geometry(id));
        QVERIFY(rect.x() >= 0);
        QVERIFY(rect.y() >= 0);
        QVERIFY(rect.x() + rect.width() <= model.columns());
        QVERIFY(rect.y() + rect.height() <= model.rows());
        const auto sizes = model.data(model.index(i), WidgetLayoutModel::SizesRole).toList();
        QVERIFY(sizes.contains(Size(rect.width(), rect.height())));
        for (const auto& other : occupied) QVERIFY(!rect.intersects(other));
        occupied.append(rect);
    }
}
} // namespace

class WidgetLayoutModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void persistenceUsesDefaultSettingsScope()
    {
        QTemporaryDir dir;
        const auto format = QSettings::defaultFormat();
        const auto organization = QCoreApplication::organizationName();
        const auto domain = QCoreApplication::organizationDomain();
        const auto application = QCoreApplication::applicationName();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName("WidgetTests");
        QCoreApplication::setOrganizationDomain("widget-tests.example");
        QCoreApplication::setApplicationName("regtest");
        bool saved{false};
        {
            AsyncSettingsWriter writer;
            writer.save("layout", [] { return QByteArray("snapshot"); });
            for (int i = 0; i < 1000 && writer.pending(); ++i) QTest::qWait(1);
            QSettings settings;
            saved = settings.value("layout").toByteArray() == QByteArray("snapshot");
        }
        QSettings::setDefaultFormat(format);
        QCoreApplication::setOrganizationName(organization);
        QCoreApplication::setOrganizationDomain(domain);
        QCoreApplication::setApplicationName(application);
        QVERIFY(saved);
    }

    void persistenceCoalescesSnapshotsWithoutBlockingGui()
    {
        QTemporaryDir dir;
        const auto file = dir.filePath("settings.ini");
        QSemaphore entered, release;
        std::atomic<int> writes{0};
        std::atomic<bool> off_gui{false};
        AsyncSettingsWriter writer(file);
        writer.save("layout", [&] {
            off_gui = QThread::currentThread() != QCoreApplication::instance()->thread();
            ++writes;
            entered.release();
            release.acquire();
            return QByteArray("first");
        });
        const bool started = entered.tryAcquire(1, 1000);
        for (int i = 0; i < 20; ++i) {
            writer.save("layout", [&, i] { ++writes; return QByteArray::number(i); });
        }
        int ticks{0};
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start(1);
        QTest::qWait(30);
        release.release();
        QTRY_VERIFY(!writer.pending());
        QVERIFY(started);
        QVERIFY(off_gui.load());
        QVERIFY(ticks > 0);
        QCOMPARE(writes.load(), 2);
        QSettings settings(file, QSettings::IniFormat);
        QCOMPARE(settings.value("layout").toByteArray(), QByteArray("19"));
    }

    void addRemoveAndUniqueIds()
    {
        WidgetLayoutModel model;
        model.setStorageKey({});
        model.setCatalog(Catalog());
        model.restore();
        QCOMPARE(model.count(), 1);
        QCOMPARE(Rect(model.geometry("clock")), QRect(0, 0, 3, 3));
        QVERIFY(model.addWidget("clock", 0));
        const QString copy = model.data(model.index(1), WidgetLayoutModel::InstanceIdRole).toString();
        QVERIFY(copy != "clock");
        QCOMPARE(model.data(model.index(1), WidgetLayoutModel::WidgetIdRole).toString(), QString("clock"));
        QCOMPARE(Rect(model.geometry(copy)).size(), QSize(2, 2));
        VerifyLayout(model);
        QVERIFY(model.removeWidget(copy));
        QVERIFY(model.contains("clock"));
        QVERIFY(!model.addWidget("missing"));
        QVERIFY(!model.addWidget("small", 100));
        QVERIFY(model.addWidget("small", 1));
        QCOMPARE(Rect(model.geometry("small")).size(), QSize(2, 1));
        VerifyLayout(model);
        QVERIFY(model.removeWidget("clock"));
        QVERIFY(model.addWidget("clock", 0));
        VerifyLayout(model);
    }

    void duplicateInstancesStayIndependentAcrossRestartAndBoards()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.ini");
        WidgetLayoutModel model(nullptr, file);
        model.setCatalog(Catalog());
        model.restore();
        QVERIFY(model.removeWidget("clock"));
        QVERIFY(model.addWidget("small", 0));
        QVERIFY(model.addWidget("small", 1));
        const QString copy = model.data(model.index(1), WidgetLayoutModel::InstanceIdRole).toString();
        QVERIFY(copy != "small");
        QVERIFY(model.beginInteraction(copy));
        model.previewMove(4, 1);
        model.commitInteraction();
        QCOMPARE(Rect(model.geometry("small")).size(), QSize(1, 1));
        QVERIFY(model.beginInteraction(copy));
        model.previewResize(2, 2);
        model.commitInteraction();
        QCOMPARE(Rect(model.geometry(copy)).size(), QSize(2, 2));
        const auto original = model.geometry("small");
        const auto duplicate = model.geometry(copy);
        for (int columns : {3, 4, 5, 6}) {
            model.setColumns(columns);
            QCOMPARE(model.count(), 2);
            VerifyLayout(model);
        }
        QCOMPARE(model.geometry("small"), original);
        QCOMPARE(model.geometry(copy), duplicate);
        QTRY_VERIFY(!model.persistencePending());
        WidgetLayoutModel reopened(nullptr, file);
        reopened.setCatalog(Catalog());
        reopened.restore();
        QCOMPARE(reopened.count(), 2);
        QCOMPARE(reopened.geometry(copy), duplicate);
        QCOMPARE(reopened.data(reopened.index(1), WidgetLayoutModel::SourceRole),
                 reopened.data(reopened.index(0), WidgetLayoutModel::SourceRole));
        QVERIFY(reopened.removeWidget("small"));
        QVERIFY(reopened.contains("small"));
        QCOMPARE(reopened.geometry(copy), duplicate);
        for (int columns : {3, 4, 5, 6}) {
            reopened.setColumns(columns);
            QCOMPARE(reopened.count(), 1);
            QVERIFY(!reopened.geometry(copy).isEmpty());
        }
        QTRY_VERIFY(!reopened.persistencePending());
        model.restore();
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.data(model.index(0), WidgetLayoutModel::InstanceIdRole).toString(), copy);
        VerifyLayout(model);
    }

    void collisionPreviewIsStableAndCancelable()
    {
        WidgetLayoutModel model;
        model.setStorageKey({});
        model.setCatalog(Catalog());
        model.restore();
        model.addWidget("small");
        model.addWidget("wide");
        model.addWidget("fixed");
        const auto before = Snapshot(model);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QVERIFY(model.beginInteraction("clock"));
        model.previewMove(3, 0);
        VerifyLayout(model);
        const auto preview = Snapshot(model);
        model.previewMove(0, 4);
        model.previewMove(3, 0);
        QCOMPARE(Snapshot(model), preview);
        QCOMPARE(resets.count(), 0);
        model.cancelInteraction();
        QCOMPARE(Snapshot(model), before);
        QVERIFY(model.activeId().isEmpty());
    }

    void resizeOnlyAllowsDeclaredSizesAndKeepsOrigin()
    {
        WidgetLayoutModel model;
        model.setStorageKey({});
        model.setCatalog(Catalog());
        model.restore();
        model.beginInteraction("clock");
        model.previewResize(1, 1);
        QCOMPARE(Rect(model.geometry("clock")), QRect(0, 0, 2, 2));
        model.previewResize(2.6, 2.6);
        QCOMPARE(Rect(model.geometry("clock")).size(), QSize(3, 3));
        model.previewResize(2.5, 2.5); // Remain stable at the boundary.
        QCOMPARE(Rect(model.geometry("clock")).size(), QSize(3, 3));
        model.previewResize(std::numeric_limits<double>::infinity(), 1);
        QCOMPARE(Rect(model.geometry("clock")).size(), QSize(3, 3));
        model.commitInteraction();
        model.addWidget("fixed");
        model.beginInteraction("fixed");
        model.previewResize(3, 3);
        QCOMPARE(Rect(model.geometry("fixed")).size(), QSize(1, 1));
        VerifyLayout(model);
    }

    void changesSurviveReopeningAfterAsyncSave()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.ini");
        WidgetLayoutModel model(nullptr, file);
        model.setCatalog(Catalog());
        model.restore();
        model.addWidget("wide", 1);
        model.beginInteraction("clock");
        model.previewMove(3, 0);
        model.commitInteraction();
        model.beginInteraction("clock");
        model.previewResize(2, 2);
        model.commitInteraction();
        const auto saved = Snapshot(model);
        // Reopen while the writer is still alive: saving must not depend on destruction.
        QTRY_VERIFY(!model.persistencePending());
        WidgetLayoutModel reopened(nullptr, file);
        reopened.setCatalog(Catalog());
        reopened.restore();
        QCOMPARE(Snapshot(reopened), saved);
        model.beginInteraction("clock");
        model.previewMove(0, 0);
        reopened.restore();
        QCOMPARE(Snapshot(reopened), saved); // Uncommitted previews never reach disk.
        model.cancelInteraction();
        model.removeWidget("clock");
        QTRY_VERIFY(!model.persistencePending());
        reopened.restore();
        QVERIFY(!reopened.contains("clock"));
        model.removeWidget("wide");
        QTRY_VERIFY(!model.persistencePending());
        reopened.restore();
        QCOMPARE(reopened.count(), 0); // An empty saved dashboard is intentional.
    }

    void responsiveReflowDoesNotOverwriteArrangement()
    {
        QTemporaryDir dir;
        WidgetLayoutModel model(nullptr, dir.filePath("settings.ini"));
        model.setCatalog(Catalog());
        model.restore();
        model.beginInteraction("clock");
        model.previewMove(3, 0);
        model.commitInteraction();
        model.addWidget("wide", 1);
        const auto wide = Snapshot(model);
        model.setColumns(3);
        VerifyLayout(model);
        model.setColumns(6);
        QCOMPARE(Snapshot(model), wide);
        model.beginInteraction("clock");
        model.previewMove(0, 10);
        model.setColumns(3);
        QVERIFY(model.activeId().isEmpty());
        model.setColumns(6);
        QCOMPARE(Snapshot(model), wide);
    }

    void restoreRepairsInvalidEntries()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.ini");
        QSettings settings(file, QSettings::IniFormat);
        settings.setValue("dashboard/layout", QByteArray(R"({"version":1,"widgets":[
            {"id":"clock","column":-5,"row":-4,"columns":9,"rows":9},
            {"id":"clock","column":0,"row":0,"columns":3,"rows":3},
            {"id":"obsolete","column":0,"row":0,"columns":1,"rows":1},
            {"id":"wide","column":0,"row":0,"columns":3,"rows":2}]})"));
        settings.sync();
        WidgetLayoutModel model(nullptr, file);
        model.setCatalog(Catalog());
        model.restore();
        QCOMPARE(model.count(), 2);
        VerifyLayout(model);
        QCOMPARE(Rect(model.geometry("clock")), QRect(0, 0, 3, 3));
    }

    void fixedDimensionsAndMovementBounds()
    {
        WidgetLayoutModel model;
        model.setStorageKey({});
        model.setCatalog(Catalog());
        model.restore();
        QCOMPARE(model.columns(), 6);
        QCOMPARE(model.rows(), 3);
        model.beginInteraction("clock");
        model.previewMove(100, 100);
        QCOMPARE(Rect(model.geometry("clock")), QRect(3, 0, 3, 3));
        model.commitInteraction();
        model.setColumns(3);
        QCOMPARE(model.rows(), 6);
        model.beginInteraction("clock");
        model.previewMove(100, 100);
        QCOMPARE(Rect(model.geometry("clock")), QRect(0, 3, 3, 3));
        model.commitInteraction();
        model.setColumns(7); // Only the four supported boards are accepted.
        QCOMPARE(model.columns(), 3);
        VerifyLayout(model);
    }

    void fullBoardRejectsAddAndResize()
    {
        WidgetLayoutModel model;
        model.setStorageKey({});
        auto catalog = Catalog();
        catalog.append(QVariantMap{{"id", "second"}, {"sizes", QVariantList{Size(3, 3)}}});
        model.setCatalog(catalog);
        model.restore();
        QVERIFY(model.addWidget("second"));
        const auto before = model.geometry("clock");
        QVERIFY(!model.addWidget("small"));
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.geometry("clock"), before);
        VerifyLayout(model);
        model.removeWidget("second");
        QVERIFY(model.addWidget("wide", 1));
        QVERIFY(model.addWidget("small", 1)); // 17 cells used.
        model.beginInteraction("small");
        model.previewResize(3, 3);
        QCOMPARE(Rect(model.geometry("small")).size(), QSize(2, 1));
        model.cancelInteraction();
        VerifyLayout(model);
    }

    void orientationAndRemovalPreserveWidgetsThatNeedRoom()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.ini");
        auto catalog = Catalog();
        catalog.append(QVariantMap{{"id", "square"}, {"sizes", QVariantList{Size(2, 2)}}});
        WidgetLayoutModel model(nullptr, file);
        model.setCatalog(catalog);
        model.setColumns(3);
        model.restore();
        model.removeWidget("clock");
        QVERIFY(model.addWidget("small", 2));
        QVERIFY(model.addWidget("square"));
        QVERIFY(model.addWidget("wide", 1)); // Fits 3x6 but cannot fit 6x3.
        QCOMPARE(model.count(), 3);
        const auto saved = model.geometry("wide");
        model.setColumns(6);
        QCOMPARE(model.unplacedCount(), 1);
        QVERIFY(model.contains("wide"));
        QVERIFY(model.addWidget("wide", 0));
        const QString copy = model.data(model.index(model.count() - 1), WidgetLayoutModel::InstanceIdRole).toString();
        QCOMPARE(model.unplacedCount(), 1);
        QVERIFY(model.removeWidget(copy));
        VerifyLayout(model);
        model.setColumns(3);
        QCOMPARE(model.unplacedCount(), 0);
        QCOMPARE(model.geometry("wide"), saved);
        model.setColumns(6);
        model.removeWidget("small"); // Frees room for the preserved widget.
        QCOMPARE(model.unplacedCount(), 0);
        QVERIFY(!model.geometry("wide").isEmpty());
        QTRY_VERIFY(!model.persistencePending());
        WidgetLayoutModel reopened(nullptr, file);
        reopened.setCatalog(catalog);
        reopened.restore();
        QVERIFY(reopened.contains("wide"));
        QVERIFY(reopened.contains("square"));
        QVERIFY(!reopened.contains("small"));
        VerifyLayout(reopened);
    }

    void shrinkingRestoresWidgetsThatNeedRoom()
    {
        WidgetLayoutModel model;
        model.setStorageKey({});
        auto catalog = Catalog();
        catalog.append(QVariantMap{{"id", "square"}, {"sizes", QVariantList{Size(2, 2)}}});
        model.setCatalog(catalog);
        model.setColumns(3);
        model.restore();
        model.removeWidget("clock");
        QVERIFY(model.addWidget("small", 2));
        QVERIFY(model.addWidget("square"));
        QVERIFY(model.addWidget("wide", 1));
        model.setColumns(6);
        QCOMPARE(model.unplacedCount(), 1);
        QVERIFY(model.beginInteraction("small"));
        model.previewResize(1, 1);
        model.commitInteraction();
        QCOMPARE(model.unplacedCount(), 0);
        QCOMPARE(model.count(), 3);
        VerifyLayout(model);
    }

    void eachShapeRetainsPositionsAcrossRestart()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.ini");
        WidgetLayoutModel model(nullptr, file);
        model.setCatalog(Catalog());
        model.restore();
        QMap<int, QVariantMap> saved;
        for (int columns : {6, 3, 4, 5}) {
            model.setColumns(columns);
            QCOMPARE(model.rows(), 9 - columns);
            QVERIFY(model.beginInteraction("clock"));
            model.previewMove(columns - 3, model.rows() - 3);
            model.commitInteraction();
            saved.insert(columns, model.geometry("clock"));
            VerifyLayout(model);
        }
        QTRY_VERIFY(!model.persistencePending());
        WidgetLayoutModel reopened(nullptr, file);
        reopened.setCatalog(Catalog());
        reopened.restore();
        for (int columns : {6, 4, 3, 5, 6}) {
            reopened.setColumns(columns);
            QCOMPARE(reopened.geometry("clock"), saved.value(columns));
            VerifyLayout(reopened);
        }
        reopened.beginInteraction("clock");
        reopened.previewResize(2, 2);
        reopened.commitInteraction();
        QVERIFY(reopened.addWidget("wide"));
        for (int columns : {3, 4, 5, 6}) {
            reopened.setColumns(columns);
            QCOMPARE(Rect(reopened.geometry("clock")).size(), QSize(2, 2));
            QVERIFY(reopened.contains("wide"));
            VerifyLayout(reopened);
        }
        QVERIFY(reopened.removeWidget("wide"));
        for (int columns : {3, 4, 5, 6}) {
            reopened.setColumns(columns);
            QVERIFY(!reopened.contains("wide"));
        }
    }

    void twentyCellBoardsPreserveOverflowOnSmallerShapes()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.ini");
        QVariantList catalog;
        for (int i = 0; i < 21; ++i) {
            catalog.append(QVariantMap{{"id", QString::number(i)}, {"sizes", QVariantList{Size(1, 1)}}});
        }
        WidgetLayoutModel model(nullptr, file);
        model.setCatalog(catalog);
        model.setColumns(4);
        model.restore();
        for (int i = 1; i < 20; ++i) QVERIFY(model.addWidget(QString::number(i)));
        QVERIFY(!model.addWidget("20"));
        QCOMPARE(model.count(), 20);
        model.setColumns(5);
        QCOMPARE(model.count(), 20);
        VerifyLayout(model);
        model.setColumns(6);
        QCOMPARE(model.count(), 18);
        QCOMPARE(model.unplacedCount(), 2);
        QTRY_VERIFY(!model.persistencePending());
        WidgetLayoutModel reopened(nullptr, file);
        reopened.setCatalog(catalog);
        reopened.restore();
        QCOMPARE(reopened.unplacedCount(), 2);
        reopened.setColumns(4);
        QCOMPARE(reopened.count(), 20);
        QCOMPARE(reopened.unplacedCount(), 0);
        VerifyLayout(reopened);
    }

    void migrateLegacyArrangementWithoutLosingItsPositions()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.ini");
        QSettings settings(file, QSettings::IniFormat);
        settings.setValue("dashboard/layout", QByteArray(R"({"version":1,"widgets":[
            {"id":"clock","column":3,"row":0,"columns":3,"rows":3}]})"));
        settings.sync();
        WidgetLayoutModel model(nullptr, file);
        model.setCatalog(Catalog());
        model.setColumns(4);
        model.restore();
        VerifyLayout(model);
        model.setColumns(6);
        QCOMPARE(Rect(model.geometry("clock")), QRect(3, 0, 3, 3));
        QTRY_VERIFY(!model.persistencePending());
        WidgetLayoutModel reopened(nullptr, file);
        reopened.setCatalog(Catalog());
        reopened.restore();
        QCOMPARE(Rect(reopened.geometry("clock")), QRect(3, 0, 3, 3));
    }

    void migrateVersionTwoAndAddAnotherInstance()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.ini");
        QSettings settings(file, QSettings::IniFormat);
        settings.setValue("dashboard/layout", QByteArray(R"({"version":2,
            "widgets":[{"id":"clock","column":3,"row":0,"columns":2,"rows":2}],
            "layouts":{"6":[{"id":"clock","column":3,"row":0,"columns":2,"rows":2}],
                       "3":[{"id":"clock","column":0,"row":3,"columns":2,"rows":2}]}})"));
        settings.sync();
        WidgetLayoutModel model(nullptr, file);
        model.setCatalog(Catalog());
        model.restore();
        QCOMPARE(Rect(model.geometry("clock")), QRect(3, 0, 2, 2));
        model.setColumns(3);
        QCOMPARE(Rect(model.geometry("clock")), QRect(0, 3, 2, 2));
        model.setColumns(6);
        QVERIFY(model.addWidget("clock", 0));
        QTRY_VERIFY(!model.persistencePending());
        WidgetLayoutModel reopened(nullptr, file);
        reopened.setCatalog(Catalog());
        reopened.restore();
        QCOMPARE(reopened.count(), 2);
        QCOMPARE(Rect(reopened.geometry("clock")), QRect(3, 0, 2, 2));
        VerifyLayout(reopened);
    }

    void gridSwitchDoesNotOverwriteUnsupportedSettings()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.ini");
        const QByteArray future{R"({"version":99,"widgets":[]})"};
        QSettings settings(file, QSettings::IniFormat);
        settings.setValue("dashboard/layout", future);
        settings.sync();
        WidgetLayoutModel model(nullptr, file);
        model.setCatalog(Catalog());
        model.restore();
        for (int columns : {3, 4, 5, 6}) model.setColumns(columns);
        settings.sync();
        QCOMPARE(settings.value("dashboard/layout").toByteArray(), future);
    }

    void mixedLayoutsRemainValidAcrossManyEdits()
    {
        WidgetLayoutModel model;
        model.setStorageKey({});
        model.setCatalog(Catalog());
        model.restore();
        for (const auto& id : {"small", "wide", "fixed"}) model.addWidget(id);
        QRandomGenerator random(42);
        const QStringList ids{"clock", "small", "wide", "fixed"};
        for (int i = 0; i < 400; ++i) {
            const auto before = Snapshot(model);
            model.beginInteraction(ids[random.bounded(4)]);
            if (i % 2) model.previewMove(random.bounded(12) - 3, random.bounded(10) - 2);
            else model.previewResize(random.bounded(5), random.bounded(5));
            VerifyLayout(model);
            if (i % 3) model.commitInteraction();
            else { model.cancelInteraction(); QCOMPARE(Snapshot(model), before); }
            if (i % 11 == 0) model.setColumns(3 + random.bounded(4));
            VerifyLayout(model);
        }
    }
};

QTEST_GUILESS_MAIN(WidgetLayoutModelTests)
#include "test_widgetlayoutmodel.moc"
