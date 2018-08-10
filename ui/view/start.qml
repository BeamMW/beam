import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "controls"

Item
{
    id: root

    width: 1024
    height: 768

    Rectangle 
    {
        id: start

        anchors.fill: parent

        color: Style.marine

        Image {
            fillMode: Image.PreserveAspectCrop
            anchors.fill: parent
            source: "qrc:///assets/bg.png"
        }

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 180
            anchors.top: parent.top
            source: "qrc:///assets/logo.png"        
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 587
            anchors.top: parent.top

            spacing: 30

            DefaultButton {
                label: "restore wallet from file"
            }

            DefaultButton {
                label: "restore wallet from blockchain"
            }

            PrimaryButton {
                label: "create new wallet"

                onClicked: root.state = "create"
            }        
        }
    }

    Rectangle 
    {
        id: create

        visible: false

        anchors.fill: parent

        color: Style.marine
    }

    states: [
        State {
            name: "start"
        },

        State {
            name: "create"
            PropertyChanges {target: start; visible: false}
            PropertyChanges {target: create; visible: true}
        }
    ]
}
