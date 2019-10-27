import QtQuick 2.11

AbstractColors {
    // Color definitions
    property color content_main:          "#ffffff"  // white
    property color accent_outgoing:       "#da68f5"  // heliotrope
    property color accent_incoming:       "#0bccf7"  // bright-sky-blue
    property color accent_swap:           "#39fdf2"
    property color accent_fail:           "#ff746b"
    property color content_secondary:     "#8da1ad"  // bluey-grey
    property color content_disabled:      "#889da9"
    property color content_opposite:      "#032e49" // marine
    property color validator_warning:     "#f4ce4a"
    property color validator_error:       "#ff625c"

    property color navigation_background: "#000000" 
    property color background_main:       "#042548" 
    property color background_main_top:   "#035b8f"
    property color background_second:     Qt.rgba(255, 255, 255, 0.05)
    property color background_row_even:   "#07ffffff"
    property color background_row_odd:    "#0cffffff"
    property color background_details:    "#09425e"
    property color background_button:     "#33566B"
    property color background_popup:      "#00446c"
    property color row_selected:          "#085469"
    property color separator:             "#33566b"
    property color table_header:          Qt.rgba(0, 246, 210, 0.1)

    property color active :               "#00f6d2" // bright-teal
    property color passive:               "#d6d9e0"  // silver
        
    property color caps_warning:          "#000000"

    property string linkStyle: "<style>a:link {color: '#00f6d2'; text-decoration: none;}</style>"
    property string explorerUrl: "https://explorer.beam.mw/"

    property color swapCurrencyPaneGrRight:     "#00458f"
    property color swapCurrencyPaneGrLeftBEAM:  "#00f6d2"
    property color swapCurrencyPaneGrLeftBTC:   "#fcaf38"
    property color swapCurrencyPaneGrLeftLTC:   "#bebebe"
    property color swapCurrencyPaneGrLeftQTUM:  "#2e9ad0"
    property color swapCurrencyPaneGrLeftOther: Qt.rgba(0, 246, 210, 0.1)
    property color swapCurrencyStateIndicator:  "#ff746b"
    property color swapCurrencyOptionsBorder:   Qt.rgba(0, 246, 210, 0.15)
}
