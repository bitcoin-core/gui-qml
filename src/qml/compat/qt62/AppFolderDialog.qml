import Qt.labs.platform 1.1

FolderDialog {
    // QtQuick.Dialogs.FolderDialog exposes the accepted folder as
    // "selectedFolder", while the Qt.labs.platform type calls it "folder".
    readonly property url selectedFolder: folder
}
