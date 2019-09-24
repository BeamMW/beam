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

    property alias  title:        controlTitle.text
    property alias  address:      addressInput.address
    property alias  seed:         seedInput.text
    property string feeRateLabel: ""
    property int    feeRate:      0
    property int    minFeeRate:   0
    signal newSeed()

    signal apply
    signal switchOff

    QtObject {
        id: internal
        property string initialAddress
        property string initialSeed
        property int    initialFeeRate
        property bool   changed: false

        function restore() {
            address  = initialAddress
            seed     = initialSeed
            feeRate  = initialFeeRate
            feeRateInput.fee = initialFeeRate
            changed  = false
        }

        function save() {
            initialAddress  = address
            initialSeed     = seed
            initialFeeRate  = feeRate
            changed = false
        }
    }

    Component.onCompleted: {
        internal.save()
    }

    function canApply() {
        return internal.changed && feeRate >= minFeeRate && seedInput.acceptableInput && addressInput.isValid
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
               internal.initialSeed.length != 0
    }

    onTitleChanged:    internal.changed = true
    onAddressChanged:  internal.changed = true
    onSeedChanged:     internal.changed = true
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
                ipOnly:           false
            }

            SFText {
                font.pixelSize: 14
                color:          Style.content_main
                ////% "Username"
                text:           "Seed Phrase"
            }

            ColumnLayout {
                Layout.fillWidth: true

                SFTextInput {
                    id:                  seedInput
                    Layout.fillWidth:    true
                    font.pixelSize:      14
                    activeFocusOnTab:    true
                    wrapMode:            TextInput.Wrap
                    implicitHeight:      50
                    validator:           RegExpValidator {regExp: /^([a-z]{2,20}\ ){11}([a-z]{2,20}){1}$/g}
                    font.italic:         text.length && !acceptableInput
                    color:               text.length && !acceptableInput ? Style.validator_error : Style.content_secondary
                    backgroundColor:     text.length && !acceptableInput ? Style.validator_error : Style.content_secondary
                    placeholderText:     qsTrId("settings-new-seed")
                    horizontalAlignment: focus || text.length > 0 ? Text.AlignLeft : Text.AlignHCenter

                    MouseArea {
                        anchors.fill: parent;
                        acceptedButtons: Qt.LeftButton
                        onDoubleClicked: if (seedInput.text.length == 0) thisControl.newSeed()
                        onClicked: parent.forceActiveFocus()
                    }
                }

                Item {
                    Layout.fillWidth: true
                    SFText {
                        font.pixelSize: 12
                        font.italic:    true
                        color:          Style.validator_error
                        text:           qsTrId("settings-invalid-seed")
                        visible:        seedInput.text.length && !seedInput.acceptableInput
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