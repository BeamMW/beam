import QtQuick 2.11
import QtQuick.Controls 2.4

import QtQuick.Layouts 1.11
import Beam.Wallet 1.0
import "."

RowLayout {
    id: "root"

    property var    swapCoinName

    property var    txId
    property var    fee
    property var    comment
    property var    swapCoinFeeRate
    property var    swapCoinFee
    property var    swapCoinLockTxId
    property var    swapCoinLockTxConfirmations
    property var    beamLockTxKernelId
    property var    failureReason

    property bool   isBeamSide
    property bool   isLockTxProofReceived    // KernelProofHeight != null
    property bool   isRefundTxProofReceived

    // isBeamSide || (!isBeamSide && isLockTxProofReceived)
    property var    beamRedeemTxKernelId
    // isBeamSide && isLockTxProofReceived
    property var    swapCoinRedeemTxId
    property var    swapCoinRedeemTxConfirmations

    // isBeamSide && isRefundTxProofReceived
    property var    beamRefundTxKernelId
    // !isBeamSide && swapCoinRefundTxConfirmations > 0
    property var    swapCoinRefundTxId
    property var    swapCoinRefundTxConfirmations

    property var    stateDetails

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
            //% "Transaction ID"
            text: qsTrId("swap-details-tx-id") + ":"
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.txId
            onCopyText: textCopied(text)
        }
        
        SFText {
            enabled: commentLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Comment"
            text: qsTrId("swap-details-tx-comment") + ":"
        }
        SFLabel {
            id: commentLabel
            enabled: text != ""
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.comment
            onCopyText: textCopied(text)
        }

        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "BEAM Transaction fee"
            text: qsTrId("swap-details-tx-fee") + ":"
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.fee
            onCopyText: textCopied(text)
        }

        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "%1 Transaction fee rate"
            text: qsTrId("swap-details-tx-fee-rate").arg(swapCoinName.toUpperCase()) + ":"
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.swapCoinFeeRate
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: swapCoinFee.enabled
            visible: enabled            
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "%1 Estimated transaction fee"
            text: qsTrId("swap-details-tx-fee-estimated").arg(swapCoinName.toUpperCase()) + ":"
        }
        SFLabel {
            id: swapCoinFee
            enabled: (text != "") && (isBeamSide || swapCoinRefundTxConfirmationsLabel.enabled)
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.swapCoinFee
            onCopyText: textCopied(text)
        }
        
        SFText {
            enabled: swapCoinLockTxIdLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "lock transaction ID"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-lock-tx-id") + ":"
        }
        SFLabel {
            id: swapCoinLockTxIdLabel
            enabled: text != ""
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.swapCoinLockTxId
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: swapCoinLockTxConfirmationsLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "lock transaction confirmations"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-lock-tx-conf") + ":"
        }
        SFLabel {
            id: swapCoinLockTxConfirmationsLabel
            enabled: (text != "") && isBeamSide
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.swapCoinLockTxConfirmations
            onCopyText: textCopied(text)
        }
        
        SFText {
            enabled: beamLockTxKernelIdLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "BEAM lock transaction kernel ID"
            text: qsTrId("swap-details-beam-lock-kernel-id") + ":"
        }
        SFLabel {
            id: beamLockTxKernelIdLabel
            enabled: text != ""
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.beamLockTxKernelId
            onCopyText: textCopied(text)
        }
        
        SFText {
            enabled: beamRedeemTxKernelIdLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "BEAM redeem transaction kernel ID"
            text: qsTrId("swap-details-beam-redeem-kernel-id") + ":"
        }
        SFLabel {
            id: beamRedeemTxKernelIdLabel
            enabled: (text != "") && (isBeamSide || (!isBeamSide && isLockTxProofReceived))
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.beamRedeemTxKernelId
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: swapCoinRedeemTxIdLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "redeem transaction ID"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-redeem-tx-id") + ":"
        }
        SFLabel {
            id: swapCoinRedeemTxIdLabel
            enabled: (text != "") && isBeamSide && isLockTxProofReceived
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.swapCoinRedeemTxId
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: swapCoinRedeemTxConfirmationsLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "redeem transaction confirmations"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-redeem-tx-conf") + ":"
        }
        SFLabel {
            id: swapCoinRedeemTxConfirmationsLabel
            enabled: (text != "") && isBeamSide && isLockTxProofReceived
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.swapCoinRedeemTxConfirmations
            onCopyText: textCopied(text)
        }
        
        SFText {
            enabled: beamRefundTxKernelIdLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "BEAM refund transaction kernel ID"
            text: qsTrId("swap-details-beam-refund-kernel-id") + ":"
        }
        SFLabel {
            id: beamRefundTxKernelIdLabel
            enabled: (text != "") && isBeamSide && isRefundTxProofReceived
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.beamRefundTxKernelId
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: swapCoinRefundTxIdLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "refund transaction ID"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-refund-tx-id") + ":"
        }
        SFLabel {
            id: swapCoinRefundTxIdLabel
            enabled: (text != "") && swapCoinRefundTxConfirmationsLabel.enabled
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.swapCoinRefundTxId
            onCopyText: textCopied(text)
        }

        SFText {
            enabled: swapCoinRefundTxConfirmationsLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "refund transaction confirmations"
            text: swapCoinName.toUpperCase() + ' ' + qsTrId("swap-details-refund-tx-conf") + ":"
        }
        SFLabel {
            id: swapCoinRefundTxConfirmationsLabel
            enabled: (text != "") && (text != "0") && !isBeamSide // exist at least 1 confirmation
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.swapCoinRefundTxConfirmations
            onCopyText: textCopied(text)
        }
        
        RowLayout {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            visible: root.stateDetails != ""
            SvgImage {
                sourceSize: Qt.size(16, 16)
                source:  "qrc:/assets/icon-attention.svg"
            }
            SFLabel {
                Layout.fillWidth: true
                copyMenuEnabled: true
                font.pixelSize: 14
                color: Style.content_main
                wrapMode: Text.Wrap
                elide: Text.ElideMiddle
                text: root.stateDetails
                onCopyText: textCopied(text)
            }
        }

        SFText {
            enabled: failureReasonLabel.enabled
            visible: enabled
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Error"
            text: qsTrId("swap-details-failure-reason") + ":"
        }
        SFLabel {
            id: failureReasonLabel
            enabled: text != ""
            visible: enabled
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            elide: Text.ElideMiddle
            text: root.failureReason
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
