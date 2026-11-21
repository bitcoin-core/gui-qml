// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include <common/args.h>
#include <common/settings.h>
#include <interfaces/node.h>
#include <qml/core_settings.h>
#include <qml/models/externalsignermodel.h>
#include <qml/models/options_model.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

namespace {
class FakeExternalSigner : public interfaces::ExternalSigner
{
public:
    explicit FakeExternalSigner(std::string name) : m_name(std::move(name)) {}
    std::string getName() override { return m_name; }

private:
    std::string m_name;
};

using Signers = std::vector<std::unique_ptr<interfaces::ExternalSigner>>;

Signers MakeSigners(std::initializer_list<const char*> names)
{
    Signers signers;
    for (const char* name : names) signers.emplace_back(std::make_unique<FakeExternalSigner>(name));
    return signers;
}

void SetSavedSigner(MockNode& node, const char* path)
{
    node.get_persistent_setting_fn = [path = std::string{path}](const std::string& name) {
        return name == "signer" && !path.empty() ? common::SettingsValue{path} : common::SettingsValue{};
    };
}

// A settings model whose saves land in its own ArgsManager, which the node
// reads back, as the app's does.
struct SignerFixture {
    MockNode node;
    ArgsManager args;
    QTemporaryDir settings_dir;
    std::unique_ptr<OptionsQmlModel> options;

    explicit SignerFixture(const char* saved_path = "", const char* command_line_path = "")
    {
        args.ForceSetArg("-settings", settings_dir.filePath(QStringLiteral("settings.json")).toStdString());
        if (*saved_path) QmlCoreSettings::SetRwSetting(args, QStringLiteral("signer"), common::SettingsValue{std::string{saved_path}});
        if (*command_line_path) {
            args.LockSettings([&](common::Settings& settings) {
                settings.command_line_options["signer"].push_back(common::SettingsValue{std::string{command_line_path}});
            });
        }
        node.get_persistent_setting_fn = [this](const std::string& name) { return args.GetPersistentSetting(name); };
        options = std::make_unique<OptionsQmlModel>(node, args);
    }

    bool ready()
    {
        return QTest::qWaitFor([&] { return options->settingsReady() && !options->settingsPending(); }, 5000);
    }
};

bool WaitForCheck(ExternalSignerModel& model)
{
    return QTest::qWaitFor([&] { return !model.checking(); }, 5000);
}
} // namespace

class ExternalSignerModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void discoverWithoutPathIsNotResponding();
    void discoverOneSignerNamesTheWallet();
    void discoverMoreThanOneSigner();
    void discoverUsesTheCommandLineSigner();
    void discoverTurnedOffOnCommandLine();
    void discoverSeparatesCommandFromDevice_data();
    void discoverSeparatesCommandFromDevice();
    void suggestedWalletName();
    void checkSavesTheEditedPathThenRunsOffTheGuiThread();
    void checkCanBeRetried();
    void checkWhileOneRunsIsIgnored();
    void editWhileACheckRunsIsRefused();
    void resultIsDroppedWhenThePathChanges();
    void invalidPathIsNotSavedOrRun();
    void commandLinePathIsCheckedWithoutSaving();
    void savedPathReplacesAnUneditedDraft();
    void revertDraftDropsAnEdit();
    void destroyingWhileACheckRunsWaitsForIt();
};

void ExternalSignerModelTests::discoverWithoutPathIsNotResponding()
{
    MockNode node;
    ArgsManager args;
    SetSavedSigner(node, "");
    node.list_external_signers_fn = [] { return MakeSigners({"Ledger Nano X"}); };

    const auto result{ExternalSignerModel::Discover(node, args)};
    QVERIFY(result.path.isEmpty());
    QVERIFY(!result.responding);
    QVERIFY(result.error.isEmpty());
    // The saved (empty) path is applied to the node before running.
    QCOMPARE(node.force_setting_arguments.size(), size_t{1});
    QVERIFY(node.force_setting_arguments.front().second.isNull());
}

