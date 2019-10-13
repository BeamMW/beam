import QtQuick 2.11
import QtQuick.Controls 2.4

import QtQuick.Layouts 1.11
import Beam.Wallet 1.0
import "."

RowLayout {
    id: "root"

    property var    swapCoinName

    property var    swapCoinLockTxId
    property var    swapCoinLockTxConfirmations
    property var    beamLockTxKernelId

    property bool   isBeamSide
    property bool   isProofReceived    // KernelProofHeight != null

    // isBeamSide || (!isBeamSide && isProofReceived)
    property var    beamRedeemTxKernelId
    // isBeamSide && isProofReceived
    property var    swapCoinRedeemTxId
    property var    swapCoinRedeemTxConfirmations
    // isBeamSide && !isProofReceived
    property var    beamRefundTxKernelId

    // !isBeamSide && !isProofReceived
    property var    swapCoinRefundTxId
    property var    swapCoinRefundTxConfirmations

    // property var onOpenExternal: null
    signal textCopied(string text)

    spacing: 30
    GridLayout {
        Layout.fillWidth: true
        Layout.preferredWidth: 4
        Layout.leftMargin: 30
        Layout.topMargin: 30
        Layout.bottomMargin: 30
        columnSpacing: 44
        rowSpacing: 14
        columns: 2
        
        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "lock transaction ID"
            text: swapCoinName + ' ' + qsTrId("swap-details-lock-tx-id") + ":"
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            //wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: root.swapCoinLockTxId
            onCopyText: textCopied(text)
        }

        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "lock transaction confirmations"
            text: swapCoinName + ' ' + qsTrId("swap-details-lock-tx-conf") + ":"
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            //wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: root.swapCoinLockTxConfirmations
            onCopyText: textCopied(text)
        }
        
        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Beam lock transaction kernel ID"
            text: qsTrId("swap-details-beam-lock-kernel-id") + ":"
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            //wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: root.beamLockTxKernelId
            onCopyText: textCopied(text)
        }
        
        SFText {
            enabled: isBeamSide || (!isBeamSide && isProofReceived)
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Beam redeem transaction kernel ID"
            text: qsTrId("swap-details-beam-redeem-kernel-id") + ":"
        }
        SFLabel {
            enabled: isBeamSide || (!isBeamSide && isProofReceived)
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            // wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: root.beamRedeemTxKernelId
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: isBeamSide && isProofReceived
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "redeem transaction ID"
            text: swapCoinName + ' ' + qsTrId("swap-details-redeem-tx-id") + ":"
        }
        SFLabel {
            enabled: isBeamSide && isProofReceived
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            // wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: root.swapCoinRedeemTxId
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: isBeamSide && isProofReceived
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "redeem transaction confirmations"
            text: swapCoinName + ' ' + qsTrId("swap-details-redeem-tx-conf") + ":"
        }
        SFLabel {
            enabled: isBeamSide && isProofReceived
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            // wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: root.swapCoinRedeemTxConfirmations
            onCopyText: textCopied(text)
        }
        
        SFText {
            enabled: isBeamSide && !isProofReceived
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Beam refund transaction kernel ID"
            text: qsTrId("swap-details-beam-refund-kernel-id") + ":"
        }
        SFLabel {
            enabled: isBeamSide && !isProofReceived
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            // wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: root.beamRefundTxKernelId
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: !isBeamSide && !isProofReceived
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "refund transaction ID"
            text: swapCoinName + ' ' + qsTrId("swap-details-refund-tx-id") + ":"
        }
        SFLabel {
            enabled: !isBeamSide && !isProofReceived
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            // wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: root.swapCoinRefundTxId
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: !isBeamSide && !isProofReceived
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "refund transaction confirmations"
            text: swapCoinName + ' ' + qsTrId("swap-details-refund-tx-conf") + ":"
        }
        SFLabel {
            enabled: !isBeamSide && !isProofReceived
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            // wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: root.swapCoinRefundTxConfirmations
            onCopyText: textCopied(text)
        }
        

        function canOpenInBlockchainExplorer(status) {
            switch(status) {
                case "completed":
                case "received":
                case "sent":
                    return true;
                default:
                    return false;
            }
        }

        // Item {
        //     Layout.fillWidth: true
        //     Layout.preferredHeight: 16
        //     visible: parent.canOpenInBlockchainExplorer(root.status)
        // }
        // Item {
        //     Layout.preferredWidth: openInExplorer.width + 10 + openInExplorerIcon.width
        //     Layout.preferredHeight: 16
        //     visible: parent.canOpenInBlockchainExplorer(root.status)

        //     SFText {
        //         id: openInExplorer
        //         font.pixelSize: 14
        //         anchors.left: parent.left
        //         anchors.top: parent.top
        //         anchors.rightMargin: 10
        //         color: Style.active
        //         //% "Open in Blockchain Explorer"
        //         text: qsTrId("open-in-explorer")
        //     }
        //     SvgImage {
        //         id: openInExplorerIcon
        //         anchors.top: parent.top
        //         anchors.right: parent.right
        //         source: "qrc:/assets/icon-external-link-green.svg"
        //     }
        //     MouseArea {
        //         anchors.fill: parent
        //         acceptedButtons: Qt.LeftButton
        //         cursorShape: Qt.PointingHandCursor
        //         onClicked: {
        //             if (onOpenExternal && typeof onOpenExternal === 'function') {
        //                 onOpenExternal();
        //             }
        //         }
        //         hoverEnabled: true
        //     }
        // }
        // SFText {
        //     Layout.alignment: Qt.AlignTop
        //     font.pixelSize: 14
        //     color: Style.content_secondary
        //     //% "Error"
        //     text: qsTrId("tx-details-error-label") + ":"
        //     visible: root.failureReason.length > 0
        // }
        // SFLabel {
        //     id: failureReason
        //     Layout.fillWidth: true
        //     copyMenuEnabled: true
        //     font.pixelSize: 14
        //     color: Style.content_main
        //     wrapMode: Text.Wrap
        //     visible: root.failureReason.length > 0
        //     text: root.failureReason.length > 0 ? root.failureReason : ""
        //     font.styleName: "Italic"
        //     elide: Text.ElideRight
        //     onCopyText: textCopied(text)
        // }
    }
}
