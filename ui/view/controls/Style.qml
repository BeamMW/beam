pragma Singleton
import QtQuick 2.11
import Beam.Wallet 1.0
import "../color_themes"

AbstractColors {

	property var themes: QtObject {
		property AbstractColors masternet: Masternet{}
		property AbstractColors testnet: Testnet{}
		property AbstractColors mainnet: Mainnet{}
	}


	Component.onCompleted: {
		var currentTheme = themes[Theme.name()]
		if (!currentTheme) {
			currentTheme = themes['masternet'];
		}

		for (var propName in this) {
			if (typeof this[propName] != "function"
				&& propName != "objectName"
				&& propName != "themes") {
				this[propName] = currentTheme[propName]
			}
				
		}
	}
}