void ExternalSignerModelTests::discoverOneSignerNamesTheWallet()
{
    MockNode node;
    ArgsManager args;
    SetSavedSigner(node, "/usr/bin/hwi");
    node.list_external_signers_fn = [] { return MakeSigners({"Coldcard Mk4"}); };

    const auto result{ExternalSignerModel::Discover(node, args)};
    QCOMPARE(result.path, QString{"/usr/bin/hwi"});
    QVERIFY(result.responding);
    QCOMPARE(result.count, 1);
    QCOMPARE(result.name, QString{"Coldcard Mk4"});
    QVERIFY(result.error.isEmpty());
    QCOMPARE(node.force_setting_arguments.size(), size_t{1});
    QCOMPARE(node.force_setting_arguments.front().second.get_str(), std::string{"/usr/bin/hwi"});
}

void ExternalSignerModelTests::discoverMoreThanOneSigner()
{
    MockNode node;
    ArgsManager args;
    SetSavedSigner(node, "/usr/bin/hwi");
    node.list_external_signers_fn = [] { return MakeSigners({"Signer A", "Signer B"}); };

    const auto result{ExternalSignerModel::Discover(node, args)};
    QVERIFY(result.responding);
    QCOMPARE(result.count, 2);
    QVERIFY(result.name.isEmpty());
    QCOMPARE(result.error, QString{"More than one external signer was found. Connect only one device."});
}

// A signer given with -signer is the one checked, even with another path
// saved in settings, and the saved path isn't forced over it.
void ExternalSignerModelTests::discoverUsesTheCommandLineSigner()
{
    ArgsManager args;
    args.LockSettings([](common::Settings& settings) {
        settings.command_line_options["signer"].push_back(common::SettingsValue{std::string{"/opt/cli/hwi"}});
    });
    MockNode node;
    SetSavedSigner(node, "/usr/bin/hwi");
    // Core runs the command-line signer, so a device error names that command.
    node.list_external_signers_fn = []() -> Signers {
        throw std::runtime_error("'/opt/cli/hwi' error: Device is locked");
    };

    const auto result{ExternalSignerModel::Discover(node, args)};
    QCOMPARE(result.path, QString{"/opt/cli/hwi"});
    QVERIFY(result.responding);
    QCOMPARE(result.error, QString{"Your device isn't ready: Device is locked"});
    QVERIFY(node.force_setting_arguments.empty());
}

void ExternalSignerModelTests::discoverTurnedOffOnCommandLine()
{
    ArgsManager args;
    args.LockSettings([](common::Settings& settings) {
        settings.command_line_options["signer"].push_back(common::SettingsValue{false});
    });
    MockNode node;
    SetSavedSigner(node, "/usr/bin/hwi");
    node.list_external_signers_fn = [] { return Signers{}; };

    // -nosigner is not a path, and the saved one doesn't replace it.
    const auto result{ExternalSignerModel::Discover(node, args)};
    QVERIFY(result.path.isEmpty());
    QVERIFY(!result.responding);
    QVERIFY(node.force_setting_arguments.empty());
}

void ExternalSignerModelTests::discoverSeparatesCommandFromDevice_data()
{
    QTest::addColumn<QString>("thrown");
    QTest::addColumn<bool>("responding");
    QTest::addColumn<QString>("error");

    const QString not_responding{"The signer command did not return valid output. Check that the path is correct."};
    // Messages as Core raises them from ExternalSigner::Enumerate() and
    // RunCommandParseJSON() for the command "/usr/bin/hwi".
    QTest::newRow("no device") << QString{} << true << QString{};
    QTest::newRow("command not found") << "execve failed: No such file or directory (2)" << false << not_responding;
    QTest::newRow("command failed") << "RunCommandParseJSON error: process(/usr/bin/hwi enumerate) returned 1: usage\n" << false << not_responding;
    QTest::newRow("output not json") << "Unable to parse JSON: hwi 3.1.0" << false << not_responding;
    QTest::newRow("output not a list") << "'/usr/bin/hwi' received invalid response, expected array of signers" << false << not_responding;
    QTest::newRow("another command") << "'/usr/bin/other' error: Device is locked" << false << not_responding;
    QTest::newRow("device error") << "'/usr/bin/hwi' error: Device is locked" << true << QString{"Your device isn't ready: Device is locked"};
    QTest::newRow("device error without text") << "'/usr/bin/hwi' error" << true << QString{"Your device isn't ready. Unlock it, then check again."};
    // The tool's reason is shown in text that reads tags as markup.
    QTest::newRow("device error that looks like markup") << "'/usr/bin/hwi' error: <img src=\"https://example.com/x.png\">Locked"
                                                         << true << QString{"Your device isn't ready. Unlock it, then check again."};
    QTest::newRow("missing fingerprint") << "'/usr/bin/hwi' received invalid response, missing signer fingerprint" << true
                                         << QString{"Your device didn't share its fingerprint. Unlock it, then check again."};
}

