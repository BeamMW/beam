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
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-lock-tx-id") + ":"
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
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-lock-tx-conf") + ":"
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
            //% "BEAM lock transaction kernel ID"
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
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "BEAM redeem transaction kernel ID"
            text: qsTrId("swap-details-beam-redeem-kernel-id") + ":"
        }
        SFLabel {
            enabled: isBeamSide || (!isBeamSide && isProofReceived)
            visible: enabled
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
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "redeem transaction ID"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-redeem-tx-id") + ":"
        }
        SFLabel {
            enabled: isBeamSide && isProofReceived
            visible: enabled
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
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "redeem transaction confirmations"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-redeem-tx-conf") + ":"
        }
        SFLabel {
            enabled: isBeamSide && isProofReceived
            visible: enabled
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
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "BEAM refund transaction kernel ID"
            text: qsTrId("swap-details-beam-refund-kernel-id") + ":"
        }
        SFLabel {
            enabled: isBeamSide && !isProofReceived
            visible: enabled
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
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "refund transaction ID"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-refund-tx-id") + ":"
        }
        SFLabel {
            enabled: !isBeamSide && !isProofReceived
            visible: enabled
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
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "refund transaction confirmations"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-refund-tx-conf") + ":"
        }
        SFLabel {
            enabled: !isBeamSide && !isProofReceived
            visible: enabled
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
    }
}
