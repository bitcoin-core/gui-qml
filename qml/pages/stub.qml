import QtQml 2.12
import QtQuick.Controls 2.12

ApplicationWindow {
    id: appWindow
    title: "Bitcoin Core TnG"
    minimumWidth: 750
    minimumHeight: 450
    visible: true

    Component.onCompleted: nodeModel.startNodeInitializionThread();
}