void ExternalSignerModelTests::discoverSeparatesCommandFromDevice()
{
    QFETCH(QString, thrown);
    QFETCH(bool, responding);
    QFETCH(QString, error);

    MockNode node;
    ArgsManager args;
    SetSavedSigner(node, "/usr/bin/hwi");
    node.list_external_signers_fn = [thrown] {
        if (!thrown.isEmpty()) throw std::runtime_error(thrown.toStdString());
        return Signers{};
    };

    const auto result{ExternalSignerModel::Discover(node, args)};
    QCOMPARE(result.responding, responding);
    QCOMPARE(result.error, error);
    QCOMPARE(result.count, 0);
    QVERIFY(result.name.isEmpty());
}

void ExternalSignerModelTests::suggestedWalletName()
{
    QCOMPARE(ExternalSignerModel::SuggestedWalletName("Ledger Nano X"), QString{"Ledger_Nano_X"});
    QCOMPARE(ExternalSignerModel::SuggestedWalletName("  --Coldcard  Mk4"), QString{"Coldcard_Mk4"});
    QCOMPARE(ExternalSignerModel::SuggestedWalletName(""), QString{"external_signer"});
    QCOMPARE(ExternalSignerModel::SuggestedWalletName("A very long hardware wallet name"), QString{"A_very_long_hardware"});
}

void ExternalSignerModelTests::checkSavesTheEditedPathThenRunsOffTheGuiThread()
{
    SignerFixture fixture;
    QVERIFY(fixture.ready());
    std::atomic<bool> ran_on_gui_thread{false};
    QThread* const gui_thread{QThread::currentThread()};
    std::string path_seen;
    fixture.node.list_external_signers_fn = [&] {
        ran_on_gui_thread = QThread::currentThread() == gui_thread;
        path_seen = fixture.args.GetPersistentSetting("signer").get_str();
        return MakeSigners({"Coldcard Mk4"});
    };
    ExternalSignerModel model(fixture.node, fixture.args, *fixture.options);
    QCOMPARE(model.status(), ExternalSignerModel::Status::NotSet);
    QVERIFY(model.canEdit());

    model.setDraftPath(QStringLiteral("  hwi  "));
    QCOMPARE(model.status(), ExternalSignerModel::Status::NotChecked);
    QSignalSpy finished(&model, &ExternalSignerModel::checkFinished);
    model.check();
    QVERIFY(model.checking());
    QCOMPARE(model.status(), ExternalSignerModel::Status::Checking);
    QVERIFY(!model.canCheck());
    QVERIFY(WaitForCheck(model));
    QCOMPARE(finished.count(), 1);

    // The command ran with the saved path, after the save, off the GUI thread.
    QCOMPARE(fixture.options->externalSignerPath(), QString{"hwi"});
    QCOMPARE(path_seen, std::string{"hwi"});
    QVERIFY(!ran_on_gui_thread);
    // The field shows the path as saved.
    QCOMPARE(model.draftPath(), QString{"hwi"});
    QCOMPARE(model.status(), ExternalSignerModel::Status::Ready);
    QVERIFY(model.deviceReady());
    QVERIFY(model.commandResponding());
    QCOMPARE(model.deviceName(), QString{"Coldcard Mk4"});
    QCOMPARE(model.suggestedWalletName(), QString{"Coldcard_Mk4"});
    QVERIFY(model.canCheck());
}

