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

    property string  amount:          "0"
    property string  color:           Style.content_main
    property bool    error:           false
    property bool    showZero:        true
    property bool    showDrop:        false
    property int     fontSize:        14
    property bool    lightFont:       true
    property string  currencySymbol:  Utils.symbolBeam
    property string  iconSource:      ""
    property size    iconSize:        Qt.size(0, 0)
    property alias   copyMenuEnabled: amountText.copyMenuEnabled
    property alias   caption:         captionText.text
    property int     captionFontSize: 12
    property string  prefix:          ""

    contentItem: RowLayout{
        spacing: control.spacing

        SvgImage {
            Layout.topMargin:   3
            source:             control.iconSource
            sourceSize:         control.iconSize
            visible:            !!control.iconSource
        }

        ColumnLayout {
            SFLabel {
                id:             captionText
                visible:        text.length > 0
                font.pixelSize: captionFontSize
                font.styleName: "Light"
                font.weight:    Font.Light
                color:          Qt.rgba(Style.content_main.r, Style.content_main.g, Style.content_main.b, 0.5)
            }

            RowLayout {
                SFLabel {
                    id:              amountText
                    font.pixelSize:  fontSize
                    font.styleName:  lightFont ? "Light" : "Regular"
                    font.weight:     lightFont ? Font.Light : Font.Normal
                    color:           control.error ? Style.validator_error : control.color
                    text:            parseFloat(amount) > 0 || showZero ? prefix + [Utils.uiStringToLocale(amount), control.currencySymbol].join(" ") : "-"
                    onCopyText:      BeamGlobals.copyToClipboard(amount)
                    copyMenuEnabled: true
                }
                Image {
                    visible: showDrop
                    source:  "qrc:/assets/icon-down.svg"
                }
            }
        }
    }
}