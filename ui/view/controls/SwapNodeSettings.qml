import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import Beam.Wallet 1.0
import "."
import "../utils.js" as Utils

Control {
    id:            thisControl
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
    property alias  useElectrum:  useELSwitch.checked

    //
    // Node props
    //
    property alias  address:      addressInput.address
    property alias  username:     usernameInput.text
    property alias  password:     passwordInput.text
    property int    feeRate:      0

    onAddressChanged:  internalNode.changed = true
    onUsernameChanged: internalNode.changed = true
    onPasswordChanged: internalNode.changed = true
    onFeeRateChanged:  internalNode.changed = true

    signal applyNode
    signal switchOffNode

    QtObject {
        id: internalNode
        property string initialAddress
        property string initialUsername
        property string initialPassword
        property int    initialFeeRate
        property bool   changed: false

        function restore() {
            address  = initialAddress
            username = initialUsername
            password = initialPassword
            feeRate  = initialFeeRate
            feeRateInput.fee = initialFeeRate
            changed  = false
        }

        function save() {
            initialAddress  = address
            initialUsername = username
            initialFeeRate  = feeRate
            initialPassword = password
            changed = false
        }
    }

    function canApplyNode() {
        return internalNode.changed && feeRate >= minFeeRate && password.length && username.length && addressInput.isValid
    }

    function applyChangesNode() {
        internalNode.save()
        thisControl.applyNode()
    }

    function canSwitchOffNode () {
        return internalNode.initialAddress.length  != 0 ||
               internalNode.initialUsername.length != 0 ||
               internalNode.initialPassword.length != 0
    }

    //
    // Electrum props
    //
    property alias  addressEL:    addressInputEL.address
    property alias  seedEL:       seedInputEL.text
    property int    feeRateEL:    0

    onAddressELChanged:  internalEL.changed = true
    onSeedELChanged:     internalEL.changed = true
    onFeeRateELChanged:  internalEL.changed = true

    signal newSeedEL
    signal applyEL
    signal switchOffEL

    QtObject {
        id: internalEL
        property string initialAddress
        property string initialSeed
        property int    initialFeeRate
        property bool   changed: false

        function restore() {
            addressEL = initialAddress
            seedEL    = initialSeed
            feeRateEL = initialFeeRate
            feeRateInputEL.fee = initialFeeRate
            changed  = false
        }

        function save() {
            initialAddress  = addressEL
            initialSeed     = seedEL
            initialFeeRate  = feeRateEL
            changed = false
        }
    }

    function canApplyEL() {
        return internalEL.changed && feeRateEL >= minFeeRate && seedInputEL.acceptableInput && addressInputEL.isValid
    }

    function applyChangesEL() {
        internalEL.save()
        thisControl.applyEL()
    }

    function canSwitchOffEL () {
        return internalEL.initialAddress.length  != 0 ||
               internalEL.initialSeed.length != 0
    }

    Component.onCompleted: {
        internalNode.save()
        internalEL.save()
    }

    background: Rectangle {
        radius:  10
        color:   Style.background_second
    }

    contentItem: ColumnLayout {
        RowLayout {
            width: parent.width

            SFText {
                id:                  controlTitle
                color:               thisControl.color
                font.pixelSize:      14
                font.weight:         Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing:  3.11
            }

            Item {
                Layout.fillWidth: true
            }

            LinkButton {
                Layout.alignment: Qt.AlignVCenter
                //% "Disconnect"
                text:       qsTrId("settings-reset")
                visible:    !useElectrum && canSwitchOffNode()
                onClicked:  {
                    thisControl.switchOffNode()
                    internalNode.save()
                }
            }

            LinkButton {
                Layout.alignment: Qt.AlignVCenter
                //% "Disconnect"
                text:       qsTrId("settings-reset")
                visible:    useElectrum && canSwitchOffEL()
                onClicked:  {
                    thisControl.switchOffEL()
                    internalEL.save()
                }
            }
        }

        RowLayout {
            Layout.topMargin: 18
            Layout.bottomMargin: 18
            spacing: 10

            SFText {
                text:  "Node"
                color: useELSwitch.checked ? thisControl.color : Style.active
                font.pixelSize: 14
            }

            CustomSwitch {
                id:          useELSwitch
                alwaysGreen: true
                spacing:     0
            }

            SFText {
                //% "Electrum"
                text: qsTrId("general-electrum")
                color: useELSwitch.checked ? Style.active : thisControl.color
                font.pixelSize: 14
            }
        }

        ColumnLayout {
            visible: !useElectrum
            id:      nodeLayout

            //
            // My Address
            //
            GridLayout{
                columns:          2
                columnSpacing:    30
                rowSpacing:       15

                SFText {
                    font.pixelSize: 14
                    color:          Style.content_main
                    //% "Node Address"
                    text:           qsTrId("settings-node-address")
                }

                IPAddrInput {
                    id:               addressInput
                    Layout.fillWidth: true
                    color:            Style.content_secondary
                }

                SFText {
                    font.pixelSize: 14
                    color:          Style.content_main
                    //% "Username"
                    text:           qsTrId("settings-username")
                }

                SFTextInput {
                    id:               usernameInput
                    Layout.fillWidth: true
                    font.pixelSize:   14
                    color:            Style.content_secondary
                    activeFocusOnTab: true
                }

                SFText {
                    font.pixelSize: 14
                    color:          Style.content_main
                    //% "Password"
                    text:           qsTrId("settings-password")
                }

                SFTextInput {
                    id:               passwordInput
                    Layout.fillWidth: true
                    font.pixelSize:   14
                    color:            Style.content_secondary
                    activeFocusOnTab: true
                    echoMode:         TextInput.Password
                }

                SFText {
                    font.pixelSize: 14
                    color: Style.content_main
                    //% "Default fee"
                    text:  qsTrId("settings-fee-rate")
                }

                FeeInput {
                    id:               feeRateInput
                    Layout.fillWidth: true
                    fillWidth:        true
                    fee:              thisControl.feeRate
                    minFee:           thisControl.minFeeRate
                    feeLabel:         thisControl.feeRateLabel
                    color:            Style.content_secondary
                    spacing:          0

                    Connections {
                        target: thisControl
                        onFeeRateChanged: feeRateInput.fee = thisControl.feeRate
                    }
                }

                Binding {
                    target:   thisControl
                    property: "feeRate"
                    value:    feeRateInput.fee
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 25
                spacing: 15

                Item {
                    Layout.fillWidth: true
                }

                CustomButton {
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  130
                    leftPadding:  25
                    rightPadding: 25
                    text:         qsTrId("general-cancel")
                    icon.source:  "qrc:/assets/icon-cancel-white.svg"
                    enabled:      internalNode.changed
                    onClicked:    internalNode.restore()
                }

                PrimaryButton {
                    leftPadding:  25
                    rightPadding: 25
                    text:         qsTrId("settings-apply")
                    icon.source:  "qrc:/assets/icon-done.svg"
                    enabled:      canApplyNode()
                    onClicked:    applyChangesNode()
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  130
                }
            }
        }

        ColumnLayout {
            visible: useElectrum
            id:      electrumLayout

            //
            // My Address
            //
            GridLayout{
                columns:          2
                columnSpacing:    30
                rowSpacing:       12

                SFText {
                    font.pixelSize: 14
                    color:          Style.content_main
                    //% "Node Address"
                    text:           qsTrId("settings-node-address")
                }

                IPAddrInput {
                    id:               addressInputEL
                    Layout.fillWidth: true
                    color:            Style.content_secondary
                    ipOnly:           false
                }

                SFText {
                    font.pixelSize: 14
                    color:          Style.content_main
                    ////% "Username"
                    text:           "Seed Phrase"
                }

                ColumnLayout {
                    Layout.fillWidth:    true
                    Layout.topMargin:    5
                    Layout.bottomMargin: 6
                    spacing:             0

                    SeedInput {
                        id:                  seedInputEL
                        Layout.fillWidth:    true
                        font.pixelSize:      14
                        activeFocusOnTab:    true
                        implicitHeight:      70
                        placeholderText:     text.length > 0 ?
                                                //% "Click to see seed phrase"
                                                qsTrId("settings-see-seed") :
                                                //% "Double click to generate new seed phrase"
                                                qsTrId("settings-new-seed")
                        onNewSeed: newSeedEL()
                    }

                    Item {
                        Layout.fillWidth: true
                        SFText {
                            font.pixelSize: 12
                            font.italic:    true
                            color:          Style.validator_error
                            //% "Invalid seed phrase"
                            text:           qsTrId("settings-invalid-seed")
                            visible:        seedInputEL.text.length && !seedInputEL.acceptableInput
                        }
                    }
                }

                SFText {
                    font.pixelSize: 14
                    color: Style.content_main
                    //% "Default fee"
                    text:  qsTrId("settings-fee-rate")
                }

                FeeInput {
                    id:               feeRateInputEL
                    Layout.fillWidth: true
                    fillWidth:        true
                    fee:              thisControl.feeRateEL
                    minFee:           thisControl.minFeeRate
                    feeLabel:         thisControl.feeRateLabel
                    color:            Style.content_secondary
                    spacing:          0

                    Connections {
                        target: thisControl
                        onFeeRateChanged: feeRateInputEL.fee = thisControl.feeRateEL
                    }
                }

                Binding {
                    target:   thisControl
                    property: "feeRateEL"
                    value:    feeRateInputEL.fee
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 25
                spacing: 15

                Item {
                    Layout.fillWidth: true
                }

                CustomButton {
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  130
                    leftPadding:  25
                    rightPadding: 25
                    text:         qsTrId("general-cancel")
                    icon.source:  "qrc:/assets/icon-cancel-white.svg"
                    enabled:      internalEL.changed
                    onClicked:    internalEL.restore()
                }

                PrimaryButton {
                    leftPadding:  25
                    rightPadding: 25
                    text:         qsTrId("settings-apply")
                    icon.source:  "qrc:/assets/icon-done.svg"
                    enabled:      canApplyEL()
                    onClicked:    applyChangesEL()
                    Layout.preferredHeight: 38
                    Layout.preferredWidth:  130
                }
            }
        }
    }
}