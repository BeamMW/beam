import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "controls"

Item
{
    id: root

    anchors.fill: parent

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
            width: 242
            height: 208
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 587
            anchors.top: parent.top

            spacing: 30

            // DefaultButton {
            //     text: "restore wallet from file"
            // }

            // DefaultButton {
            //     text: "restore wallet from blockchain"
            // }

            PrimaryButton {
                text: "create new wallet"

                onClicked: root.state = "create"
            }
        }
    }

    Rectangle 
    {
        id: open

        visible: false

        anchors.fill: parent

        color: Style.marine

        Image {
            fillMode: Image.PreserveAspectCrop
            anchors.fill: parent
            source: "qrc:///assets/bg.png"
        }

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 100
            anchors.top: parent.top
            source: "qrc:///assets/logo.png"
            width: 242
            height: 208
        }

        SFText {
            text: "Enter your password to access the current wallet"
            color: Style.white
            font.pixelSize: 12

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 408
        }

        Column {
            width: 400

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 476

            clip: true

            spacing: 10

            SFText {
                text: "Enter password"
                color: Style.white
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            SFTextInput {
                id: openPassword
                width: parent.width
				focus: true
				activeFocusOnTab: true
                font.pixelSize: 12
                color: Style.white
                echoMode: TextInput.Password
				onAccepted: btnCurrentWallet.clicked()
            }

            Rectangle {
                width: parent.width
                height: 1

                color: Style.white
                opacity: 0.1
            }

            SFText {
                id: openPasswordError
                color: "#ff625c"
                font.pixelSize: 10
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 587
            anchors.top: parent.top

            spacing: 30

            //DefaultButton {
            //    text: "restore wallet from file"
				// activeFocusOnTab: true
            //}

    //         DefaultButton {
    //             text: "restore wallet from blockchain"
				// activeFocusOnTab: true
    //         }

            PrimaryButton {
				id: btnCurrentWallet
                text: "open wallet"
				activeFocusOnTab: true
                onClicked: {
                    if(openPassword.text.length == 0)
                    {
                        openPasswordError.text = "Please, enter password";
                    }
                    else
                    {
                        if(!startViewModel.openWallet(openPassword.text))
                        {
                            openPasswordError.text = "Invalid password or wallet data unreadable.\nRestore wallet.db from latest backup or delete it and reinitialize the wallet.";
                        }
                    }
                }
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
                    text: "Enter secret key"
                    color: Style.white
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                SFTextInput {

                    id:seed

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

                    id:password

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
                    id: confirmPassword
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
                    id: passwordError
                    color: "#ff625c"
                    font.pixelSize: 10
                }
            }
        }

        PrimaryButton {
            text: "create wallet"

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 599

            onClicked: {
                if(seed.text.length == 0)
                {
                    passwordError.text = "Please, enter miner secret";
                }
                else if(password.text.length == 0)
                {
                    passwordError.text = "Please, enter password";
                }
                else if(password.text != confirmPassword.text)
                {
                    passwordError.text = "Passwords do not match";
                }
                else if(!startViewModel.createWallet(seed.text, password.text))
                {
                    passwordError.text = "Error, something went worng, wallet not created :(";
                }
            }
        }

    }

    Component.onCompleted:{
        root.state = startViewModel.walletExists ? "open" : "start"
    }

    states: [
        State {
            name: "start"
        },

        State {
            name: "create"
            PropertyChanges {target: start; visible: false}
            PropertyChanges {target: create; visible: true}
        },

        State {
            name: "open"
            PropertyChanges {target: start; visible: false}
            PropertyChanges {target: open; visible: true}
        }
    ]
}
