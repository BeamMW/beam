import QtQuick 2.11
import QtQuick.Controls 2.4

import QtQuick.Layouts 1.11
import Beam.Wallet 1.0
import "."

RowLayout {
    id: "root"
    property TxObject model
    signal textCopied(string text)
    signal showDetails()

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
            font.pixelSize: 14
            color: Style.content_main
            //% "General transaction info"
            text: qsTrId("tx-details-title")
            font.styleName: "Bold"; font.weight: Font.Bold
            Layout.columnSpan: 2
        }
        
        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Sending address:"
            text: qsTrId("tx-details-sending-addr-label")
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            //wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: {
                return model ? model.sendingAddress : "";
            }
            onCopyText: textCopied(text)
        }

        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Receiving address:"
            text: qsTrId("tx-details-receiving-addr-label")
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            //wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: {
                return model ? model.receivingAddress : "";
            }
            onCopyText: textCopied(text)
        }
        
        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Transaction fee:"
            text: qsTrId("tx-details-fee-label")
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            text:{
                return model ? model.fee : "";
            }
            onCopyText: textCopied(text)
        }
        
        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Comment:"
            text: qsTrId("tx-details-comment-label")
        }
        SFLabel {
            Layout.fillWidth: true
            id: commentTx
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            wrapMode: Text.Wrap
            text: {
                return model ? model.comment : "";
            }
            font.styleName: "Italic"
            elide: Text.ElideRight
            onCopyText: textCopied(text)
        }
        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Kernel ID:"
            text: qsTrId("tx-details-kernel-id-label")
        }
        SFLabel {
            Layout.fillWidth: true
            id: kernelID
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            //wrapMode: Text.Wrap
            text: model ? model.kernelID : ""
            font.styleName: "Italic"
            elide: Text.ElideMiddle
            onCopyText: textCopied(text)
        }
        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Transaction ID:"
            text: qsTrId("tx-details-tx-id-label")
        }
        SFLabel {
            Layout.fillWidth: true
            id: transactionID
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            text: model ? model.transactionID : ""
            font.styleName: "Italic"
            elide: Text.ElideMiddle
            onCopyText: textCopied(text)
        }
        SFText {
            Layout.alignment: Qt.AlignTop
            font.pixelSize: 14
            color: Style.content_secondary
            //% "Error: "
            text: qsTrId("tx-details-error-label")
            visible: model ? model.failureReason.length > 0 : false
        }
        SFLabel {
            id: failureReason
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.content_main
            wrapMode: Text.Wrap
            visible: model ? model.failureReason.length > 0 : false
            text: {
                if(model && model.failureReason.length > 0)
                {
                    return model.failureReason;
                }
                return "";
            }
            font.styleName: "Italic"
            elide: Text.ElideRight
            onCopyText: textCopied(text)
        }
    }

    GridLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 3
        Layout.rightMargin: 30
        Layout.topMargin: 30
        Layout.bottomMargin: 30
        columns: 2
        columnSpacing: 44
        rowSpacing: 14
        visible:  model ? !model.income : false
        
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.columnSpan: 2
        }
        SFText {
            font.pixelSize: 14
            color: Style.content_main
            //% "Payment proof"
            text: qsTrId("tx-details-payment-proof-label")
            font.styleName: "Bold"; font.weight: Font.Bold
            Layout.columnSpan: 2
        }
        Row {
            spacing: 20
            CustomButton {
                //% "details"
                text: qsTrId("tx-details-details-button")
                icon.source: "qrc:/assets/icon-details.svg"
                icon.width: 21
                icon.height: 14
                enabled: model ? model.hasPaymentProof && !model.isSelfTx() : false
                onClicked: showDetails();
            }
            CustomButton {
                //% "copy"
                text: qsTrId("tx-details-copy-button")
                icon.source: "qrc:/assets/icon-copy.svg"
                enabled: model ? model.hasPaymentProof && !model.isSelfTx() : false
                onClicked: {
                     if (model) 
                     {
                         var paymentInfo = model.getPaymentInfo();
                         if (paymentInfo.paymentProof.length == 0)
                         {
                            paymentInfo.paymentProofChanged.connect(function() {
                                textCopied(paymentInfo.paymentProof);
                            });
                         }
                         else
                         {
                            textCopied(paymentInfo.paymentProof);
                         }
                     }
                }
            }
        }
    }
}
