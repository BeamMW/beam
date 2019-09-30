import QtQuick 2.11
import QtQuick.Layouts 1.3

Rectangle {
    property color borderColor: Style.swapCurrencyOptionsBorder
    property int borderSize: 0
    property alias rectOpacity: rect.opacity
    property color gradLeft: Style.swapCurrencyPaneGrLeftBEAM
    property color gradRight: Style.swapCurrencyPaneGrRight
    property string currencyIcon: ""
    property var currencyIcons: []
    property color stateIndicatorColor: Style.swapCurrencyStateIndicator
    property string valueStr: ""
    property string vatueSecondaryStr: ""
    property bool isOk: true
    property int textSize: 16
    property int textSecondarySize: 12
    property color textColor: Style.content_main
    property color textSecondaryColor: Style.content_secondary
    property string textConnectionError: "error"
    property bool showLoader: false
    property var onClick: function() {}

    Layout.fillWidth: true
    height: 67
    color: "transparent"

    Rectangle {
        id: rect
        width:  parent.height
        height: parent.width
        anchors.centerIn: parent
        anchors.alignWhenCentered: false
        rotation: 90
        radius:   10
        opacity: 0.3
        gradient: Gradient {
            GradientStop { position: 0.0; color: gradRight }
            GradientStop { position: 1.0; color: gradLeft }
        }
        border {
            width: borderSize
            color: borderColor
        }
    }
    Item {
        anchors.fill: parent

        Item {
            id: currencyLogo
            width: childrenRect.width
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.margins: {
                left: 20
            }
            SvgImage {
                anchors.verticalCenter: parent.verticalCenter
                source: currencyIcon
                visible: currencyIcon.length
            }

            Repeater {
                model: currencyIcons.length
                visible: currencyIcons.length
                
                SvgImage {
                    anchors.verticalCenter: parent.verticalCenter
                    x: parent.x + index * 15 - 20
                    source: currencyIcons[index]
                }
            }
        }

        Column {
            anchors.left: currencyLogo.right
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: isOk
            SFText {
                anchors.left: parent.left
                anchors.right: parent.right
                leftPadding: 20
                rightPadding: 20
                font.pixelSize: textSize
                color: textColor
                elide: Text.ElideRight
                text: valueStr
                wrapMode: Text.Wrap
                visible: valueStr.length
            }
            SFText {
                anchors.left: parent.left
                anchors.right: parent.right
                leftPadding: 20
                rightPadding: 20
                font.pixelSize: textSecondarySize
                color: textSecondaryColor
                elide: Text.ElideRight
                wrapMode: Text.Wrap
                text: vatueSecondaryStr
                visible: vatueSecondaryStr.length
            }
        }

        SFText {
            id: connectionError
            anchors.left: currencyLogo.right
            anchors.right: connectionErrorIndicator.left
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 10
            rightPadding: 10
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            color: Style.validator_error
            elide: Text.ElideRight
            wrapMode: Text.Wrap
            text: textConnectionError
            visible: !isOk
        }

        SvgImage {
            id: loader
            property double angleValue: 0
            visible: showLoader
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.margins: {
                right: 15
            }

            source: "qrc:/assets/loading-spinner.svg"            
            rotation: loader.angleValue
            Timer {
                interval: 200; running: true; repeat: true
                onTriggered: loader.angleValue += 15
            }
        }

        Item {
            id: connectionErrorIndicator
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: {
                right: 15
            }
            width: childrenRect.width
            visible: !isOk

            property int radius: 5

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left

                width: parent.radius * 2
                height: parent.radius * 2
                radius: parent.radius
                color: stateIndicatorColor
            }
        }
    }
}
