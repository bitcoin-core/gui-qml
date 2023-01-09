pragma Singleton
import QtQuick 2.15

Item {
    property bool isDesktop: true
    property bool isMobile: false
    enum Mode {
        DESKTOP,
        MOBILE
    }
    property string state: "MOBILE"
}
