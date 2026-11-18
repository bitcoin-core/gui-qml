// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/qmlfiledialog.h>

#include <QApplication>
#include <QFileDialog>
#include <QLineEdit>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QUrl>

namespace {
//! The chooser owns its QFileDialog privately, so reach it as a top level widget.
QFileDialog* VisibleFileDialog()
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* dialog = qobject_cast<QFileDialog*>(widget)) {
            if (dialog->isVisible()) return dialog;
        }
    }
    return nullptr;
}
} // namespace

class QmlFileDialogTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void saveModeAsksToSaveNotToOpen();
    void openModeRequiresAnExistingFile();
    void directoryModeShowsDirectoriesOnly();
    void onlyDirectoryModeShowsHiddenEntries();
    void propertiesReachTheDialog();
    void acceptedCarriesTheSelectedFile();
    void overwritePolicyIsExplicitAndRetained();
    void closingChooserDoesNotCloseTheApplication();
    void chooserAdmissionRejectsOverlapAndReentry();
    void cancellingTheVisibleChooserReleasesModalityAndAdmission();
    void retiredNativeCallbackCannotChooseForANewContext();
    void contextChangeDuringFilePublicationRejectsAcceptance();
    void disablingDuringCompletionRejectsAcceptance();
};

void QmlFileDialogTests::chooserAdmissionRejectsOverlapAndReentry()
{
    QmlFileDialog first, second;
    QVERIFY(first.open());
    QVERIFY(!first.open());
    QVERIFY(!second.open());
    bool reentered{true}, overlapped{true};
    connect(&first, &QmlFileDialog::rejected, this, [&] { reentered = first.open(); overlapped = second.open(); });
    first.close();
    QVERIFY(!reentered);
    QVERIFY(!overlapped);
    QVERIFY(!first.isVisible());
    QVERIFY(second.open());
    second.close();
}

// #1066: the platform-independent part of the overlap. While Save is open, an
// Import request is refused without ever reporting a visible chooser. When the
// user dismisses Save through the dialog itself, no modal dialog or admission
// is left behind and Import presents a chooser that releases the same way.
void QmlFileDialogTests::cancellingTheVisibleChooserReleasesModalityAndAdmission()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QmlFileDialog save, import;
    save.setFileMode(QmlFileDialog::SaveFile);
    save.setCurrentFile(QUrl::fromLocalFile(directory.filePath("transaction.psbt")));
    import.setFileMode(QmlFileDialog::OpenFile);
    QSignalSpy import_visible{&import, &QmlFileDialog::visibleChanged};
    QSignalSpy save_rejected{&save, &QmlFileDialog::rejected};
    QVERIFY(save.open());
    const QPointer<QFileDialog> save_dialog = VisibleFileDialog(); QVERIFY(save_dialog);
    QVERIFY(save_dialog->isModal());
    QVERIFY(!import.open());
    QVERIFY(!import.isVisible()); QCOMPARE(import_visible.count(), 0);
    QCOMPARE(VisibleFileDialog(), save_dialog.data());
    save_dialog->reject();
    QCOMPARE(save_rejected.count(), 1);
    QVERIFY(!save.isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!VisibleFileDialog()); QVERIFY(!QApplication::activeModalWidget());
    QVERIFY(import.open());
    QVERIFY(import.isVisible()); QCOMPARE(import_visible.count(), 1);
    const QPointer<QFileDialog> import_dialog = VisibleFileDialog(); QVERIFY(import_dialog); QVERIFY(import_dialog != save_dialog);
    import_dialog->reject();
    QVERIFY(!import.isVisible()); QCOMPARE(import_visible.count(), 2);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!VisibleFileDialog()); QVERIFY(!QApplication::activeModalWidget());
    QVERIFY(save.open()); save.close();
}

void QmlFileDialogTests::retiredNativeCallbackCannotChooseForANewContext()
{
    QObject original, replacement;
    QmlFileDialog chooser;
    chooser.setContextObject(&original);
    QVERIFY(chooser.open());
    const QPointer<QFileDialog> old = VisibleFileDialog();
    QVERIFY(old);
    QSignalSpy accepted{&chooser, &QmlFileDialog::accepted};
    QSignalSpy rejected{&chooser, &QmlFileDialog::rejected};
    QVERIFY(QMetaObject::invokeMethod(old, "accepted", Qt::QueuedConnection));
    chooser.setContextObject(&replacement);
    QCOMPARE(rejected.count(), 1);
    QVERIFY(!chooser.isVisible());
    QVERIFY(chooser.open());
    const QPointer<QFileDialog> current = VisibleFileDialog();
    QVERIFY(current);
    QVERIFY(current != old);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(rejected.count(), 1);
    QVERIFY(chooser.isVisible());
    chooser.close();
    QCOMPARE(rejected.count(), 2);
}

