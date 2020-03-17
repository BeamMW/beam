package main

import (
	"encoding/json"
	"fmt"
	"github.com/olahol/melody"
	"log"
)

type rpcLogoutParams struct {
	WalletID string `json:"WalletID"`
}

type rpcLogoutResult struct {
	Logout bool `json:"logout"`
}

func onWalletLogout(session* melody.Session, params *json.RawMessage) (result interface{}, err error) {
	wid, err := getValidWID(session)
	if err != nil {
		return
	}

	var logoutParms  rpcLogoutParams
	if err = json.Unmarshal(*params, &logoutParms); err != nil {
		return
	}

	if wid != logoutParms.WalletID {
		err = fmt.Errorf("logout request for %v on wrong session (wid mismatch)", logoutParms.WalletID)
		return
	}

	if config.Debug {
		log.Printf("wallet %v, rpc logout request", logoutParms.WalletID)
	}

	if err = walletServicesLogout(logoutParms.WalletID); err != nil {
		return
	}

	session.Set(RpcKeyWID, nil)
	result = &rpcLogoutResult {
		Logout: true,
	}
	return
}

func onWalletDisconnect(session *melody.Session) error {
	// Client might be already logged out since disconnect is always called
	// on connection release if there is no wallet id we just ignore and leave
	if wid, err := getValidWID(session); err == nil {
		if config.Debug {
			log.Printf("wallet %v, rpc disconnect", wid)
		}
		session.Set(RpcKeyWID, nil)
		return walletServicesLogout(wid)
	}
	return nil
}
