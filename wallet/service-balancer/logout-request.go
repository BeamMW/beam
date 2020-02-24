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

func logoutRequest(w http.ResponseWriter, r *http.Request) (res interface{}, err error) {
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
	var logoutParms  rpcLogoutParams
	if err = json.Unmarshal(*params, &logoutParms); err != nil {
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
	if iwid, ok := session.Get(RpcKeyWID); ok && iwid != nil {
		if wid, ok := iwid.(string); ok {
			log.Printf("wallet %v, rpc disconnect", wid)
			session.Set(RpcKeyWID, nil)
			return monitorLogout(wid)
		} else {
			return fmt.Errorf("ERROR, invalid or nil wid in rpcDisconnect, %v", wid)
		}
	}
	return nil
}
