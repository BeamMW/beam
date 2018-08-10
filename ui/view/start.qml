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

        SFText {
            text: "Create new wallet"
            color: Style.white
            font.pixelSize: 36

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 50
        }

        SFText {
            text: "Create password to access your wallet"
            color: Style.white
            font.pixelSize: 12

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 123
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 191
            width: 400

            clip: true

            spacing: 30

            Column {
                width: parent.width

                spacing: 10

                SFText {
                    text: "Enter miner secret"
                    color: Style.white
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                SFTextInput {

                    width: parent.width

                    font.pixelSize: 12
                    color: Style.white
                }

                Rectangle {
                    width: parent.width
                    height: 1

                    color: Style.white
                    opacity: 0.1
                }
            }

            Column {
                width: parent.width

                spacing: 10

                SFText {
                    text: "Enter password"
                    color: Style.white
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                SFTextInput {

                    width: parent.width

                    font.pixelSize: 12
                    color: Style.white
                    echoMode: TextInput.Password
                }

                Rectangle {
                    width: parent.width
                    height: 1

                    color: Style.white
                    opacity: 0.1
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Repeater {
                        model: 3

                        Rectangle {
                            width: 60
                            height: 4
                            radius: 10
                            color: "#f4ce4a"
                        }
                    }

                    Repeater {
                        model: 3

                        Rectangle {
                            width: 60
                            height: 4
                            radius: 10
                            color: Style.marine
                            border.width: 1
                            border.color: Style.dark_slate_blue
                        }
                    }
                }

                SFText {
                    text: "Medium strength password (add  at least one capital letter and one small letter)"
                    color: "#84a5b2"
                    font.pixelSize: 10
                }
            }

            Column {
                width: parent.width

                spacing: 10

                SFText {
                    text: "Confirm password"
                    color: Style.white
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                SFTextInput {

                    width: parent.width

                    font.pixelSize: 12
                    color: Style.white
                    echoMode: TextInput.Password
                }

                Rectangle {
                    width: parent.width
                    height: 1

                    color: Style.white
                    opacity: 0.1
                }

                SFText {
                    text: "Passwords do not match"
                    color: "#ff625c"
                    font.pixelSize: 10
                }
            }
        }

        PrimaryButton {
            label: "Proceed to your wallet"

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 599

            onClicked: root.state = "start"
        }

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
