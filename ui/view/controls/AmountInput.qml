import QtQuick.Layouts 1.11
import QtQuick 2.11
import Beam.Wallet 1.0

ColumnLayout {
    id: control

    readonly property variant currencies: [
        {label: "BEAM", feeLabel: "GROTH",   minFee: 100,   defaultFee: 100},
        {label: "BTC",  feeLabel: "sat/kB",  minFee: 50000, defaultFee: 90000},
        {label: "LTC",  feeLabel: "ph/kB",   minFee: 50000, defaultFee: 90000},
        {label: "QTUM", feeLabel: "qsat/kB", minFee: 50000, defaultFee: 90000}
    ]

    readonly property bool isValid: hasFee ? fee >= currencies[currency].minFee : true

    property string   title
    property string   color:     Style.accent_incoming
    property string   currColor: Style.content_main
    property bool     hasFee:    false
    property bool     multi:     false
    property int      currency:  Currency.CurrBEAM
    property double   amount:    0
    property int      fee:       currencies[currency].defaultFee

    function currList() {
        return ["BEAM", "BTC", "LTC", "QTUM"]
    }

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
            id:               amountInput
            Layout.fillWidth: true
            font.pixelSize:   36
            font.styleName:   "Light"
            font.weight:      Font.Light
            color:            control.color
            validator:        RegExpValidator {regExp: /^(([1-9][0-9]{0,7})|(1[0-9]{8})|(2[0-4][0-9]{7})|(25[0-3][0-9]{6})|(0))(\.[0-9]{0,7}[1-9])?$/}
            selectByMouse:    true
            text:             formatAmount()

            onTextChanged: {
                if (focus) control.amount = text ? parseFloat(text) : 0;
            }

            onFocusChanged: {
                text = formatAmount()
            }

            function formatAmount() {
                return control.amount ? control.amount.toLocaleString(focus ? Qt.locale("C") : Qt.locale(), 'f', -128) : ""
            }
        }

        SFText {
            Layout.topMargin:   22
            font.pixelSize:     24
            font.letterSpacing: 0.6
            color:              control.currColor
            text:               currencies[control.currency].label
            visible:            !multi
        }

        CustomComboBox {
            id:                  currCombo
            Layout.topMargin:    22
            Layout.minimumWidth: 95
            spacing:             0
            fontPixelSize:       24
            fontLetterSpacing:   0.6
            currentIndex:        currency
            color:               control.currColor
            visible:             multi
            model:               currList()

            onCurrentIndexChanged: {
                control.amount   = 0
                amountInput.text = ""
            }
        }

        Binding {
            target:   control
            property: "currency"
            value:    currCombo.currentIndex
        }
    }

    SFText {
        Layout.topMargin: 30
        font.pixelSize:   14
        font.styleName:   "Bold"
        font.weight:      Font.Bold
        color:            Style.content_main
        text:             currency == Currency.CurrBEAM ? qsTrId("general-fee") : qsTrId("general-fee-rate")
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
            color:                 isValid ? control.color : Style.validator_error
            backgroundColor:       isValid ? Style.content_main : Style.validator_error
            maximumLength:         9
            selectByMouse:         true
            text:                  formatFee()
            validator:             IntValidator {bottom: currencies[currency].minFee}

            onTextChanged: {
                if (focus) control.fee = text ? parseInt(text) : 0
            }

            onFocusChanged: {
                text = formatFee()
            }

            function formatFee(fee) {
                return control.fee ? control.fee.toLocaleString(focus ? Qt.locale("C") : Qt.locale(), 'f', -128) : ""
            }
        }

        SFText {
            font.pixelSize: 14
            color:          Style.content_main
            text:           currencies[currency].feeLabel
        }
    }

    Item {
        Layout.fillWidth: true
        SFText {
            //% "The minimum fee is %1 GROTH"
            text:            qsTrId("general-fee-fail").arg(currencies[currency].minFee).arg(currencies[currency].feeLabel)
            color:           Style.validator_error
            font.pixelSize:  11
            font.styleName:  "Italic"
            width:           parent.width
            visible:         control.visible && !control.isValid
        }
    }
}