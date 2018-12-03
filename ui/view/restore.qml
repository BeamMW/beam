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
    id: root

    anchors.fill: parent

    property bool isRecoveryMode: false
    property bool isCreating: false
    property bool isConnectToRandomNode: false

    RestoreViewModel {
        id: viewModel 
        onSyncCompleted: {
            root.parent.source = "qrc:/main.qml";
        }
    }

    LogoComponent {
        id: logoComponent
    }    

    Rectangle
    {
        anchors.fill: parent
        color: Style.marine

        Image {
            fillMode: Image.PreserveAspectCrop
            anchors.fill: parent
            source: "qrc:/assets/bg.svg"
        }


        ColumnLayout {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.fill: parent
            anchors.topMargin: 50

            Item {
                Layout.fillHeight: true
                Layout.maximumHeight: 140
            }

            Loader { 
                sourceComponent: logoComponent 
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                //Layout.topMargin: 140
            }
            SFText {
                Layout.bottomMargin: 6
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                text: !isCreating ? qsTr("Loading wallet...") : ( isRecoveryMode ? qsTr("Restoring wallet...") : qsTr("Creating wallet..."))
                font.pixelSize: 14
                color: Style.white
            }
            SFText {
                Layout.bottomMargin: 30
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                text: viewModel.progressMessage
                font.pixelSize: 14
                opacity: 0.5
                color: Style.white
            }
            CustomProgressBar {
                Layout.alignment: Qt.AlignHCenter
                id: bar
                value: viewModel.progress
            }

            Item {
                Layout.fillHeight: true
            }

            Row {
                Layout.alignment: Qt.AlignBottom | Qt.AlignHCenter
                Layout.topMargin: 52
                //Layout.bottomMargin: 143

                CustomButton {
                    visible: isRecoveryMode && isCreating
                    text: qsTr("cancel")
                    icon.source: "qrc:/assets/icon-cancel.svg"
                    onClicked: {
                        viewModel.cancelRestore();
                        root.parent.setSource("qrc:/start.qml", {"isRestoreCancelled": true, "isRandomNodeSelected": isConnectToRandomNode});
                    }
                }
            }

            Item {
                Layout.fillHeight: true
                Layout.maximumHeight: 143
            }
        }
        Component.onCompleted: {
            //viewModel.restoreFromBlockchain();
        }
    }
}