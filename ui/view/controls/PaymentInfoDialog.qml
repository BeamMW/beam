    import QtQuick 2.11
    import QtQuick.Controls 2.4

    import QtQuick.Layouts 1.11
    import Beam.Wallet 1.0
    import "."

    Dialog {
        property PaymentInfoItem model
        property bool shouldVerify: false
    
        signal textCopied(string text);

        id: dialog
        modal: true

        x: (parent.width - width) / 2
        y: (parent.height - height) / 2

        height: contentItem.implicitHeight
    
        parent: Overlay.overlay
        padding: 0

        closePolicy: Popup.NoAutoClose | Popup.CloseOnEscape

        onClosed: {
            paymentProofInput.text = ""
        }

        onOpened: {
            forceActiveFocus();
            if (shouldVerify)
            {
                paymentProofInput.forceActiveFocus();
            }
        }

        background: Rectangle {
            radius: 10
            color: Style.dark_slate_blue
            anchors.fill: parent
        }

        contentItem: ColumnLayout {
            GridLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 400
                Layout.margins: 30
                rowSpacing: 20
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
                    id: verifyLayout
                    Layout.fillWidth: true
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
            
                    ScrollView {
                        id: scrollView
                        clip: true
                        Layout.fillWidth: true
                        Layout.maximumHeight: 130
                        
                        SFTextArea {
                            id: paymentProofInput
                            focus: true
                            activeFocusOnTab: true
                            font.pixelSize: 14
                            wrapMode: TextInput.Wrap
                            color: verifyLayout.isInvalidPaymentProof() ? Style.validator_color : Style.white
                             text: model ? model.paymentProof : ""
                            Binding {
                                target: model
                                property: "paymentProof"
                                value: paymentProofInput.text.trim()
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: -16
                        color: paymentProofInput.color
                        Layout.preferredHeight: (paymentProofInput.activeFocus || paymentProofInput.hovered) ? 2 : 1
                        opacity: (paymentProofInput.activeFocus || paymentProofInput.hovered) ? 0.3 : 0.1
                    }
                
                    SFText {
                        Layout.fillWidth: true
                        font.pixelSize: 14
                        font.italic: true
                        text: qsTr("Cannot decode a proof, illegal sequence.")
                        color: Style.validator_color
                        visible: verifyLayout.isInvalidPaymentProof()
                    }
                }

                SFText {
                    Layout.alignment: Qt.AlignTop
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
                    Layout.topMargin: 10
                    Layout.columnSpan: 2
                    font.pixelSize: 18
                    font.styleName: "Bold";
                    font.weight: Font.Bold
                    color: Style.white
                    text: qsTr("Details")
                    visible: model? model.isValid : false
                }
            
                SFText {
                    Layout.alignment: Qt.AlignTop
                    font.pixelSize: 14
                    font.styleName: "Bold"
                    font.weight: Font.Bold
                    color: Style.white
                    text: qsTr("Sender:")
                    visible: model? model.isValid : false
                }
            
                SFText {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    wrapMode: Text.Wrap
                    font.pixelSize: 14
                    color: Style.disable_text_color
                    text: model ? model.sender : ""
                    verticalAlignment: Text.AlignBottom
                    visible: model? model.isValid : false
                }
            
                SFText {
                    Layout.alignment: Qt.AlignTop
                    font.pixelSize: 14
                    font.styleName: "Bold"
                    font.weight: Font.Bold
                    color: Style.white
                    text: qsTr("Receiver:")
                    visible: model? model.isValid : false
                }
            
                SFText {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    wrapMode: Text.Wrap
                    font.pixelSize: 14
                    color: Style.disable_text_color
                    text: model ? model.receiver : ""
                    visible: model? model.isValid : false
                }
            
                SFText {
                    Layout.alignment: Qt.AlignTop
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
                    Layout.alignment: Qt.AlignTop
                    font.pixelSize: 14
                    font.styleName: "Bold"
                    font.weight: Font.Bold
                    color: Style.white
                    text: qsTr("Kernel ID:")
                    visible: model? model.isValid : false
                }
            
                SFText {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    wrapMode: Text.Wrap
                    font.pixelSize: 14
                    color: Style.disable_text_color
                    text: model ? model.kernelID : ""
                    visible: model? model.isValid : false
                }
            }
            Row {
                id: buttonsLayout
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
                        textCopied("Sender: " + model.sender + "\nReceiver: " + model.receiver + "\nAmount: " + model.amount + " BEAM" + "\nKernel ID: " + model.kernelID);
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