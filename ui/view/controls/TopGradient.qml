import QtQuick 2.11

Item {
    id: root

    property var mainRoot
    property color topColor

    property color mainTopColor: null
    property color mainTopGradientColor: null

    Component.onCompleted: {
        mainTopColor = mainRoot.topColor;
        mainTopGradientColor = mainRoot.topGradientColor;
        mainRoot.topColor = Qt.rgba(topColor.r, topColor.g, topColor.b, 0.5);
        mainRoot.topGradientColor = Qt.rgba(topColor.r, topColor.g, topColor.b, 0.0);
    }

    Component.onDestruction: {
        mainRoot.topColor = mainTopColor;
        mainRoot.topGradientColor = mainTopGradientColor;
    }
}