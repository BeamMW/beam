import QtQuick 2.11

AbstractColors {
    // Color definitions
    property color content_main:          "#ffffff"  // white
    property color accent_outgoing:       "#da68f5"  // heliotrope
    property color accent_incoming:       "#0bccf7"  // bright-sky-blue
    property color content_secondary:     "#8da1ad"  // bluey-grey
    property color content_disabled:      "#889da9"
    property color content_opposite:      "#032e49" // marine
    property color validator_warning:     "#f4ce4a"
    property color validator_error:       "#ff625c"

    property color navigation_background: "#02253c" // navy
    property color background_main:       "#032e49" // marine
    property color background_second:     "#1c435b"  // dark-slate-blue
    property color background_row_even:   "#0e3850"  // light-navy
    property color background_details:    "#09425e"
    property color background_button:     "#33566B"
    property color row_selected:          "#085469"
    property color separator:             "#33566b"

    property color active :               "#00f6d2" // bright-teal
    property color passive:               "#d6d9e0"  // silver
        
    property color caps_warning:          "#000000"

    property string linkStyle: "<style>a:link {color: '#00f6d2'; text-decoration: none;}</style>"
    property string explorerUrl: "https://explorer.beam.mw/"
}
