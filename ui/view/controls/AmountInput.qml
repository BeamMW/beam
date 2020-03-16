import QtQuick.Layouts 1.11
import QtQuick 2.11
import Beam.Wallet 1.0
import "../utils.js" as Utils
import Beam.Wallet 1.0

ColumnLayout {
    id: control

    function getFeeTitle() {
        if (control.currency == Currency.CurrBeam) {
            return control.currFeeTitle ?
                //% "BEAM Transaction fee"
                qsTrId("beam-transaction-fee") :
                //% "Transaction fee"
                qsTrId("general-fee")
        }
        //% "%1 Transaction fee rate"
        return qsTrId("general-fee-rate").arg(control.currencyLabel)
    }

    function getTotalFeeTitle() {
        //% "%1 Transaction fee (est)"
        return qsTrId("general-fee-total").arg(control.currencyLabel)
    }

    function getTotalFeeAmount() {
        return BeamGlobals.calcTotalFee(control.currency, control.fee);
    }

    function getFeeInSecondCurrency(feeValue) {
        return BeamGlobals.calcFeeInSecondCurrency(feeValue, control.currency, control.secondCurrencyRateValue, control.secondCurrencyLabel)
    }

    function getAmountInSecondCurrency() {
        return BeamGlobals.calcAmountInSecondCurrency(control.amount, control.secondCurrencyRateValue, control.secondCurrencyLabel)
    }

    readonly property bool     isValidFee:     hasFee ? feeInput.isValid : true
    readonly property bool     isValid:        error.length == 0 && isValidFee
    readonly property string   currencyLabel:  BeamGlobals.getCurrencyLabel(control.currency)

    property string   title
    property string   color:        Style.accent_incoming
    property string   currColor:    Style.content_main
    property bool     hasFee:       false
    property bool     currFeeTitle: false
    property bool     multi:        false // changing this property in runtime would reset bindings
    property int      currency:     Currency.CurrBeam
    property string   amount:       "0"
    property string   amountIn:     "0"  // public property for binding. Use it to avoid binding overriding
    property int      fee:          BeamGlobals.getDefaultFee(control.currency)
    property alias    error:        errmsg.text
    property bool     readOnlyA:    false
    property bool     readOnlyF:    false
    property bool     resetAmount:  true
    property var      amountInput:  ainput
    property bool     showTotalFee: false
    property bool     showAddAll:   false
    property string   secondCurrencyRateValue:  "0"
    property string   secondCurrencyLabel:      ""
    property var      setMaxAvailableAmount: {} // callback function

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
            text:             formatDisplayedAmount()
            readOnly:         control.readOnlyA

            onTextChanged: {
                // if nothing then "0", remove insignificant zeroes and "." in floats
                if (ainput.focus) {
                    control.amount = text ? text.replace(/\.0*$|(\.\d*[1-9])0+$/,'$1') : "0"
                }
            }

            onFocusChanged: {
                text = formatDisplayedAmount()
                if (focus) cursorPosition = positionAt(ainput.getMousePos().x, ainput.getMousePos().y)
            }

            function formatDisplayedAmount() {
                return control.amountIn == "0" ? "" : (ainput.focus ? control.amountIn : Utils.uiStringToLocale(control.amountIn))
            }

            Connections {
                target: control
                onAmountInChanged: {
                    if (!ainput.focus) {
                        ainput.text = ainput.formatDisplayedAmount()
                    }
                }
            }
        }

        SFText {
            Layout.topMargin:   22
            font.pixelSize:     24
            font.letterSpacing: 0.6
            color:              control.currColor
            text:               control.currencyLabel
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
            model:               Utils.currenciesList()

            onActivated: {
                if (multi) control.currency = index
                if (resetAmount) control.amount = 0
            }
        }

        RowLayout {
            id:                  addAllButton
            Layout.alignment:    Qt.AlignBottom
            Layout.bottomMargin: 7
            Layout.leftMargin:   25
            visible:             control.showAddAll

            function addAll(){
                ainput.focus = false;                
                if (control.setMaxAvailableAmount) {
                    control.setMaxAvailableAmount();
                }
            }

            SvgImage {
                Layout.maximumHeight: 16
                Layout.maximumWidth:  16
                source: "qrc:/assets/icon-send-blue-copy-2.svg"
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        addAllButton.addAll();
                    }
                }
            }

            SFText {
                font.pixelSize:   14
                font.styleName:   "Bold";
                font.weight:      Font.Bold
                color:            control.color
                //% "add all"
                text:             qsTrId("amount-input-add-all")
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        addAllButton.addAll();
                    }
                }
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
        SFText {
            id:             amountSecondCurrencyText
            visible:        secondCurrencyLabel != "" && !errmsg.visible && !showTotalFee    // show only on send side
            font.pixelSize: 14
            color:          Style.content_secondary
            text:           getAmountInSecondCurrency()
        }
    }

    GridLayout {
        columns:       2
        Layout.topMargin: 30
        ColumnLayout {
            Layout.maximumWidth:  198
            visible:              control.hasFee
            SFText {
                font.pixelSize:   14
                font.styleName:   "Bold"
                font.weight:      Font.Bold
                color:            Style.content_main
                text:             getFeeTitle()
            }
            FeeInput {
                id:               feeInput
                Layout.fillWidth: true
                fee:              control.fee
                minFee:           BeamGlobals.getMinimalFee(control.currency)
                feeLabel:         BeamGlobals.getFeeRateLabel(control.currency)
                color:            control.color
                readOnly:         control.readOnlyF
                Connections {
                    target: control
                    onFeeChanged: feeInput.fee = control.fee
                    onCurrencyChanged: feeInput.fee = BeamGlobals.getDefaultFee(control.currency)
                }
            }
            SFText {
                id:               feeInSecondCurrency
                visible:          control.secondCurrencyLabel != ""
                font.pixelSize:   14
                color:            Style.content_secondary
                text:             getFeeInSecondCurrency(control.fee)
            }
        }
       
        ColumnLayout {
            Layout.alignment:     Qt.AlignLeft | Qt.AlignTop
            visible:              showTotalFee && control.hasFee && control.currency != Currency.CurrBeam
            SFText {
                font.pixelSize:   14
                font.styleName:   "Bold"
                font.weight:      Font.Bold
                color:            Style.content_main
                text:             getTotalFeeTitle()
            }
            SFText {
                id:               totalFeeLabel
                Layout.topMargin: 6
                font.pixelSize:   14
                color:            Style.content_main
                text:             getTotalFeeAmount()
            }
            SFText {
                id:               feeTotalInSecondCurrency
                Layout.topMargin: 6
                font.pixelSize:   14
                color:            Style.content_secondary
                text:             getFeeInSecondCurrency(parseInt(totalFeeLabel.text, 10))
            }
        }
    }

    SFText {
        enabled:               control.hasFee && control.currency != Currency.CurrBeam
        visible:               enabled
        Layout.preferredWidth: 370
        font.pixelSize:        14
        wrapMode:              Text.WordWrap
        color:                 Style.content_secondary
        lineHeight:            1.1 
        //% "Remember to validate the expected fee rate for the blockchain (as it varies with time)."
        text:                  qsTrId("settings-fee-rate-note")
    }

    Binding {
        target:   control
        property: "fee"
        value:    feeInput.fee
    }
}
