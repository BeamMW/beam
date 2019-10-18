import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import Beam.Wallet 1.0
import "."
import "../utils.js" as Utils

Control {
    id:            control
    leftPadding:   25
    rightPadding:  25
    topPadding:    20
    bottomPadding: 20

    //
    // Common props
    //
    property alias  title: controlTitle.text
    property string feeRateLabel: ""
    property int    minFeeRate:   0
    property string color: Qt.rgba(Style.content_main.r, Style.content_main.g, Style.content_main.b, 0.5)
    property alias  editElectrum:  useElectrumSwitch.checked
    property bool   canEdit: true

    property bool isConnected: false
    property bool isNodeConnection: false
    property bool isElectrumConnection: false

    //
    // Node props
    //
    property alias  address:      addressInput.address
    property alias  username:     usernameInput.text
    property alias  password:     passwordInput.text
    property alias  feeRate:      feeRateInput.fee
    property bool   canEditNode:  !control.isNodeConnection

    //
    // Electrum props
    //
    property alias addressElectrum:  addressInputElectrum.address
    property alias seedElectrum:     seedInputElectrum.text
    property bool  canEditElectrum:  !control.isElectrumConnection

    //
    // signals
    //
    signal disconnect
    // node
    signal applyNode
    signal clearNode
    signal connectToNode
    // electrum
    signal newSeedElectrum
    signal applyElectrum
    signal clearElectrum
    signal connectToElectrum

    QtObject {
        id: internalCommon
        property int    initialFeeRate
        function restore() {
            feeRate  = initialFeeRate
        }

        function save() {
            initialFeeRate  = feeRate
        }

        function isChanged() {
            return initialFeeRate  !== feeRate
        }
    }

    QtObject {
        id: internalNode
        property string initialAddress
        property string initialUsername
        property string initialPassword        

        function restore() {
            address  = initialAddress
            username = initialUsername
            password = initialPassword
        }

        function save() {
            initialAddress  = address
            initialUsername = username
            initialPassword = password
        }

        function isChanged() {
            return initialAddress  !== address
                || initialUsername !== username
                || initialPassword !== password
        }
    }

    function isSettingsChanged() {
        return  internalCommon.isChanged() || (editElectrum ? internalElectrum.isChanged() : internalNode.isChanged());
    }

    function canApplySettings() {
        return editElectrum ? canApplyElectrum() : canApplyNode();
    }

    function applyChanges() {
        internalCommon.save();
        return editElectrum ? applyChangesElectrum() : applyChangesNode();
    }

    function restoreSettings() {
        internalCommon.restore();
        return editElectrum ? internalElectrum.restore() : internalNode.restore();
    }

    function haveSettings() {
        return editElectrum ? haveElectrumSettings() : haveNodeSettings();
    }

    function clear() {
        internalCommon.save();
        if (editElectrum) {
            control.clearElectrum();
            internalElectrum.save();
        }
        else {
            control.clearNode();
            internalNode.save();
        }
    }

    function canClear() {
        return control.canEdit && (editElectrum ? canClearElectrum() : canClearNode());
    }

    function canDisconnect() {
        return control.canEdit && isConnected && (editElectrum ? canDisconnectElectrum() : canDisconnectNode());
    }

    function canApplyNode() {
        return feeRate >= minFeeRate && password.length && username.length && addressInput.isValid
    }

    function applyChangesNode() {
        internalNode.save()
        control.applyNode()
    }

    function canClearNode() {
        return !isNodeConnection && (internalNode.initialPassword.length || internalNode.initialUsername.length || internalNode.initialAddress.length);
    }

    function canDisconnectNode() {
        return isNodeConnection;
    }

    function haveNodeSettings() {
        return feeRate >= minFeeRate && password.length && username.length && addressInput.isValid;
    }

    //
    // Electrum props
    //

    QtObject {
        id: internalElectrum
        property string initialAddress
        property string initialSeed

        function restore() {
            addressElectrum = initialAddress
            seedElectrum    = initialSeed
        }

        function save() {
            initialAddress  = addressElectrum
            initialSeed     = seedElectrum
        }

        function isChanged() {
            return initialAddress !== addressElectrum
                || initialSeed !== seedElectrum
        }
    }

    function canApplyElectrum() {
        return feeRate >= minFeeRate && seedInputElectrum.acceptableInput && addressInputElectrum.isValid
    }

    function canClearElectrum() {
        return !isElectrumConnection && (internalElectrum.initialAddress.length || internalElectrum.initialSeed.length);
    }

    function canDisconnectElectrum() {
        return isElectrumConnection;
    }

    function applyChangesElectrum() {
        internalElectrum.save();
        control.applyElectrum();
    }

    function haveElectrumSettings() {
        return feeRate >= minFeeRate && seedInputElectrum.acceptableInput && addressInputElectrum.isValid;
    }

    Component.onCompleted: {
        control.editElectrum = control.isElectrumConnection;
        internalCommon.save();
        internalNode.save();
        internalElectrum.save();
    }

    background: Rectangle {
        radius:  10
        color:   Style.background_second
    }

    contentItem: ColumnLayout {
        RowLayout {
            width: parent.width

            // TODO: indicator

            SFText {
                id:                  controlTitle
                color:               control.color
                font.pixelSize:      14
                font.weight:         Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing:  3.11
            }
        }

        ColumnLayout {
            id: editableLayout
            Layout.fillHeight: true
            Layout.fillWidth:  true

            // Node & Electrum switch
            RowLayout {
                Layout.topMargin: 18
                Layout.bottomMargin: 18
                spacing: 10

                SFText {
                    //% "Node"
                    text:  qsTrId("settings-swap-node")
                    color: useElectrumSwitch.checked ? control.color : Style.active
                    font.pixelSize: 14
                }

                CustomSwitch {
                    id:          useElectrumSwitch
                    alwaysGreen: true
                    spacing:     0
                }

                SFText {
                    //% "Electrum"
                    text: qsTrId("general-electrum")
                    color: useElectrumSwitch.checked ? Style.active : control.color
                    font.pixelSize: 14
                }

                Item {
                    Layout.fillWidth: true
                }

                LinkButton {
                    Layout.alignment: Qt.AlignVCenter
                    linkStyle: "<style>a:link {color: '#f9605b'; text-decoration: none;}</style>"
                    //% "Clear"
                    text:       qsTrId("settings-reset")
                    visible:    canClear()
                    onClicked:  clear()
                }

                LinkButton {
                    Layout.alignment: Qt.AlignVCenter
                    linkStyle: "<style>a:link {color: '#f9605b'; text-decoration: none;}</style>"
                    //% "Disconnect"
                    text:       qsTrId("settings-swap-disconnect")
                    visible:    canDisconnect()
                    onClicked:  disconnect()
                }
            }

            GridLayout {
                columns:          2
                columnSpacing:    30
                rowSpacing:       15

                SFText {
                    visible:        !editElectrum
                    font.pixelSize: 14
                    color:          control.color
                    //% "Node Address"
                    text:           qsTrId("settings-node-address")
                }

                IPAddrInput {
                    id:               addressInput
                    visible:          !editElectrum
                    Layout.fillWidth: true
                    color:            Style.content_main
                    underlineVisible: canEditNode
                    readOnly:         !canEditNode
                }

                SFText {
                    visible:        !editElectrum
                    font.pixelSize: 14
                    color:          control.color
                    //% "Username"
                    text:           qsTrId("settings-username")
                }

                SFTextInput {
                    id:               usernameInput
                    visible:          !editElectrum
                    Layout.fillWidth: true
                    font.pixelSize:   14
                    color:            Style.content_main
                    activeFocusOnTab: true
                    underlineVisible: canEditNode
                    readOnly:         !canEditNode
                }

                SFText {
                    visible:        !editElectrum
                    font.pixelSize: 14
                    color:          control.color
                    //% "Password"
                    text:           qsTrId("settings-password")
                }

                SFTextInput {
                    id:               passwordInput
                    visible:          !editElectrum
                    Layout.fillWidth: true
                    font.pixelSize:   14
                    color:            Style.content_main
                    activeFocusOnTab: true
                    echoMode:         TextInput.Password
                    underlineVisible: canEditNode
                    readOnly:         !canEditNode
                }

                // electrum settings
                SFText {
                    visible:        editElectrum
                    font.pixelSize: 14
                    color:          control.color
                    //% "Node Address"
                    text:           qsTrId("settings-node-address")
                }

                IPAddrInput {
                    visible:          editElectrum
                    id:               addressInputElectrum
                    Layout.fillWidth: true
                    color:            Style.content_main
                    ipOnly:           false
                    underlineVisible: canEditElectrum
                    readOnly:         !canEditElectrum
                }

                SFText {
                    visible:        editElectrum
                    font.pixelSize: 14
                    color:          control.color
                    //% "Seed Phrase"
                    text:           qsTrId("settings-swap-seed-phrase")
                }

                ColumnLayout {
                    visible:             editElectrum
                    Layout.fillWidth:    true
                    Layout.topMargin:    3
                    spacing:             0

                    SeedInput {
                        id:               seedInputElectrum
                        Layout.fillWidth: true
                        font.pixelSize:   14
                        activeFocusOnTab: true
                        implicitHeight:   70
                        placeholderText:  text.length > 0 ?
                                          //% "Click to see seed phrase"
                                          qsTrId("settings-see-seed") :
                                          //% "Double click to generate new seed phrase"
                                          qsTrId("settings-new-seed")
                        onNewSeed:        newSeedElectrum()
                    }

                    Item {
                        Layout.fillWidth:   true
                        SFText {
                            font.pixelSize: 12
                            font.italic:    true
                            color:          Style.validator_error
                            //% "Invalid seed phrase"
                            text:           qsTrId("settings-invalid-seed")
                            visible:        seedInputElectrum.text.length && !seedInputElectrum.acceptableInput
                        }
                    }
                }

                // common fee rate
                SFText {
                    font.pixelSize: 14
                    color:          control.color
                    //% "Default fee"
                    text:           qsTrId("settings-fee-rate")
                }

                FeeInput {
                    id:                  feeRateInput
                    Layout.fillWidth:    true
                    fillWidth:           true
                    inputPreferredWidth: -1
                    minFee:              control.minFeeRate
                    feeLabel:            control.feeRateLabel
                    color:               Style.content_main
                    spacing:             0
                    underlineVisible:    canEdit
                    readOnly:            !canEdit
                }
            }

            // alert text if we have active transactions
            SFText {
                visible:               !control.canEdit
                Layout.preferredWidth: 500
                horizontalAlignment:   Text.AlignHCenter
                verticalAlignment:     Text.AlignVCenter
                font.pixelSize:        14
                wrapMode:              Text.WordWrap
                color:                 control.color
                lineHeight:            1.1 
                //% "You cannot disconnect wallet, edit seed phrase or change default fee while you have\ntransactions in progress. Please wait untill transactions are completed\nand try again."
                text:                  qsTrId("settings-progress-na")
            }

            // buttons
            // "cancel" "apply"
            // "connect to node" or "connect to electrum"
            RowLayout {
                visible:          control.canEdit
                Layout.fillWidth: true
                Layout.topMargin: 25
                spacing:          15

                Item {
                    Layout.fillWidth: true
                }

                CustomButton {
                    visible:                !connectButtonId.visible
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  130
                    leftPadding:  25
                    rightPadding: 25
                    text:         qsTrId("general-cancel")
                    icon.source:  "qrc:/assets/icon-cancel-white.svg"
                    enabled:      isSettingsChanged()
                    onClicked:    restoreSettings()
                }

                PrimaryButton {
                    visible:                !connectButtonId.visible
                    leftPadding:            25
                    rightPadding:           25
                    text:                   qsTrId("settings-apply")
                    icon.source:            "qrc:/assets/icon-done.svg"
                    enabled:                isSettingsChanged() && canApplySettings()
                    onClicked:              applyChanges()
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  130
                }

                PrimaryButton {
                    id:                     connectButtonId
                    visible:                !isSettingsChanged() && haveSettings() && (editElectrum ? !isElectrumConnection : !isNodeConnection)
                    leftPadding:            25
                    rightPadding:           25
                    text:                   editElectrum ?
                                            //% "connect to electrum node"
                                            qsTrId("connect to electrum node") :
                                            //% "connect to node"
                                            qsTrId("connect to node");
                    icon.source:            "qrc:/assets/icon-connect.svg"
                    onClicked:              editElectrum ? connectToElectrum() : connectToNode();
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  editElectrum ? 250 : 195
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }
    }
}