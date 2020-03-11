package main

import (
	"encoding/json"
	"fmt"
	"github.com/olahol/melody"
	"log"
)

type rpcLoginParams struct {
	WalletID string `json:"WalletID"`
}

type rpcLoginResult struct {
	Endpoint string  `json:"endpoint"`
}

func onWalletLogin(session* melody.Session, params *json.RawMessage) (result interface{}, err error) {
	wid, err := getValidWID(session)
	if err == nil {
		err = fmt.Errorf("already signed in, wallet id %v", wid)
		return
	}

	var loginParms  rpcLoginParams
	var loginResult rpcLoginResult

	if err = json.Unmarshal(*params, &loginParms); err != nil {
		return
	}

	log.Printf("wallet %v, rpc login request", loginParms.WalletID)
	loginResult.Endpoint, err = wallletServicesGet(loginParms.WalletID)
	if err != nil {
		return
	}

	session.Set(RpcKeyWID, loginParms.WalletID)
	result = &loginResult

	return
}
