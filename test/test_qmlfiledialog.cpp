// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/qmlfiledialog.h>

#include <QApplication>
#include <QFileDialog>
#include <QLineEdit>
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
};

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
#include "test_qmlfiledialog.moc"
