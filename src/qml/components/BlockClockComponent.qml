import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

BlockClock {
    id: blockClock
    anchors.centerIn: parent
    synced: nodeModel.verificationProgress > 0.999
    pause: nodeModel.pause
    // TODO: Modify following logic once nodeModel.numOutboundPeers property is incorporated.
    conns: true

    states: [
        State {
            name: "intialBlockDownload"; when: !synced && !pause && conns
            PropertyChanges {
                target: blockClock

                ringProgress: nodeModel.verificationProgress
                header: Math.round(ringProgress * 100) + "%"
                subText: Math.round(nodeModel.remainingSyncTime/60000) > 0 ? Math.round(nodeModel.remainingSyncTime/60000) + "mins" : Math.round(nodeModel.remainingSyncTime/1000) + "secs"
            }
        },

        State {
            name: "blockClock"; when: synced && !pause && conns
            PropertyChanges {
                target: blockClock

                ringProgress: blockList[0]
                header: Number(nodeModel.blockTipHeight).toLocaleString(Qt.locale(), 'f', 0)
                subText: "Blocktime"
                blockList: chainModel.timeRatioList
            }
        },

        State {
            name: "Manual Pause"; when: pause
            PropertyChanges {
                target: blockClock

                ringProgress: 0
                header: "Paused"
                headerSize: 24
                subText: "Tap to start"
                blockList: {}
            }
        },

        State {
            name: "Connecting"; when: !pause && !conns
            PropertyChanges {
                target: blockClock

                ringProgress: 0
                header: "Connecting"
                headerSize: 24
                subText: "Please Wait"
                blockList: {}
            }
        }
    ]
}