// A device connected or unlocked after a check is found by checking again,
// on the same page, without editing the path.
void ExternalSignerModelTests::checkCanBeRetried()
{
    SignerFixture fixture{"hwi"};
    QVERIFY(fixture.ready());
    std::atomic<int> runs{0};
    fixture.node.list_external_signers_fn = [&]() -> Signers {
        if (++runs == 1) throw std::runtime_error("'hwi' error: Device is locked");
        return MakeSigners({"Trezor T"});
    };
    ExternalSignerModel model(fixture.node, fixture.args, *fixture.options);
    QCOMPARE(model.status(), ExternalSignerModel::Status::NotChecked);

    model.check();
    QVERIFY(WaitForCheck(model));
    QCOMPARE(model.status(), ExternalSignerModel::Status::DeviceNotReady);
    QCOMPARE(model.error(), QString{"Your device isn't ready: Device is locked"});
    QVERIFY(model.commandResponding());
    QVERIFY(model.canCheck());

    model.check();
    QVERIFY(WaitForCheck(model));
    QCOMPARE(runs.load(), 2);
    QCOMPARE(model.status(), ExternalSignerModel::Status::Ready);
    QVERIFY(model.error().isEmpty());
}

void ExternalSignerModelTests::checkWhileOneRunsIsIgnored()
{
    SignerFixture fixture{"hwi"};
    QVERIFY(fixture.ready());
    QSemaphore release;
    std::atomic<int> runs{0};
    fixture.node.list_external_signers_fn = [&] {
        ++runs;
        release.acquire();
        return Signers{};
    };
    ExternalSignerModel model(fixture.node, fixture.args, *fixture.options);
    QSignalSpy finished(&model, &ExternalSignerModel::checkFinished);
    model.check();
    model.check();
    QVERIFY(QTest::qWaitFor([&] { return runs.load() == 1; }, 5000));
    model.check();
    release.release();
    QVERIFY(WaitForCheck(model));
    QCOMPARE(runs.load(), 1);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(model.status(), ExternalSignerModel::Status::NoDevice);
}

// A check saves and runs the path it started with, so an edit made while it
// runs is refused: the field would show one path next to the result of
// another.
void ExternalSignerModelTests::editWhileACheckRunsIsRefused()
{
    SignerFixture fixture{"hwi"};
    QVERIFY(fixture.ready());
    QSemaphore release;
    fixture.node.list_external_signers_fn = [&] {
        release.acquire();
        return MakeSigners({"Trezor T"});
    };
    ExternalSignerModel model(fixture.node, fixture.args, *fixture.options);
    QSignalSpy draft_changed(&model, &ExternalSignerModel::draftPathChanged);
    model.check();
    model.setDraftPath(QStringLiteral("hwi2"));
    const QString draft_during_check{model.draftPath()};
    // Let the check finish before comparing, so a failure doesn't leave it
    // waiting.
    release.release();
    QCOMPARE(draft_during_check, QString{"hwi"});
    // The signal puts a typed field back on the path being checked.
    QCOMPARE(draft_changed.count(), 1);
    QVERIFY(WaitForCheck(model));
    QCOMPARE(model.status(), ExternalSignerModel::Status::Ready);
    QCOMPARE(model.deviceName(), QString{"Trezor T"});
    QCOMPARE(fixture.options->externalSignerPath(), QString{"hwi"});
}

// A result describes the path it ran with, at the time it ran. Moving off
// that path and back needs a new check, since the device may have changed.
void ExternalSignerModelTests::resultIsDroppedWhenThePathChanges()
{
    SignerFixture fixture{"hwi"};
    QVERIFY(fixture.ready());
    fixture.node.list_external_signers_fn = [] { return MakeSigners({"Trezor T"}); };
    ExternalSignerModel model(fixture.node, fixture.args, *fixture.options);
    model.check();
    QVERIFY(WaitForCheck(model));
    QCOMPARE(model.status(), ExternalSignerModel::Status::Ready);

    model.setDraftPath(QStringLiteral("hwi2"));
    QCOMPARE(model.status(), ExternalSignerModel::Status::NotChecked);
    model.setDraftPath(QStringLiteral("hwi"));
    QTRY_VERIFY(!model.pathPending());
    QCOMPARE(model.status(), ExternalSignerModel::Status::NotChecked);
    QVERIFY(!model.deviceReady());
    QVERIFY(model.deviceName().isEmpty());

    model.check();
    QVERIFY(WaitForCheck(model));
    QCOMPARE(model.status(), ExternalSignerModel::Status::Ready);
}

