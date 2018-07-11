import QtQuick 2.3
import QtQuick.Window 2.2

Image {
    id: root

    sourceSize.width:  originalSizeImage.sourceSize.width  * Screen.devicePixelRatio
    sourceSize.height: originalSizeImage.sourceSize.height * Screen.devicePixelRatio

    width:  originalSizeImage.sourceSize.width
    height: originalSizeImage.sourceSize.height

    Image {
        id: originalSizeImage
        source: root.source
        visible: false
    }
}