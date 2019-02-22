import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.11
import "."
import Beam.Wallet 1.0

Dialog {
    property PaymentInfoItem model
    property bool shouldVerify: false
    
    signal textCopied(string text);

    id: dialog
    modal: true

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    
    parent: Overlay.overlay
    padding: 0

    background: Rectangle {
        radius: 10
        color: Style.dark_slate_blue
        anchors.fill: parent
    }

    contentItem: ColumnLayout {
        //anchors.fill: parent
        //anchors.left: parent.left
       // anchors.right: parent.right
        //width: parent.width
        width: 400
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            //Layout.preferredWidth: 400
            Layout.maximumWidth: 400
          //  implicitHeight: 100
            Layout.margins: 30
            rowSpacing: 30
            columnSpacing: 13
            columns: 2

            RowLayout {
                Layout.columnSpan: 2
                SFText {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    leftPadding: 30
                    font.pixelSize: 18
                    font.styleName: "Bold"
                    font.weight: Font.Bold
                    color: Style.white
                    text: shouldVerify ? qsTr("Payment proof verification") : qsTr("Payment proof")
                }

                CustomToolButton {
                    icon.source: "qrc:/assets/icon-cancel.svg"
                    icon.width: 12
                    icon.height: 12
                    ToolTip.text: qsTr("close")
                    onClicked: {
                        dialog.close();
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.row: 1
                Layout.columnSpan: 2
                Layout.alignment: Qt.AlignTop
                visible: shouldVerify
            
                SFText {
                    Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
                    opacity: 0.5
                    font.pixelSize: 14
                    color: Style.white
                    text: qsTr("Paste your payment proof here")
                }
            
                function isInvalidPaymentProof()
                {
                    return model && !model.isValid && paymentProofInput.length > 0;
                }
            
                SFTextInput {
                    id: paymentProofInput
                    Layout.fillWidth: true
                    focus: true
                    activeFocusOnTab: true
                    font.pixelSize: 14
                    wrapMode: TextInput.Wrap
                    color: parent.isInvalidPaymentProof() ? Style.validator_color : Style.white
                    backgroundColor: color
                    text: model ? model.paymentProof : ""
                    Binding {
                        target: model
                        property: "paymentProof"
                        value: paymentProofInput.text.trim()
                    }
                }
                SFText {
                    Layout.fillWidth: true
                    font.pixelSize: 14
                    font.italic: true
                    text: qsTr("Cannot decode a proof, illegal sequence or belongs to a different sender.")
                    color: Style.validator_color
                    visible: parent.isInvalidPaymentProof()
                }
            }

            SFText {
                Layout.alignment: Qt.AlignTop
                Layout.row: 1
                Layout.fillWidth: true
                font.pixelSize: 14
                font.styleName: "Bold"
                font.weight: Font.Bold
                color: Style.white
                text: qsTr("Code:")
                visible: !shouldVerify
            }
            
            SFText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 14
                text: model ? model.paymentProof : ""
                color: Style.disable_text_color
                visible: !shouldVerify
            }
            
            SFText {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 20
                Layout.row: 2
                Layout.columnSpan: 2
                font.pixelSize: 18
                font.styleName: "Bold";
                font.weight: Font.Bold
                color: Style.white
                text: qsTr("Details")
                visible: model? model.isValid : false
            }
            
            SFText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.row: 3
                font.pixelSize: 14
                font.styleName: "Bold"
                font.weight: Font.Bold
                color: Style.white
                text: qsTr("Sender:")
                visible: model? model.isValid : false
            }
            
            SFText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 14
                color: Style.disable_text_color
                text: model ? model.sender : ""
                verticalAlignment: Text.AlignBottom
                visible: model? model.isValid : false
            }
            
            SFText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.row: 4
                font.pixelSize: 14
                font.styleName: "Bold"
                font.weight: Font.Bold
                color: Style.white
                text: qsTr("Receiver:")
                visible: model? model.isValid : false
            }
            
            SFText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 14
                color: Style.disable_text_color
                text: model ? model.receiver : ""
                visible: model? model.isValid : false
            }
            
            SFText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.row: 5
                font.pixelSize: 14
                font.styleName: "Bold"
                font.weight: Font.Bold
                color: Style.white
                text: qsTr("Amount:")
                visible: model? model.isValid : false
            }
            
            SFText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 14
                color: Style.disable_text_color
                text: model ? model.amount + " " + qsTr("BEAM") : ""
                visible: model? model.isValid : false
            }
            
            SFText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.row: 6
                font.pixelSize: 14
                font.styleName: "Bold"
                font.weight: Font.Bold
                color: Style.white
                text: qsTr("Kernel ID:")
                visible: model? model.isValid : false
            }
            
            SFText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 14
                color: Style.disable_text_color
                text: model ? model.kernelID : ""
                visible: model? model.isValid : false
            }

        }
        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.leftMargin: 30
            Layout.rightMargin: 30
            Layout.bottomMargin: 30
            spacing: 20
            visible: model? model.isValid : false

            function copyDetails()
            {
                if (model)
                {
                    textCopied("Sender: " + model.sendingAddress + "\nReceiver: " + model.receivingAddress + "\nAmount: " + model.amount + " BEAM" + "\nKernel ID: " + model.kernelID);
                }
            }

            CustomButton {
                icon.source: "qrc:/assets/icon-copy.svg"
                text: qsTr("copy details")
                visible: !shouldVerify
                onClicked: {
                    parent.copyDetails();
                }
            }
            
            PrimaryButton {
                icon.source: "qrc:/assets/icon-copy-blue.svg"
                text: qsTr("copy code")
                visible: !shouldVerify
                onClicked: {
                    if (model)
                    {
                        textCopied(model.paymentProof);
                    }
                }
            }

            PrimaryButton {
                icon.source: "qrc:/assets/icon-copy-blue.svg"
                text: qsTr("copy details")
                visible: shouldVerify
                onClicked: {
                    if (model)
                    {
                        parent.copyDetails();
                    }
                }
            }
        }
    }
}