// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/backuppolicy.h>
#include <qml/models/backuppolicyio.h>
#include <test/qt_test_registry.h>

#include <QBuffer>
#include <QDir>
#include <QScopeGuard>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

#include <algorithm>
#include <array>
#include <thread>

namespace {
std::string Bytes(const QString& text) { const auto bytes = text.toUtf8(); return {bytes.constData(), size_t(bytes.size())}; }
bool Save(const QString& path, const QByteArray& bytes)
{
    QFile file{path};
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray Load(const QString& path) { QFile file{path}; return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
} // namespace

class BackupPolicyTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void transitionGuards_data()
    {
        QTest::addColumn<int>("phase"); QTest::addColumn<int>("event");
        QTest::addColumn<bool>("accepted"); QTest::addColumn<int>("next"); QTest::addColumn<int>("effect");
        // Legal event/phase pairs are the domain contract; every other pair
        // must preserve state and produce no effect, including terminal replay.
        using P = backup::Phase; using E = backup::Event;
        using F = backup::Effect;
        struct Rule { P from; E event; P to; F effect{F::None}; };
        constexpr Rule legal[] = {
            {P::Queued, E::Start, P::Inspecting, F::Inspect},
            {P::Inspecting, E::Inspected, P::Ready},
            {P::AwaitingConfirmation, E::Approve, P::Ready},
            {P::AwaitingConfirmation, E::Refuse, P::Declined},
            {P::Ready, E::AdmitWrite, P::Writing, F::Write},
            {P::Writing, E::Written, P::Succeeded},
            {P::Queued, E::Retire, P::Retired},
            {P::Inspecting, E::Retire, P::Retired},
            {P::AwaitingConfirmation, E::Retire, P::Retired},
            {P::Ready, E::Retire, P::Retired},
            {P::Queued, E::Fault, P::Failed},
            {P::Inspecting, E::Fault, P::Failed},
            {P::AwaitingConfirmation, E::Fault, P::Failed},
            {P::Ready, E::Fault, P::Failed},
            {P::Writing, E::Fault, P::Failed},
        };
        for (int p = 0; p <= int(P::Retired); ++p) {
            for (int e = 0; e <= int(E::Fault); ++e) {
                const auto rule = std::find_if(std::begin(legal), std::end(legal), [p, e](const Rule& row) { return int(row.from) == p && int(row.event) == e; });
                const bool accepted = rule != std::end(legal);
                QTest::newRow(qPrintable(QString("phase-%1-event-%2").arg(p).arg(e)))
                    << p << e << accepted << (accepted ? int(rule->to) : p) << (accepted ? int(rule->effect) : int(F::None));
            }
        }
    }

    void transitionGuards()
    {
        QFETCH(int, phase); QFETCH(int, event); QFETCH(bool, accepted); QFETCH(int, next); QFETCH(int, effect);
        backup::Snapshot initial;
        initial.sequence = 20; initial.phase = backup::Phase(phase); initial.target.path = "/destination.bak";
        const auto decision = backup::Advance(initial, backup::Event(event), initial.target);
        QCOMPARE(decision.accepted, accepted); QCOMPARE(int(decision.snapshot.phase), next);
        QCOMPARE(int(decision.effect), effect); QCOMPARE(decision.snapshot.sequence, uint64_t(20 + int(accepted)));
        QCOMPARE(decision.snapshot.target.path, initial.target.path);
    }
    void inspectionAndWriteOutcomes_data()
    {
        QTest::addColumn<bool>("exists"); QTest::addColumn<bool>("renamed"); QTest::addColumn<int>("error");
        for (int exists = 0; exists < 2; ++exists) for (int renamed = 0; renamed < 2; ++renamed)
            for (int error = 0; error <= int(backup::Error::Transport); ++error)
                QTest::newRow(qPrintable(QString("existing-%1-renamed-%2-error-%3").arg(exists).arg(renamed).arg(error))) << bool(exists) << bool(renamed) << error;
    }
    void inspectionAndWriteOutcomes()
    {
        QFETCH(bool, exists); QFETCH(bool, renamed); QFETCH(int, error);
        backup::Snapshot initial; initial.phase = backup::Phase::Inspecting;
        backup::Proposal target{"/backup.bak", "hash", exists, renamed, backup::Error(error)};
        auto decision = backup::Advance(initial, backup::Event::Inspected, target);
        QCOMPARE(decision.effect, backup::Effect::None);
        QCOMPARE(decision.snapshot.phase, error ? backup::Phase::Failed : exists || renamed ? backup::Phase::AwaitingConfirmation : backup::Phase::Ready);
        QCOMPARE(decision.snapshot.error, backup::Error(error));
        if (!error && (exists || renamed)) {
            decision = backup::Advance(decision.snapshot, backup::Event::Approve);
            QVERIFY(decision.snapshot.destination_approved);
        }
        if (!error) {
            auto write = backup::Advance(decision.snapshot, backup::Event::AdmitWrite);
            QCOMPARE(write.effect, backup::Effect::Write);
            write = backup::Advance(write.snapshot, backup::Event::Written, {}, {backup::Error::CoreBackup});
            QCOMPARE(write.snapshot.phase, backup::Phase::Failed); QCOMPARE(write.snapshot.error, backup::Error::CoreBackup);
        }
    }
    void invalidProposalsCannotWrite()
    {
        backup::Snapshot state; state.phase = backup::Phase::Ready;
        QVERIFY(!backup::Advance(state, backup::Event::AdmitWrite).accepted);
        state.target.path = "/backup.bak"; state.target.error = backup::Error::Inspection;
        QVERIFY(!backup::Advance(state, backup::Event::AdmitWrite).accepted);
        state.target.error = backup::Error::None; state.target.exists = true;
        QVERIFY(!backup::Advance(state, backup::Event::AdmitWrite).accepted);
        state.target.exists = false; state.target.renamed = true;
        QVERIFY(!backup::Advance(state, backup::Event::AdmitWrite).accepted);
        state.phase = backup::Phase::Inspecting; state.target = {};
        QCOMPARE(backup::Advance(state, backup::Event::Inspected).snapshot.error, backup::Error::InvalidPath);
        state.phase = backup::Phase::Queued;
        QCOMPARE(backup::Advance(state, backup::Event::Fault, {}, {backup::Error::Transport}).snapshot.error, backup::Error::Transport);
        QVERIFY(!backup::Advance(state, backup::Event(-1)).accepted);
        QVERIFY(!backup::Advance(state, backup::Event(100)).accepted);
        state.phase = backup::Phase(-1); QVERIFY(!backup::Advance(state, backup::Event::Start).accepted);
        state.phase = backup::Phase(100); QVERIFY(!backup::Advance(state, backup::Event::Start).accepted);
    }
    void destinationNormalization_data()
    {
        QTest::addColumn<QString>("selected"); QTest::addColumn<QString>("final"); QTest::addColumn<bool>("renamed");
        QTest::newRow("bak") << "archive.bak" << "archive.bak" << false;
        QTest::newRow("dat") << "archive.dat" << "archive.dat" << false;
        QTest::newRow("uppercase") << "archive.BAK" << "archive.BAK" << false;
        QTest::newRow("bare") << "archive" << "archive.bak" << true;
        QTest::newRow("other-suffix") << "archive.v1" << "archive.v1.bak" << true;
        QTest::newRow("trailing-dot") << "archive." << "archive..bak" << true;
        QTest::newRow("unicode") << QString::fromUtf8("备份.bak") << QString::fromUtf8("备份.bak") << false;
        QTest::newRow("whitespace-name") << "a b.bak" << "a b.bak" << false;
    }
    void destinationNormalization()
    {
        QFETCH(QString, selected); QFETCH(QString, final); QFETCH(bool, renamed);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto proposal = backup::Inspect(Bytes(dir.filePath(selected)), "wallet");
        QCOMPARE(proposal.path, Bytes(dir.filePath(final))); QCOMPARE(proposal.renamed, renamed);
        QVERIFY(!proposal.exists); QCOMPARE(proposal.error, backup::Error::None);
        const auto url = backup::Inspect(Bytes(QUrl::fromLocalFile(dir.filePath(selected)).toString()), "wallet");
        QCOMPARE(url.path, proposal.path); QCOMPARE(url.error, backup::Error::None);
    }
    void malformedAndUnsupportedPaths()
    {
        for (const std::string& raw : std::array<std::string, 9>{"", "  ", std::string(32'769, 'x'), std::string("a\0b", 3), "https://remote/backup", "file:invalid%zz", "file://", "file://[invalid", "file:///tmp/invalid%00.bak"})
            QCOMPARE(backup::Inspect(raw, "wallet").error, backup::Error::InvalidPath);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QCOMPARE(backup::Inspect(Bytes(dir.filePath("missing/a.bak")), "wallet").error, backup::Error::Inspection);
        auto directory = backup::Inspect(Bytes(dir.path()), "folder/wallet\\name");
        QCOMPARE(directory.path, Bytes(dir.filePath("folder_wallet_name.bak"))); QVERIFY(!directory.renamed);
        QCOMPARE(backup::Inspect(Bytes(dir.path()), "").path, Bytes(dir.filePath("wallet.bak")));
        QVERIFY(Save(dir.filePath("target.bak"), "original"));
        QCOMPARE(backup::Inspect(Bytes(dir.filePath("target.bak/child.bak")), "wallet").error, backup::Error::Inspection);
        QVERIFY(QFile::link(dir.filePath("target.bak"), dir.filePath("link.bak")));
        QCOMPARE(backup::Inspect(Bytes(dir.filePath("link.bak")), "wallet").error, backup::Error::UnsupportedTarget);
    }
    void writeRequiresUnchangedApprovedDestination_data()
    {
        QTest::addColumn<bool>("exists"); QTest::addColumn<bool>("approve"); QTest::addColumn<bool>("change");
        for (int exists = 0; exists < 2; ++exists) for (int approve = 0; approve < 2; ++approve) for (int change = 0; change < 2; ++change)
            QTest::newRow(qPrintable(QString("exists-%1-approve-%2-change-%3").arg(exists).arg(approve).arg(change))) << bool(exists) << bool(approve) << bool(change);
    }
    void writeRequiresUnchangedApprovedDestination()
    {
        QFETCH(bool, exists); QFETCH(bool, approve); QFETCH(bool, change);
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path = dir.filePath("archive.bak");
        if (exists) QVERIFY(Save(path, "original"));
        const auto proposal = backup::Inspect(Bytes(path), "wallet");
        if (change) QVERIFY(Save(path, "interloper"));
        backup::InstallCoordinator coordinator; int calls{0};
        const auto result = backup::Write(proposal, approve, [&](const std::string& staged) {
            ++calls; return Save(QString::fromUtf8(staged.data(), qsizetype(staged.size())), "new-backup");
        }, coordinator);
        const bool success = (!exists || approve) && !change;
        QCOMPARE(result.error, success ? backup::Error::None : backup::Error::Changed);
        QCOMPARE(calls, exists && !approve ? 0 : 1);
        QCOMPARE(Load(path), success ? QByteArray("new-backup") : change ? QByteArray("interloper") : QByteArray("original"));
        QCOMPARE(QDir{dir.path()}.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);
    }
    void rewrittenNewFileRequiresBackendConsent()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString selected = dir.filePath("archive.v1");
        const auto proposal = backup::Inspect(Bytes(selected), "wallet");
        QVERIFY(proposal.renamed); QVERIFY(!proposal.exists); backup::InstallCoordinator coordinator; int calls{0};
        auto produce = [&](const std::string& stage) { ++calls; return Save(QString::fromUtf8(stage.data(), qsizetype(stage.size())), "approved"); };
        QCOMPARE(backup::Write(proposal, false, produce, coordinator).error, backup::Error::Changed);
        QCOMPARE(calls, 0); QVERIFY(!QFileInfo::exists(selected + ".bak"));
        QCOMPARE(backup::Write(proposal, true, produce, coordinator).error, backup::Error::None);
        QCOMPARE(calls, 1); QCOMPARE(Load(selected + ".bak"), QByteArray("approved")); QVERIFY(!QFileInfo::exists(selected));
    }

    void producerFailureAndInvalidStaging_data()
    {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 4; ++i) QTest::newRow(qPrintable(QString("failure-%1").arg(i))) << i;
    }
    void producerFailureAndInvalidStaging()
    {
        QFETCH(int, kind); QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("archive.bak"); QVERIFY(Save(path, "original"));
        const auto proposal = backup::Inspect(Bytes(path), "wallet"); backup::InstallCoordinator coordinator;
        const auto result = backup::Write(proposal, true, [&](const std::string& staged) {
            const QString stage{QString::fromUtf8(staged.data(), qsizetype(staged.size()))};
            if (kind == 0) return false;
            if (kind == 1) return true;
            if (kind == 2) return QDir{}.mkdir(stage);
            return QFile::link(path, stage);
        }, coordinator);
        QCOMPARE(result.error, backup::Error::CoreBackup); QCOMPARE(Load(path), QByteArray("original"));
        QCOMPARE(QDir{dir.path()}.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);
    }
    void failedPreparationPreservesDestination()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString parent = dir.filePath("gone"); QVERIFY(QDir{}.mkdir(parent));
        const QString path = QDir{parent}.filePath("archive.bak"); const auto proposal = backup::Inspect(Bytes(path), "wallet");
        QVERIFY(QDir{}.rmdir(parent)); backup::InstallCoordinator coordinator; bool called{false};
        QCOMPARE(backup::Write(proposal, false, [&](const auto&) { called = true; return true; }, coordinator).error, backup::Error::Staging);
        QVERIFY(!called);
        backup::Proposal invalid; QCOMPARE(backup::Write(invalid, false, {}, coordinator).error, backup::Error::Changed);
        invalid.path = Bytes(path); invalid.error = backup::Error::Inspection;
        QCOMPARE(backup::Write(invalid, false, {}, coordinator).error, backup::Error::Changed);
    }
    void targetMutationDuringCoreBackup_data()
    {
        QTest::addColumn<int>("mutation"); QTest::addColumn<int>("expected");
        QTest::newRow("removed") << 0 << int(backup::Error::Changed);
        QTest::newRow("replaced-with-directory") << 1 << int(backup::Error::UnsupportedTarget);
        QTest::newRow("replaced-with-symlink") << 2 << int(backup::Error::UnsupportedTarget);
        QTest::newRow("unreadable") << 3 << int(backup::Error::Inspection);
    }
    void targetMutationDuringCoreBackup()
    {
        QFETCH(int, mutation); QFETCH(int, expected);
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path = dir.filePath("archive.bak");
        QVERIFY(Save(path, "original")); const auto proposal = backup::Inspect(Bytes(path), "wallet");
        backup::InstallCoordinator coordinator;
        const auto result = backup::Write(proposal, true, [&](const std::string& stage) {
            if (!Save(QString::fromUtf8(stage.data(), qsizetype(stage.size())), "new-backup")) return false;
            if (mutation == 3) return QFile::setPermissions(path, {});
            if (!QFile::remove(path)) return false;
            if (mutation == 1) return QDir{}.mkdir(path);
            if (mutation == 2) return QFile::link(QString::fromUtf8(stage.data(), qsizetype(stage.size())), path);
            return true;
        }, coordinator);
        QCOMPARE(result.error, backup::Error(expected));
        if (mutation == 3) { QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner)); QCOMPARE(Load(path), QByteArray("original")); }
        if (mutation == 0) QVERIFY(!QFileInfo::exists(path));
        if (mutation == 1) QVERIFY(QFileInfo{path}.isDir());
        if (mutation == 2) QVERIFY(QFileInfo{path}.isSymLink());
    }

    void readVersionReportsDeviceFailure()
    {
        class ReadFailure final : public QIODevice {
            qint64 readData(char*, qint64) override { return -1; }
            qint64 writeData(const char*, qint64) override { return -1; }
        public:
            bool atEnd() const override { return false; }
        } failure;
        QVERIFY(failure.open(QIODevice::ReadOnly));
        const auto failed = backup::ReadVersion(failure);
        QCOMPARE(failed.error, backup::Error::Inspection); QVERIFY(failed.digest.empty());
        QBuffer buffer; buffer.setData("abc"); QVERIFY(buffer.open(QIODevice::ReadOnly));
        const auto good = backup::ReadVersion(buffer); QCOMPARE(good.error, backup::Error::None);
        QCOMPARE(QByteArray(good.digest.data(), qsizetype(good.digest.size())).toHex(), QByteArray("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }

    void failedInstallPreservesApprovedDestination_data()
    {
        QTest::addColumn<bool>("exists");
        QTest::newRow("new-file") << false; QTest::newRow("existing-file") << true;
    }
    void failedInstallPreservesApprovedDestination()
    {
        QFETCH(bool, exists); QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("archive.bak");
        if (exists) QVERIFY(Save(path, "original"));
        const auto proposal = backup::Inspect(Bytes(path), "wallet"); backup::InstallCoordinator coordinator;
        QString stage;
        const auto permissions = qScopeGuard([&] { QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner); });
        const auto result = backup::Write(proposal, exists, [&](const std::string& staged) {
            stage = QString::fromUtf8(staged.data(), qsizetype(staged.size()));
            return Save(stage, "new-backup") && QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner);
        }, coordinator);
        QVERIFY(QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QCOMPARE(result.error, backup::Error::Install);
        QCOMPARE(QFileInfo::exists(path), exists);
        if (exists) QCOMPARE(Load(path), QByteArray("original"));
        // The independent permission change can prevent temporary-directory
        // removal too. Restore permissions and dispose only the owned stage.
        if (QFileInfo::exists(stage)) QVERIFY(QFile::remove(stage));
        QDir{}.rmdir(QFileInfo{stage}.absolutePath());
    }

    void competingWalletsCannotOverwriteWithoutFreshConsent()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path = dir.filePath("archive.bak");
        const auto proposal = backup::Inspect(Bytes(path), "wallet"); backup::InstallCoordinator coordinator;
        std::array<backup::WriteResult, 2> results;
        std::array<std::thread, 2> threads;
        for (size_t i = 0; i < threads.size(); ++i) threads[i] = std::thread{[&, i] {
            results[i] = backup::Write(proposal, false, [i](const std::string& stage) {
                return Save(QString::fromUtf8(stage.data(), qsizetype(stage.size())), i ? "wallet-B" : "wallet-A");
            }, coordinator);
        }};
        for (auto& thread : threads) thread.join();
        QCOMPARE(int(results[0].error == backup::Error::None) + int(results[1].error == backup::Error::None), 1);
        QVERIFY(results[0].error == backup::Error::Changed || results[1].error == backup::Error::Changed);
        const auto bytes = Load(path); QVERIFY(bytes == "wallet-A" || bytes == "wallet-B");
    }
};

BITCOINQML_REGISTER_QT_TEST(BackupPolicyTests)
#include <test_backuppolicy.moc>
