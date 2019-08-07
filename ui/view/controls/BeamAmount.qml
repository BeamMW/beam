import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "../utils.js" as Utils

RowLayout
{
    id: control
    spacing: 5
    property double amount: 0
    property string color:  Style.content_main
    property bool   error:  false

    SFText {
        id:             amountText
        font.pixelSize: 14
        font.styleName: "Light";
        font.weight:    Font.Light
        color:          control.error ? Style.validator_error : control.color
        text:           Utils.formatAmount(amount).length ? Utils.formatAmount(amount) : "0"
    }

    SvgImage {
        id:               beamIcon
        Layout.topMargin: 3
        sourceSize:       Qt.size(10, 15)
        source:           control.error ? "qrc:/assets/b-red.svg" : "qrc:/assets/b-grey.svg"
    }
}