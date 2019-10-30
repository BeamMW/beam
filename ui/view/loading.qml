import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "controls"
import "utils.js" as Utils
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
        //% "Try again"
        okButtonText: qsTrId("loading-try-again-button")
        okButtonIconSource: "qrc:/assets/icon-restore-blue.svg"
        //% "Change settings"
        cancelButtonText: qsTrId("general-change-settings")
        cancelButtonIconSource: "qrc:/assets/icon-settings-white.svg"

        property alias titleText: title.text
        property alias messageText: message.text
        property var rejectedCallback: undefined

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
        onRejected: {
            if (rejectedCallback) rejectedCallback();
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
            confirmationDialog.titleText        = title;
            confirmationDialog.messageText      = message;
            confirmationDialog.okButtonVisible  = false;
            confirmationDialog.okButtonEnable   = false;
            confirmationDialog.closePolicy      = Popup.NoAutoClose;
            confirmationDialog.rejectedCallback = isCreating ? cancelCreating : changeNodeSettings;
            confirmationDialog.open();
        }

        onWalletResetCompleted: {
            if(cancelCallback) {
                cancelCallback();
            }
        }
    }

    function cancelCreating() {
        viewModel.resetWallet();
    }

    function changeNodeSettings () {
        rootLoading.parent.setSource("qrc:/start.qml", {"isBadPortMode": true});
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
                Layout.preferredHeight: Utils.getLogoTopGapSize(parent.height)
            }

            LogoComponent {
                Layout.alignment: Qt.AlignHCenter
            }

            Item {
                Layout.fillHeight: true
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Item {
                        Layout.preferredHeight: 30
                    }

                    SFText {
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        Layout.preferredHeight: 16
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
                        Layout.topMargin: 6
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        text: viewModel.progressMessage
                        font.pixelSize: 14
                        opacity: 0.5
                        color: Style.content_main
                    }

                    CustomProgressBar {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 24
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
                        Layout.topMargin: 20
                        SFText {
                            horizontalAlignment: Text.AlignHCenter
                            width: 548
                            height: 30
                            //% "Only the wallet balance (UTXO) can be restored, transaction info and addresses are always private and never kept in the blockchain."
                            text: qsTrId("loading-restore-message-line2")
                            font.pixelSize: 14
                            color: Style.content_secondary
                            wrapMode: Text.Wrap
                            font.italic: true
                            visible: isRecoveryMode
                        }
                    }

                    Row {
                        Layout.alignment: Qt.AlignBottom | Qt.AlignHCenter
                        Layout.topMargin: isRecoveryMode ? 40 : 52

                        CustomButton {
                            visible: (isCreating || isRecoveryMode)
                            enabled: true
                            //% "Cancel"
                            text: qsTrId("general-cancel")
                            icon.source: "qrc:/assets/icon-cancel.svg"
                            onClicked: {
                                this.enabled = false;
                                cancelCreating();
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: 67
                    }

                    SFText {
                        Layout.alignment:    Qt.AlignHCenter
                        font.pixelSize:      12
                        color:               Qt.rgba(255, 255, 255, 0.3)
                        text:                [qsTrId("settings-version"), BeamGlobals.version()].join(' ')
                    }

                    Item {
                        Layout.minimumHeight: 35
                    }
                }
            }
        }

        Timer {
            interval: 1000
            running: true
            repeat: true
            onTriggered: {
                viewModel.recalculateProgress();
            }
        }
    }
}