void QmlFileDialogTests::contextChangeDuringFilePublicationRejectsAcceptance()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QObject original, replacement;
    QmlFileDialog chooser;
    chooser.setFileMode(QmlFileDialog::SaveFile);
    chooser.setContextObject(&original);
    chooser.setCurrentFile(QUrl::fromLocalFile(directory.filePath("backup.bak")));
    QVERIFY(chooser.open());
    const QPointer<QFileDialog> native = VisibleFileDialog();
    QVERIFY(native);
    QSignalSpy accepted{&chooser, &QmlFileDialog::accepted};
    QSignalSpy rejected{&chooser, &QmlFileDialog::rejected};
    connect(&chooser, &QmlFileDialog::selectedFileChanged, this, [&] { chooser.setContextObject(&replacement); });
    QVERIFY(QMetaObject::invokeMethod(native, "accepted", Qt::DirectConnection));
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(rejected.count(), 1);
    QVERIFY(!chooser.isVisible());
}

void QmlFileDialogTests::disablingDuringCompletionRejectsAcceptance()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QmlFileDialog chooser;
    chooser.setFileMode(QmlFileDialog::SaveFile);
    chooser.setCurrentFile(QUrl::fromLocalFile(directory.filePath("backup.bak")));
    QVERIFY(chooser.open());
    const QPointer<QFileDialog> native = VisibleFileDialog();
    QVERIFY(native);
    QSignalSpy accepted{&chooser, &QmlFileDialog::accepted};
    QSignalSpy rejected{&chooser, &QmlFileDialog::rejected};
    connect(&chooser, &QmlFileDialog::visibleChanged, this, [&] { if (!chooser.isVisible()) chooser.setEnabled(false); });
    QVERIFY(QMetaObject::invokeMethod(native, "accepted", Qt::DirectConnection));
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(rejected.count(), 1);
    QVERIFY(!chooser.open());
    chooser.setEnabled(true);
    QVERIFY(chooser.open());
    chooser.close();
}

void QmlFileDialogTests::saveModeAsksToSaveNotToOpen()
{
    QmlFileDialog chooser;
    chooser.setFileMode(QmlFileDialog::SaveFile);
    chooser.open();

    QFileDialog* dialog = VisibleFileDialog();
    QVERIFY(dialog);
    // The QtQuick.Dialogs fallback hardcodes Open | Cancel whatever fileMode
    // says, which is what made a new file name impossible to commit. The accept
    // button follows from the accept mode, and its label is only filled in once
    // the dialog builds its widgets, which an offscreen platform never does.
    QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptSave);
    QCOMPARE(dialog->fileMode(), QFileDialog::AnyFile);
    chooser.close();
}

void QmlFileDialogTests::openModeRequiresAnExistingFile()
{
    QmlFileDialog chooser;
    chooser.setFileMode(QmlFileDialog::OpenFile);
    chooser.open();

    QFileDialog* dialog = VisibleFileDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptOpen);
    QCOMPARE(dialog->fileMode(), QFileDialog::ExistingFile);
    QVERIFY(!dialog->testOption(QFileDialog::ShowDirsOnly));
    chooser.close();
}

void QmlFileDialogTests::directoryModeShowsDirectoriesOnly()
{
    QmlFileDialog chooser;
    chooser.setFileMode(QmlFileDialog::Directory);
    chooser.open();

    QFileDialog* dialog = VisibleFileDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->fileMode(), QFileDialog::Directory);
    QVERIFY(dialog->testOption(QFileDialog::ShowDirsOnly));
    chooser.close();
}

