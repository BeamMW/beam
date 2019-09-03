import QtQuick 2.11

AbstractColors {
    property color content_main:          "#ffffff"  // white
    property color accent_outgoing:       "#da68f5"  // heliotrope
    property color accent_incoming:       "#0bccf7"  // bright-sky-blue
    property color content_secondary:     "#8da1ad"  // bluey-grey
    property color content_disabled:      "#889da9"
    property color content_opposite:      "#171717"
    property color validator_warning:     "#f4ce4a"
    property color validator_error:       "#ff625c"

    property color navigation_background: "#101010"
    property color background_main:       "#171717"
    property color background_second:     "#323232"
    property color background_row_even:   "#242424"
    property color background_details:    "#3d3d3d"
    property color background_button:     "#474646"
    property color row_selected:          "#353636"
    property color separator:             "#353636"

    property color active :               "#00f6d2" // bright-teal
    property color passive:               "#d6d9e0"  // silver
        
    property color caps_warning:          "#ffffff"

    property string linkStyle: "<style>a:link {color: '#00f6d2'; text-decoration: none;}</style>"
    property string explorerUrl: "https://master-net.explorer.beam.mw/"
}