void ExternalSignerModelTests::invalidPathIsNotSavedOrRun()
{
    SignerFixture fixture{"hwi"};
    QVERIFY(fixture.ready());
    std::atomic<bool> ran{false};
    fixture.node.list_external_signers_fn = [&] {
        ran = true;
        return Signers{};
    };
    ExternalSignerModel model(fixture.node, fixture.args, *fixture.options);

    // The check waits for the path to be validated, then stops on its error.
    model.setDraftPath(QStringLiteral("/definitely/not/a/real/signer"));
    QVERIFY(fixture.options->signerPathValidationPending());
    QVERIFY(model.canCheck());
    QSignalSpy finished(&model, &ExternalSignerModel::checkFinished);
    model.check();
    QVERIFY(WaitForCheck(model));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(model.pathError(), QString{"The configured signer path does not exist."});
    QVERIFY(!model.canCheck());
    QCOMPARE(fixture.options->externalSignerPath(), QString{"hwi"});
    QCOMPARE(model.draftPath(), QString{"/definitely/not/a/real/signer"});
    QCOMPARE(model.status(), ExternalSignerModel::Status::NotChecked);
    QVERIFY(!ran.load());
}

// A path set on the command line can't be saved, but can still be checked,
// for example after connecting a device.
void ExternalSignerModelTests::commandLinePathIsCheckedWithoutSaving()
{
    SignerFixture fixture{"saved-hwi", "cli-hwi"};
    QVERIFY(fixture.ready());
    fixture.node.list_external_signers_fn = [] { return MakeSigners({"Trezor T"}); };
    ExternalSignerModel model(fixture.node, fixture.args, *fixture.options);
    QVERIFY(!model.canEdit());
    QVERIFY(model.canCheck());
    QCOMPARE(model.status(), ExternalSignerModel::Status::NotChecked);

    model.check();
    QVERIFY(WaitForCheck(model));
    QCOMPARE(model.status(), ExternalSignerModel::Status::Ready);
    QVERIFY(fixture.node.force_setting_arguments.empty());
    QCOMPARE(QString::fromStdString(fixture.args.GetPersistentSetting("signer").get_str()), QString{"saved-hwi"});
}

// A path saved elsewhere, or loaded at startup, shows in a field that has
// no edit of its own.
void ExternalSignerModelTests::savedPathReplacesAnUneditedDraft()
{
    SignerFixture fixture;
    QVERIFY(fixture.ready());
    ExternalSignerModel model(fixture.node, fixture.args, *fixture.options);
    QSignalSpy draft_changed(&model, &ExternalSignerModel::draftPathChanged);
    fixture.options->setExternalSignerPath(QStringLiteral("hwi"));
    QCOMPARE(model.draftPath(), QString{"hwi"});
    QCOMPARE(draft_changed.count(), 1);
    QTRY_VERIFY(!fixture.options->settingsPending());

    model.setDraftPath(QStringLiteral("hwi-edit"));
    fixture.options->setExternalSignerPath(QStringLiteral("hwi2"));
    QCOMPARE(model.draftPath(), QString{"hwi-edit"});
    QTRY_VERIFY(!fixture.options->settingsPending());
}

void ExternalSignerModelTests::revertDraftDropsAnEdit()
{
    SignerFixture fixture{"hwi"};
    QVERIFY(fixture.ready());
    ExternalSignerModel model(fixture.node, fixture.args, *fixture.options);
    QCOMPARE(model.draftPath(), QString{"hwi"});
    model.setDraftPath(QStringLiteral("hwi-edit"));
    model.revertDraft();
    QCOMPARE(model.draftPath(), QString{"hwi"});
    QTRY_VERIFY(!model.pathPending());
}

void ExternalSignerModelTests::destroyingWhileACheckRunsWaitsForIt()
{
    SignerFixture fixture{"hwi"};
    QVERIFY(fixture.ready());
    QSemaphore started;
    std::atomic<bool> finished{false};
    fixture.node.list_external_signers_fn = [&] {
        started.release();
        QThread::msleep(100);
        finished = true;
        return Signers{};
    };
    auto model = std::make_unique<ExternalSignerModel>(fixture.node, fixture.args, *fixture.options);
    model->check();
    QVERIFY(QTest::qWaitFor([&] { return started.available() == 1; }, 5000));
    model.reset();
    // The node the check used was still there when it finished.
    QVERIFY(finished.load());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ExternalSignerModelTests)
#else
QTEST_MAIN(ExternalSignerModelTests)
#endif
#include "test_externalsignermodel.moc"
