import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import Beam.Wallet 1.0
import "."
import "../utils.js" as Utils

Control {
    id: control

    property string beamValue
    property string btcValue
    property string ltcValue
    property string qtumValue

    property bool btcOK:  true
    property bool ltcOK:  true
    property bool qtumOK: true

    property alias  color:     panel.color
    property string textColor: Style.content_main

    property var onOpenExternal: null
    signal copyValueText()

    background: Rectangle {
        id:      panel
        radius:  10
        color:   Style.background_second
    }

    leftPadding:   25
    rightPadding:  25
    topPadding:    25
    bottomPadding: 25

    function onlyBeam () {
        return !BeamGlobals.haveBtc() && ! BeamGlobals.haveLtc() && !BeamGlobals.haveQtum()
    }

    contentItem: ColumnLayout {
        RowLayout {
            Layout.fillWidth: true

            SFText {
                font.pixelSize:    18
                font.styleName:    "Bold"
                font.weight:       Font.Bold
                color:             Style.content_main
                //% "Available"
                text:              qsTrId("available-panel-available")
            }

            Item {
                Layout.fillWidth: true
            }

            SFText {
                id:               whereToBuy
                font.pixelSize:   14
                Layout.alignment: Qt.AlignTop
                color:            Style.active
                opacity:          0.5
                //% "Where to buy BEAM?"
                text:             qsTrId("available-panel-where-to-buy")

                MouseArea {
                    anchors.fill:    parent
                    acceptedButtons: Qt.LeftButton
                    cursorShape:     Qt.PointingHandCursor
                    onClicked: {
                        if (onOpenExternal && typeof onOpenExternal === 'function') {
                            onOpenExternal();
                        }
                    }
                    hoverEnabled: true
                }
            }

            SvgImage {
                id:                whereToBuyIcon
                Layout.alignment:  Qt.AlignTop
                source:            "qrc:/assets/icon-external-link.svg"
                sourceSize:        Qt.size(14, 14)

                MouseArea {
                    anchors.fill:    parent
                    acceptedButtons: Qt.LeftButton
                    cursorShape:     Qt.PointingHandCursor
                    onClicked: {
                        if (onOpenExternal && typeof onOpenExternal === 'function') {
                            onOpenExternal();
                        }
                    }
                    hoverEnabled: true
                }
            }
        }

        RowLayout {
            id:                amount
            visible:           onlyBeam()
            Layout.topMargin:  35
            Layout.fillWidth:  true

            Item {Layout.fillWidth: true}

            BeamAmount {
                amount:          beamValue
                spacing:         20
                color:           textColor
                fontSize:        38
                currencySymbol:  Utils.symbolBeam
                iconSource:      "qrc:/assets/beam-circle.svg"
                iconSize:        Qt.size(34, 34)
                copyMenuEnabled: true
            }

            Item {Layout.fillWidth: true}
        }

        Item {Layout.fillHeight: true}

        RowLayout {
            Layout.fillWidth:   true

            Item {Layout.fillWidth: true}

            GridLayout {
                columnSpacing:      25
                rowSpacing:         20
                columns:            2
                rows:               2
                visible:            !onlyBeam()
                flow:               GridLayout.TopToBottom

                BeamAmount {
                    amount:           beamValue
                    color:            textColor
                    fontSize:         23
                    currencySymbol:   Utils.symbolBeam
                    iconSource:       "qrc:/assets/beam-circle.svg"
                    iconSize:         Qt.size(23, 23)
                    copyMenuEnabled:  true
                    spacing:          10
                }

                BeamAmount {
                    amount:           btcValue
                    fontSize:         23
                    currencySymbol:   Utils.symbolBtc
                    visible:          BeamGlobals.haveBtc()
                    color:            btcOK ? textColor : Style.validator_error
                    iconSource:       "qrc:/assets/btc-circle.svg"
                    iconSize:         Qt.size(23, 23)
                    copyMenuEnabled:  true
                    spacing:          10
                }

                BeamAmount {
                    amount:           ltcValue
                    fontSize:         23
                    currencySymbol:   Utils.symbolLtc
                    visible:          BeamGlobals.haveLtc()
                    color:            ltcOK ? textColor : Style.validator_error
                    iconSource:       "qrc:/assets/ltc-circle.svg"
                    iconSize:         Qt.size(23, 23)
                    copyMenuEnabled:  true
                    spacing:          10
                }

                BeamAmount {
                    amount:           qtumValue
                    fontSize:         22
                    currencySymbol:   Utils.symbolQtum
                    visible:          BeamGlobals.haveQtum()
                    color:            qtumOK ? textColor : Style.validator_error
                    iconSource:       "qrc:/assets/qtum-circle.svg"
                    iconSize:         Qt.size(23, 23)
                    copyMenuEnabled:  true
                    spacing:          10
                }
            }

            Item {Layout.fillWidth: true}
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
