import QtQuick.Layouts 1.11
import QtQuick 2.11
import Beam.Wallet 1.0
import "../utils.js" as Utils
import Beam.Wallet 1.0

ColumnLayout {
    id: control

    readonly property variant currencies: [
        {label: "BEAM", feeLabel: "GROTH",   minFee: BeamGlobals.minFeeBEAM(), defaultFee: 100},
        {label: "BTC",  feeLabel: "sat/kB",  minFee: BeamGlobals.minFeeBTC(),  defaultFee: 90000},
        {label: "LTC",  feeLabel: "ph/kB",   minFee: BeamGlobals.minFeeLTC(),  defaultFee: 90000},
        {label: "QTUM", feeLabel: "qsat/kB", minFee: BeamGlobals.minFeeQTUM(), defaultFee: 90000}
    ]

    function currList() {
        return ["BEAM", "BTC", "LTC", "QTUM"]
    }

    function getCurrencyLabel() {
        return currencies[control.currency].label
    }

    function getFeeLabel() {
        return currencies[control.currency].feeLabel
    }

    readonly property bool isValidFee: hasFee ? fee >= currencies[currency].minFee : true
    readonly property bool isValid: error.length == 0 && isValidFee

    property string   title
    property string   color:       Style.accent_incoming
    property string   currColor:   Style.content_main
    property bool     hasFee:      false
    property bool     multi:       false // changing this property in runtime would reset bindings
    property int      currency:    Currency.CurrBEAM
    property double   amount:      0
    property int      fee:         currencies[currency].defaultFee
    property alias    error:       errmsg.text
    property bool     readOnly:    false
    property bool     resetAmount: true
    property var      amountInput: ainput

    SFText {
        font.pixelSize:   14
        font.styleName:   "Bold"
        font.weight:      Font.Bold
        color:            Style.content_main
        text:             control.title
    }

    RowLayout {
        Layout.fillWidth: true

        SFTextInput {
            id:               ainput
            Layout.fillWidth: true
            font.pixelSize:   36
            font.styleName:   "Light"
            font.weight:      Font.Light
            color:            error.length ? Style.validator_error : control.color
            backgroundColor:  error.length ? Style.validator_error : Style.content_main
            validator:        RegExpValidator {regExp: /^(([1-9][0-9]{0,7})|(1[0-9]{8})|(2[0-4][0-9]{7})|(25[0-3][0-9]{6})|(0))(\.[0-9]{0,7}[1-9])?$/}
            selectByMouse:    true
            text:             formatAmount()
            readOnly:         control.readOnly

            onTextChanged: {
                if (focus) control.amount = text ? parseFloat(text) : 0;
            }

            onFocusChanged: {
                text = formatAmount()
            }

            function formatAmount() {
                return Utils.formatAmount(control.amount, focus)
            }
        }

        SFText {
            Layout.topMargin:   22
            font.pixelSize:     24
            font.letterSpacing: 0.6
            color:              control.currColor
            text:               getCurrencyLabel()
            visible:            !multi
        }

        CustomComboBox {
            id:                  currCombo
            Layout.topMargin:    22
            Layout.minimumWidth: 95
            spacing:             0
            fontPixelSize:       24
            fontLetterSpacing:   0.6
            currentIndex:        control.currency
            color:               control.currColor
            visible:             multi
            model:               currList()

            onCurrentIndexChanged: {
                if (multi) control.currency = currentIndex
                // if (resetAmount) control.amount = 0
            }
        }
    }

    Item {
        Layout.fillWidth: true
        SFText {
            id:              errmsg
            color:           Style.validator_error
            font.pixelSize:  12
            font.styleName:  "Italic"
            width:           parent.width
            visible:         error.length
        }
    }

    SFText {
        Layout.topMargin: 30
        font.pixelSize:   14
        font.styleName:   "Bold"
        font.weight:      Font.Bold
        color:            Style.content_main
        text:             control.currency == Currency.CurrBEAM ? qsTrId("general-fee") : qsTrId("general-fee-rate")
        visible:          control.hasFee
    }

    RowLayout {
        Layout.fillWidth: true
        visible:          control.hasFee

        SFTextInput {
            id:                    feeInput
            Layout.preferredWidth: 150
            font.pixelSize:        14
            font.styleName:        "Light"
            font.weight:           Font.Light
            font.italic:           !isValid
            color:                 isValidFee ? control.color : Style.validator_error
            backgroundColor:       isValidFee ? Style.content_main : Style.validator_error
            maximumLength:         9
            selectByMouse:         true
            text:                  formatFee()
            validator:             IntValidator {bottom: currencies[control.currency].minFee}
            readOnly:              control.readOnly

            onTextChanged: {
                if (focus) control.fee = text ? parseInt(text) : 0
            }

            onFocusChanged: {
                text = formatFee()
            }

            function formatFee() {
                return control.fee ? control.fee.toLocaleString(focus ? Qt.locale("C") : Qt.locale(), 'f', -128) : ""
            }
        }

        SFText {
            font.pixelSize: 14
            color:          Style.content_main
            text:           getFeeLabel()
        }
    }

    Item {
        Layout.fillWidth: true
        SFText {
            //% "The minimum fee is %1 GROTH"
            text:            qsTrId("general-fee-fail").arg(currencies[control.currency].minFee).arg(getFeeLabel())
            color:           Style.validator_error
            font.pixelSize:  12
            font.styleName:  "Italic"
            width:           parent.width
            visible:         hasFee && !control.isValidFee
        }
    }
}