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

    RestoreViewModel { id: viewModel }

    Component
    {
        id: logoComponent

        Column
        {
            spacing: 20

            Image {
                anchors.horizontalCenter: parent.horizontalCenter

                source: "qrc:/assets/start-logo.svg"
                width: 242
                height: 170
            }

            SFText {
                anchors.horizontalCenter: parent.horizontalCenter

                text: qsTr("BEAM")
                color: "#25c1ff"
                font.pixelSize: 32
                font.styleName: "Bold"; font.weight: Font.Bold
                font.letterSpacing: 20
            }

            SFText {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 20

                text: qsTr("Scalable confidential cryptocurrency")

                color: "#25c1ff"
                font.pixelSize: 18
                font.styleName: "Bold"; font.weight: Font.Bold
            }
        }
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

        property Item defaultFocusItem: createNewWallet

        Loader { 
            sourceComponent: logoComponent 

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 140
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 143
            anchors.bottom: parent.bottom

            CustomButton {
                text: qsTr("cancel")
                icon.source: "qrc:/assets/icon-cancel.svg"
                onClicked: {
                            
                }
            }
        }
        Component.onCompleted: {
            viewModel.restoreFromBlockchain();
        }
    }
}