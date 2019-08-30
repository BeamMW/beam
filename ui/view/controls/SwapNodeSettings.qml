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
    leftPadding:   28
    rightPadding:  28
    topPadding:    23
    bottomPadding: 23

    property alias  title:        controlTitle.text
    property alias  address:      addressInput.address
    property alias  username:     usernameInput.text
    property alias  password:     passwordInput.text
    property string feeRateLabel: ""
    property int    feeRate:      0
    property int    minFeeRate:   0

    signal apply
    signal switchOff

    QtObject {
        id: internal
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

    Component.onCompleted: {
        internal.save()
    }

    function canApply() {
        return internal.changed && feeRate >= minFeeRate && password.length && username.length && addressInput.isValid
    }

    function cancelChanges() {
        internal.restore()
    }

    function applyChanges() {
        internal.save()
        thisControl.apply()
    }

    function canSwitchOff () {
        return internal.initialAddress.length  != 0 ||
               internal.initialUsername.length != 0 ||
               internal.initialPassword.length != 0
    }

    onTitleChanged:    internal.changed = true
    onAddressChanged:  internal.changed = true
    onUsernameChanged: internal.changed = true
    onPasswordChanged: internal.changed = true
    onFeeRateChanged:  internal.changed = true

    background: Rectangle {
        radius:  10
        color:   Style.background_second
    }

    contentItem: ColumnLayout {
        RowLayout {
            width: parent.width

            SFText {
                id:             controlTitle
                color:          Style.content_main
                font.pixelSize: 18
                font.weight:    Font.Bold
            }

           Item {
                Layout.fillWidth: true
           }

            LinkButton {
                Layout.alignment: Qt.AlignVCenter
                //% "Switch off"
                text:       qsTrId("settings-reset")
                visible:    canSwitchOff()
                onClicked:  {
                    thisControl.switchOff()
                    internal.save()
                }
            }
        }

        //
        // My Address
        //
        GridLayout{
            columns:          2
            Layout.topMargin: 10
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
                enabled:      internal.changed
                onClicked:    cancelChanges()
            }

            PrimaryButton {
                id:           applyBtn
                leftPadding:  25
                rightPadding: 25
                text:         qsTrId("settings-apply")
                icon.source:  "qrc:/assets/icon-done.svg"
                enabled:      canApply()
                onClicked:    applyChanges()
                Layout.preferredHeight: 38
                Layout.preferredWidth:  130
            }
        }
    }
}