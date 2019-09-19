import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "../utils.js" as Utils

Control {
    id: control
    spacing: 8

    property double  amount:          0
    property string  color:           Style.content_main
    property bool    error:           false
    property int     fontSize:        14
    property string  currencySymbol:  Utils.symbolBeam
    property string  iconSource:      undefined
    property size    iconSize:        undefined
    property alias   copyMenuEnabled: amountText.copyMenuEnabled

    contentItem: RowLayout{
        spacing: control.spacing

        SvgImage {
            Layout.topMargin:   3
            source:             control.iconSource
            sourceSize:         control.iconSize
            visible:            !!control.iconSource
        }

        SFLabel {
            id:             amountText
            font.pixelSize: fontSize
            font.styleName: "Light"
            font.weight:    Font.Light
            color:          control.error ? Style.validator_error : control.color
            text:           [Utils.formatAmount(amount).length ? Utils.formatAmount(amount) : "0", control.currencySymbol].join(" ")
            onCopyText:     BeamGlobals.copyToClipboard(Utils.formatAmount(amount))
        }
    }
}