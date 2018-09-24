import QtQuick 2.3
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

    StartViewModel { id: viewModel }

    StackView {
        id: startWizzardView
        anchors.fill: parent
        initialItem: start
        focus: true
        onCurrentItemChanged: {
            if (currentItem && currentItem.defaultFocusItem) {
                currentItem.defaultFocusItem.focus = true;
            }
        }
        Component {
            id: start
            Rectangle
            {
                color: Style.marine

                Image {
                    fillMode: Image.PreserveAspectCrop
                    anchors.fill: parent
                    source: "qrc:/assets/bg.svg"
                }

                property Item defaultFocusItem: createNewWallet

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.topMargin: 180
                    anchors.top: parent.top
                    source: "qrc:/assets/start-logo.svg"
                    width: 242
                    height: 170
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.topMargin: 587
                    anchors.top: parent.top

                    spacing: 30
                    // DefaultButton {
                    //     text: qsTr("restore wallet from file")
                    // }

                    // DefaultButton {
                    //     text: qsTr("restore wallet from blockchain")
                    // }

                    PrimaryButton {
                        id: createNewWallet
                        text: qsTr("create new wallet")
                        onClicked: startWizzardView.push(nodeSetup);
                    }
                }
            }
        }

        Component {
            id: create
            Rectangle
            {
                color: Style.marine

                property Item defaultFocusItem: seed

                SFText {
                    text: qsTr("Create new wallet")
                    color: Style.white
                    font.pixelSize: 36

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 50
                }

                SFText {
                    text: qsTr("Create password to access your wallet")
                    color: Style.white
                    font.pixelSize: 18

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 123
                }

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 191
                    width: 400

                    //clip: true

                    spacing: 30

                    Column {
                        width: parent.width

                        spacing: 10

                        SFText {
                            text: qsTr("Enter secret key (seed)")
                            color: Style.white
                            font.pixelSize: 14
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        SFTextInput {

                            id:seed

                            width: parent.width

                            font.pixelSize: 14
                            color: Style.white
                            echoMode: TextInput.Password
                            onTextChanged: if (seed.text.length > 0) passwordError.text = ""
                        }
                    }

                    Column {
                        width: parent.width

                        spacing: 10

                        SFText {
                            text: qsTr("Enter password")
                            color: Style.white
                            font.pixelSize: 14
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        SFTextInput {

                            id:password

                            width: parent.width

                            font.pixelSize: 14
                            color: Style.white
                            echoMode: TextInput.Password
                            onTextChanged: if (password.text.length > 0) passwordError.text = ""
                        }

                        RowLayout{
                            id: strengthChecker

                            property var strengthTests: 
                            [
                                {exp: new RegExp("(?=.{1,})")                                                               , color: "#ff625c", msg: "Very weak password"},
                                {exp: new RegExp("((?=.{6,})(?=.*[0-9]))|((?=.{6,})(?=.*[A-Z]))|((?=.{6,})(?=.*[a-z]))")    , color: "#ff625c", msg: "Weak password"},
                                {exp: new RegExp("((?=.{6,})(?=.*[A-Z])(?=.*[a-z]))|((?=.{6,})(?=.*[0-9])(?=.*[a-z]))")     , color: "#f4ce4a", msg: "Medium strength password"},
                                {exp: new RegExp("(?=.{8,})(?=.*[0-9])(?=.*[A-Z])(?=.*[a-z])")                              , color: "#f4ce4a", msg: "Medium strength password"},
                                {exp: new RegExp("(?=.{10,})(?=.*[0-9])(?=.*[A-Z])(?=.*[a-z])")                             , color: "#00f6d2", msg: "Strong password"},
                                {exp: new RegExp("(?=.{10,})(?=.*[!@#\$%\^&\*])(?=.*[0-9])(?=.*[A-Z])(?=.*[a-z])")          , color: "#00f6d2", msg: "Very strong password"},
                            ]

                            function passwordStrength(pass)
                            {
                                for(var i = strengthTests.length - 1; i >= 0; i--)
                                    if(strengthTests[i].exp.test(pass))
                                        return i + 1;
                               
                                return 0;
                            }

                            property var strength: passwordStrength(password.text)

                            width: parent.width

                            spacing: 8

                            Repeater{
                                model: parent.strengthTests.length

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 4
                                    border.width: index < parent.strength ? 0 : 1
                                    border.color: Style.dark_slate_blue
                                    radius: 10
                                    color: index < parent.strength ? parent.strengthTests[parent.strength-1].color : Style.marine
                                }
                            }
                        }

                        SFText {
                            text: strengthChecker.strength > 0 ? strengthChecker.strengthTests[strengthChecker.strength-1].msg : ""
                            color: "#84a5b2"
                            font.pixelSize: 14
                        }
                    }

                    Column {
                        width: parent.width
                        anchors.bottomMargin: 6
                        spacing: 10

                        SFText {
                            text: qsTr("Confirm password")
                            color: Style.white
                            font.pixelSize: 14
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        SFTextInput {
                            id: confirmPassword
                            width: parent.width

                            font.pixelSize: 14
                            color: Style.white
                            echoMode: TextInput.Password
                            onTextChanged: if (confirmPassword.text.length > 0) passwordError.text = ""
                        }

                        SFText {
                            id: passwordError
                            color: Style.validator_color
                            font.pixelSize: 14
                        }
                    }
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 599
                    spacing: 30
                    CustomButton {
                        text: qsTr("back");
                        onClicked: startWizzardView.pop();
                    }
                    PrimaryButton {
                        text: qsTr("create wallet")
                        onClicked: {
                            if(seed.text.length == 0)
                            {
                                 passwordError.text = qsTr("Please, enter secret key (seed)");
                            }
                            else if(password.text.length == 0)
                            {
                                passwordError.text = qsTr("Please, enter password");
                            }
                            else if(password.text != confirmPassword.text)
                            {
                                passwordError.text = qsTr("Passwords do not match");
                            }
                            else if(!viewModel.createWallet(seed.text, password.text))
                            {
                                passwordError.text = qsTr("Error, something went worng, wallet not created :(");
                            }
                            else
                            {
                                root.parent.source = "qrc:/main.qml";
                            }
                        }
                    }
                }
            }
        }

        Component {
            id: nodeSetup

            Rectangle
            {
                color: Style.marine

                property Item defaultFocusItem: localNodeButton

                SFText {
                    text: qsTr("Setup node")
                    color: Style.white
                    font.pixelSize: 36

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 50
                }

                SFText {
                    text: qsTr("Please choose your node preferences")
                    color: Style.white
                    font.pixelSize: 18

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
                    ButtonGroup {
                        id: nodePreferencesGroup
                    }
                    CustomRadioButton {
                        id: localNodeButton
                        text: qsTr("Run local testnet node")
                        ButtonGroup.group: nodePreferencesGroup
                        font.pixelSize: 14
                    }
                    Column {
                        id: localNodePanel
                        visible: localNodeButton.checked
                        width: parent.width

                        spacing: 10

                        SFText {
                            text: qsTr("Enter port to listen")
                            color: Style.white
                            font.pixelSize: 14
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        SFTextInput {
                            id:portInput
                            width: parent.width

                            font.pixelSize: 14
                            color: Style.white
                            text: "10000"
                            onTextChanged: if (portInput.text.length > 0) portError.text = ""
                        }
                        SFText {
                            id: portError
                            color: Style.validator_color
                            font.pixelSize: 14
                        }

                        SFText {
                            text: qsTr("Enter mining threads (0 - no mining)")
                            color: Style.white
                            font.pixelSize: 14
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        FeeSlider {
                            id: miningInput
                            precision: 0
                            showTicks: true
                            width: parent.width
                            value: 0
                            to: {viewModel.coreAmount()}
                            stepSize: 1
                        }
                    }
                    CustomRadioButton {
                        id: remoteNodeButton
                        text: qsTr("Connect to remote node")
                        ButtonGroup.group: nodePreferencesGroup
                        font.pixelSize: 14
                    }
                    Column {
                        id: remoteNodePanel
                        visible: remoteNodeButton.checked
                        width: parent.width

                        spacing: 10

                        SFText {
                            text: qsTr("Enter remote node address")
                            color: Style.white
                            font.pixelSize: 14
                            font.styleName: "Bold"; font.weight: Font.Bold
                        }

                        SFTextInput {
                            id:remoteNodeAddrInput
                            width: parent.width
                            font.pixelSize: 14
                            color: Style.white
                            text: "127.0.0.1:10000"
                            validator: RegExpValidator { regExp: /^(\s|\x180E)*(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(\s|\x180E)*$/ }
                            onTextChanged: if (remoteNodeAddrInput.text.length > 0) remoteNodeAddrError.text = ""
                        }
                        SFText {
                            id: remoteNodeAddrError
                            color: Style.validator_color
                            font.pixelSize: 14
                        }
                    }
                    CustomRadioButton {
                        id: testnetNodeButton
                        text: qsTr("Connect to randomly selected node for testnet")
                        ButtonGroup.group: nodePreferencesGroup
                        font.pixelSize: 14
                    }
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 599
                    spacing: 30

                    CustomButton {
                        text: qsTr("back");
                        onClicked: startWizzardView.pop();
                    }

                    PrimaryButton {
                        text: qsTr("next");
                        enabled: nodePreferencesGroup.checkState != Qt.Unchecked
                        onClicked:{
                            if (localNodeButton.checked) {
                                var portEmpty = portInput.text.trim().length === 0;
                                if (portEmpty) {
                                    portError.text = qsTr("Please, specify port to listen ");
                                }
                                if (!portEmpty) {
                                    viewModel.setupLocalNode(parseInt(portInput.text), parseInt(miningInput.value));
                                }
                                else {
                                    return;
                                }
                            }
                            else if (remoteNodeButton.checked) {
                                if (remoteNodeAddrInput.text.trim().length === 0) {
                                    remoteNodeAddrError.text = qsTr("Please, specify address of the remote node");
                                    return;
                                }
                                viewModel.setupRemoteNode(remoteNodeAddrInput.text.trim());
                            }
                            else if (testnetNodeButton.checked) {
                                viewModel.setupTestnetNode();
                            }
                            startWizzardView.push(create);
                        }
                    }
                }
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
            source: "qrc:/assets/bg.svg"
        }

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 100
            anchors.top: parent.top
            source: "qrc:/assets/start-logo.svg"
            width: 242
            height: 170
        }

        SFText {
            text: qsTr("Enter your password to access the current wallet")
            color: Style.white
            font.pixelSize: 14

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 408
        }

        Column {
            width: 400

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 476

            //clip: true

            spacing: 10

            SFText {
                text: qsTr("Enter password")
                color: Style.white
                font.pixelSize: 14
                font.styleName: "Bold"; font.weight: Font.Bold
            }

            SFTextInput {
                id: openPassword
                width: parent.width
                focus: true
                activeFocusOnTab: true
                font.pixelSize: 14
                color: Style.white
                echoMode: TextInput.Password
                onAccepted: btnCurrentWallet.clicked()
                onTextChanged: if (openPassword.text.length > 0) openPasswordError.text = ""

            }

            SFText {
                id: openPasswordError
                color: Style.validator_color
                font.pixelSize: 14
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 587
            anchors.top: parent.top

            spacing: 30

            //DefaultButton {
            //    text: qsTr("restore wallet from file")
                // activeFocusOnTab: true
            //}

    //         DefaultButton {
    //             text: qsTr("restore wallet from blockchain")
                // activeFocusOnTab: true
    //         }

            PrimaryButton {
                id: btnCurrentWallet
                text: qsTr("open wallet")
                activeFocusOnTab: true
                onClicked: {
                    if(openPassword.text.length == 0)
                    {
                        openPasswordError.text = qsTr("Please, enter password");
                    }
                    else
                    {
                        if(!viewModel.openWallet(openPassword.text))
                        {
                            openPasswordError.text = qsTr("Invalid password or wallet data unreadable.\nRestore wallet.db from latest backup or delete it and reinitialize the wallet.");
                        }
                        else
                        {
                             root.parent.source = "qrc:/main.qml";
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted:{
        root.state = viewModel.walletExists ? "open" : "start"
    }

    states: [
        State {
            name: "start"
        },

        State {
            name: "open"
            PropertyChanges {target: startWizzardView; visible: false}
            PropertyChanges {target: open; visible: true}
            StateChangeScript {
                script: openPassword.forceActiveFocus(Qt.TabFocusReason);
            }
        }
    ]
}

