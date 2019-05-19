import QtQuick 2.11
import QtQuick.Window 2.2

Image {
    id: root

	property real dpr: Screen.devicePixelRatio

    sourceSize.width:  originalSizeImage.sourceSize.width  * (dpr == 1.0 ? 2 : dpr)
    sourceSize.height: originalSizeImage.sourceSize.height * (dpr == 1.0 ? 2 : dpr)

    width:  originalSizeImage.sourceSize.width
    height: originalSizeImage.sourceSize.height

    Image {
        id: originalSizeImage
        source: root.source
        visible: false
    }
}
