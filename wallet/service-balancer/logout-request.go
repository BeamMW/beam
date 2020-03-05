package main

import (
	"encoding/json"
	"fmt"
	"github.com/olahol/melody"
	"log"
	"net/http"
)

type logout struct {
	WalletID string
}

func logoutRequest(r *http.Request) (res interface{}, err error) {
	var req logout

	decoder := json.NewDecoder(r.Body)
	if err = decoder.Decode(&req); err != nil {
		return
	}

	if len(req.WalletID) == 0 {
		err = fmt.Errorf("logout request, bad wallet id %v", req.WalletID)
		return
	}

	err = monitorLogout(req.WalletID)
	return
}

type rpcLogoutParams struct {
	WalletID string `json:"WalletID"`
}

type rpcLogoutResult struct {
	Logout bool `json:"logout"`
}

func rpcLogoutRequest(session* melody.Session, params *json.RawMessage) (result interface{}, err error) {
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

	log.Printf("wallet %v, rpc logout request", logoutParms.WalletID)
	if err = monitorLogout(logoutParms.WalletID); err != nil {
		return
	}

	session.Set(RpcKeyWID, nil)
	result = &rpcLogoutResult {
		Logout: true,
	}
	return
}

func rpcDisconnect(session *melody.Session) error {
	wid, err := getWID(session)
	if err != nil {
		return err
	}

	// Disconnect is always called
	// Client might be already logged out
	// in this case we just ignore and leave
	if len(wid) != 0 {
		log.Printf("wallet %v, rpc disconnect", wid)
		session.Set(RpcKeyWID, nil)
		return monitorLogout(wid)
	}

	return nil
}
