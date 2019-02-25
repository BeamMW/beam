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
        Layout.leftMargin: 30
        Layout.topMargin: 30
        Layout.bottomMargin: 30
        columnSpacing: 44
        rowSpacing: 14

        SFText {
            font.pixelSize: 14
            color: Style.white
            text: qsTr("General transaction info")
            font.styleName: "Bold"; font.weight: Font.Bold
            Layout.columnSpan: 2
        }
        
        SFText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.row: 1
            font.pixelSize: 14
            color: Style.bluey_grey
            text: qsTr("Sending address:")
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.white
            //wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: {
                return model ? model.sendingAddress : "";
            }
            onCopyText: textCopied(text)
        }
        
        SFText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.row: 2
            font.pixelSize: 14
            color: Style.bluey_grey
            text: qsTr("Receiving address:")
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.white
            //wrapMode: Text.Wrap
            elide: Text.ElideMiddle
            text: {
                return model ? model.receivingAddress : "";
            }
            onCopyText: textCopied(text)
        }
        
        SFText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.row: 3
            font.pixelSize: 14
            color: Style.bluey_grey
            text: qsTr("Transaction fee:")
        }
        SFLabel {
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.white
            text:{
                return model ? model.fee : "";
            }
            onCopyText: textCopied(text)
        }
        
        SFText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.row: 4
            font.pixelSize: 14
            color: Style.bluey_grey
            text: qsTr("Comment:")
        }
        SFLabel {
            Layout.fillWidth: true
            id: commentTx
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.white
            //wrapMode: Text.Wrap
            text: {
                return model ? model.comment : "";
            }
            font.styleName: "Italic"
            elide: Text.ElideRight
            onCopyText: textCopied(text)
        }
        SFText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.row: 5
            font.pixelSize: 14
            color: Style.bluey_grey
            text: qsTr("Kernel ID:")
        }
        SFLabel {
            Layout.fillWidth: true
            id: kernelID
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.white
            //wrapMode: Text.Wrap
            text: model ? model.kernelID : ""
            font.styleName: "Italic"
            elide: Text.ElideMiddle
            onCopyText: textCopied(text)
        }
        SFText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.row: 6
            font.pixelSize: 14
            color: Style.bluey_grey
            text: qsTr("Error: ")
            visible: model ? model.failureReason.length > 0 : false
        }
        SFLabel {
            id: failureReason
            Layout.fillWidth: true
            copyMenuEnabled: true
            font.pixelSize: 14
            color: Style.white
            //wrapMode: Text.Wrap
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
        Layout.rightMargin: 30
        Layout.topMargin: 30
        Layout.bottomMargin: 30
        columns: 2
        columnSpacing: 44
        rowSpacing: 14
        visible: model ? !model.income : false
        
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.columnSpan: 2
        }
        SFText {
            font.pixelSize: 14
            color: Style.white
            text: qsTr("Payment proof")
            font.styleName: "Bold"; font.weight: Font.Bold
            Layout.columnSpan: 2
        }
        Row {
            spacing: 20
            CustomButton {
                text: qsTr("details")
                icon.source: "qrc:/assets/icon-details.svg"
                icon.width: 21
                icon.height: 14
                onClicked: showDetails();
            }
            CustomButton {
                text: qsTr("copy")
                icon.source: "qrc:/assets/icon-copy.svg"
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