void QmlFileDialogTests::onlyDirectoryModeShowsHiddenEntries()
{
    QmlFileDialog chooser;
    chooser.setFileMode(QmlFileDialog::Directory);
    chooser.open();

    QFileDialog* dialog = VisibleFileDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->filter().testFlag(QDir::Hidden));
    chooser.close();

    // The chooser reuses its dialog, so a later file mode must drop the flag.
    chooser.setFileMode(QmlFileDialog::OpenFile);
    chooser.open();
    dialog = VisibleFileDialog();
    QVERIFY(dialog);
    QVERIFY(!dialog->filter().testFlag(QDir::Hidden));
    chooser.close();
}

void QmlFileDialogTests::propertiesReachTheDialog()
{
    QmlFileDialog chooser;
    chooser.setTitle(QStringLiteral("Export activity"));
    chooser.setDefaultSuffix(QStringLiteral("csv"));
    chooser.setNameFilters({QStringLiteral("Comma separated file (*.csv)")});
    chooser.setCurrentFolder(QUrl::fromLocalFile(QDir::tempPath()));
    chooser.open();

    QFileDialog* dialog = VisibleFileDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->windowTitle(), QStringLiteral("Export activity"));
    QCOMPARE(dialog->defaultSuffix(), QStringLiteral("csv"));
    QCOMPARE(dialog->nameFilters(), QStringList{QStringLiteral("Comma separated file (*.csv)")});
    QCOMPARE(dialog->directory().absolutePath(), QDir::tempPath());
    chooser.close();
}

void QmlFileDialogTests::overwritePolicyIsExplicitAndRetained()
{
    QmlFileDialog chooser;
    QVERIFY(chooser.confirmOverwrite());
    QSignalSpy changes{&chooser, &QmlFileDialog::confirmOverwriteChanged};
    chooser.setFileMode(QmlFileDialog::SaveFile);
    chooser.open();
    QVERIFY(VisibleFileDialog());
    QVERIFY(!VisibleFileDialog()->testOption(QFileDialog::DontConfirmOverwrite));
    chooser.close();
    chooser.setConfirmOverwrite(false);
    chooser.setConfirmOverwrite(false);
    QCOMPARE(changes.count(), 1);
    chooser.open();
    QVERIFY(VisibleFileDialog()->testOption(QFileDialog::DontConfirmOverwrite));
    chooser.close();
    chooser.setConfirmOverwrite(true);
    chooser.open();
    QVERIFY(!VisibleFileDialog()->testOption(QFileDialog::DontConfirmOverwrite));
    chooser.close();
    QCOMPARE(changes.count(), 2);
}

void QmlFileDialogTests::closingChooserDoesNotCloseTheApplication()
{
    QQuickWindow main;
    main.show();
    QmlFileDialog chooser; chooser.open();
    auto* dialog = VisibleFileDialog(); QVERIFY(dialog);
    QVERIFY(!dialog->testAttribute(Qt::WA_QuitOnClose));
    QSignalSpy last_closed{qGuiApp, &QGuiApplication::lastWindowClosed};
    chooser.close(); QCoreApplication::processEvents();
    QCOMPARE(last_closed.count(), 0); QVERIFY(main.isVisible());
}

void QmlFileDialogTests::acceptedCarriesTheSelectedFile()
{
    // Offscreen takes the native path, which emits urlSelected and hides the bug.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    const auto restore{qScopeGuard([] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false); })};

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QmlFileDialog chooser;
    chooser.setFileMode(QmlFileDialog::SaveFile);
    chooser.setCurrentFolder(QUrl::fromLocalFile(dir.path()));
    chooser.open();

    QFileDialog* dialog = VisibleFileDialog();
    QVERIFY(dialog);

    QSignalSpy accepted(&chooser, &QmlFileDialog::accepted);
    QUrl selected_on_accept;
    connect(&chooser, &QmlFileDialog::accepted, this, [&] { selected_on_accept = chooser.selectedFile(); });
    auto* name_field{dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))};
    QVERIFY(name_field);
    name_field->clear();
    QTest::keyClicks(name_field, QStringLiteral("activity.csv"));
    QTest::keyClick(name_field, Qt::Key_Return);

    const QUrl expected{QUrl::fromLocalFile(dir.filePath(QStringLiteral("activity.csv")))};
    QCOMPARE(accepted.count(), 1);
    QCOMPARE(selected_on_accept, expected);
    QCOMPARE(chooser.selectedFile(), expected);
    QVERIFY(!chooser.isVisible());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QmlFileDialogTests)
#else
QTEST_MAIN(QmlFileDialogTests)
#endif
#include <test_qmlfiledialog.moc>
