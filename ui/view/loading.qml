import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "controls"
import Beam.Wallet 1.0
import QtQuick.Layouts 1.3

Item
{
    id: rootLoading

    property bool isRecoveryMode: false
    property alias isCreating: viewModel.isCreating
    property var cancelCallback: undefined

    ConfirmationDialog {
        id: confirmationDialog
        okButtonColor: Style.active
        //% "Сhange settings"
        okButtonText: qsTrId("loading-change-settings-button")
        okButtonIconSource: "qrc:/assets/icon-settings-blue.svg"
        cancelButtonIconSource: "qrc:/assets/icon-cancel-white.svg"

        property alias titleText: title.text
        property alias messageText: message.text

        contentItem: Item {
            id: confirmationContent
            ColumnLayout {
                anchors.fill: parent
                spacing: 18

                SFText {
                    id: title
                    Layout.alignment: Qt.AlignHCenter
                    Layout.minimumHeight: 21
                    Layout.leftMargin: 68
                    Layout.rightMargin: 68
                    Layout.topMargin: 30
                    font.pixelSize: 18
                    font.styleName: "Bold";
                    font.weight: Font.Bold
                    color: Style.content_main
                }

                SFText {
                    id: message
                    Layout.alignment: Qt.AlignHCenter
                    Layout.minimumHeight: 18
                    Layout.leftMargin: 60
                    Layout.rightMargin: 60
                    Layout.bottomMargin: 30
                    font.pixelSize: 14
                    color: Style.content_main
                }
            }
        }
        onAccepted: {
            cancelCreating();
        }
    }

    LoadingViewModel {
        id: viewModel 
        onSyncCompleted: {
            if (isRecoveryMode || isCreating)
                root.parent.source = "qrc:/main.qml";
            else
                rootLoading.parent.source = "qrc:/main.qml";
        }

        onWalletError: {
            if (isCreating) {
                confirmationDialog.titleText = title;
                confirmationDialog.messageText = message;
                confirmationDialog.open();
            }
        }
    }

    function cancelCreating() {
        viewModel.resetWallet();
        cancelCallback();
    }

    LogoComponent {
        id: logoComponent
    }    

    Rectangle
    {
        anchors.fill: parent
        color: Style.background_main

        Image {
            fillMode: Image.PreserveAspectCrop
            anchors.fill: parent
            source: "qrc:/assets/bg.svg"
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Item {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.minimumHeight: 70
                Layout.maximumHeight: 280
            }

            Loader { 
                sourceComponent: logoComponent 
                Layout.alignment: Qt.AlignHCenter
                Layout.fillHeight: true
                Layout.minimumHeight: 200//187
                Layout.maximumHeight: 269
            }
            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 30
                Layout.maximumHeight: 89

            }
            Item {
                Layout.preferredHeight: 186 
            }

            Item {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.minimumHeight: 67
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 70
                Layout.maximumHeight: 280
            }

            Item { 
                Layout.fillHeight: true
                Layout.minimumHeight: 200//187
                Layout.maximumHeight: 269
            }

            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 30
                Layout.maximumHeight: 89
            }
            SFText {
                  Layout.bottomMargin: 6
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                text: !isCreating ? 
                        //% "Loading wallet..."
                        qsTrId("loading-loading") :
                        ( isRecoveryMode ?
                            //% "Restoring wallet..."
                            qsTrId("loading-restoring") :
                            //% "Creating wallet..."
                            qsTrId("loading-creating"))
                font.pixelSize: 14
                color: Style.content_main
            }
            SFText {
                Layout.bottomMargin: 30
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                text: viewModel.progressMessage
                font.pixelSize: 14
                opacity: 0.5
                color: Style.content_main
            }
            CustomProgressBar {
                Layout.alignment: Qt.AlignHCenter
                id: bar
                value: viewModel.progress
            }

            SFText {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 30
                width: 584
                //% "Please wait for synchronization and do not close or minimize the application."
                text: qsTrId("loading-restore-message-line1")
                font.pixelSize: 14
                color: Style.content_secondary
                font.italic: true
                visible: isRecoveryMode
            }

            Row {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 25
                SFText {
                    horizontalAlignment: Qt.AlignHCenter
                    width: 548
                    height: 30
                    //% "Only the wallet balance (UTXO) can be restored, transaction info and addresses are always private and never kept in the blockchain."
                    text: qsTrId("loading-restore-message-line2")
                    font.pixelSize: 14
                    color: Style.content_secondary
                    visible: isRecoveryMode
                    wrapMode: Text.Wrap
                    font.italic: true
                }
            }

            Item {
                Layout.fillHeight: true
            }

            Row {
                Layout.alignment: Qt.AlignBottom | Qt.AlignHCenter
                Layout.topMargin: isRecoveryMode ? 40 : 52

                CustomButton {
                    visible: isCreating
                    //% "Cancel"
                    text: qsTrId("general-cancel")
                    icon.source: "qrc:/assets/icon-cancel.svg"
                    onClicked: {
                        cancelCreating();
                    }
                }
            }
            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: isRecoveryMode ? 40 : 67
            }
        }
    }
}