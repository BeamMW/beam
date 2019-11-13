import QtQuick.Layouts 1.11
import QtQuick 2.11
import Beam.Wallet 1.0
import "../utils.js" as Utils
import Beam.Wallet 1.0

ColumnLayout {
    id: control
    spacing: 0

    property bool ipOnly:   true
    property string color:  Style.content_main
    property alias address: addressInput.text
    readonly property bool  isValid: addressInput.acceptableInput

    property var ipValidator: RegExpValidator {regExp: /^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5]):([1-9]|[1-5]?[0-9]{2,4}|6[1-4][0-9]{3}|65[1-4][0-9]{2}|655[1-2][0-9]|6553[1-5])$/g}
    property var ipDomainValidator: RegExpValidator {regExp: /^((((([A-Za-z0-9-]{1,63}(?!-)\.)+[A-Za-z]{2,6}))|((([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5]))):([1-9]|[1-5]?[0-9]{2,4}|6[1-4][0-9]{3}|65[1-4][0-9]{2}|655[1-2][0-9]|6553[1-5]))$/g}

    property alias readOnly: addressInput.readOnly
    property alias underlineVisible: addressInput.underlineVisible

    SFTextInput {
        id:               addressInput
        Layout.fillWidth: true
        activeFocusOnTab: true
        font.pixelSize:   14
        font.italic:      address.length && !isValid
        color:            address.length && !isValid ? Style.validator_error : control.color
        backgroundColor:  address.length && !isValid ? Style.validator_error : control.color
        text:             control.address
        validator:        ipOnly ? ipValidator : ipDomainValidator
    }

    Item{
        Layout.fillWidth: true
        SFText {
            font.pixelSize: 12
            font.italic:    true
            color:          Style.validator_error
            text:           qsTrId("general-invalid-address")
            visible:        address.length && !isValid
        }
    }
}