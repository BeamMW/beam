package main

import (
	"github.com/olahol/melody"
)

func onWalletPong(session *melody.Session) (err error) {
	wid, err := getValidWID(session)
	if err != nil {
		return
	}

	err = walletServicesAlive(wid)
	return
}
