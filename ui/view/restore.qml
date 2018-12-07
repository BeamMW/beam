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
    id: root_restore

    property bool isRecoveryMode: false
    property bool isCreating: false
    property var cancelCallback: undefined

    RestoreViewModel {
        id: viewModel 
        onSyncCompleted: {
            if (isRecoveryMode || isCreating)
                root.parent.source = "qrc:/main.qml";
            else
                root_restore.parent.source = "qrc:/main.qml";
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

                CustomButton {
                    visible: isRecoveryMode && isCreating
                    text: qsTr("cancel")
                    icon.source: "qrc:/assets/icon-cancel.svg"
                    onClicked: {
                        viewModel.cancelRestore();
                        cancelCallback();
                    }
                }
            }
            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 67
            }
        }
    }
}