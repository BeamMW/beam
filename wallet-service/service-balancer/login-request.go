package main

import (
	"encoding/json"
	"fmt"
	"github.com/olahol/melody"
	"log"
)

type loginParams struct {
	WalletID string `json:"WalletID"`
}

type loginResult struct {
	Endpoint string  `json:"endpoint"`
}

func onWalletLogin(session* melody.Session, params *json.RawMessage) (result interface{}, err error) {
	wid, err := getValidWID(session)
	if err == nil {
		err = fmt.Errorf("already signed in, wallet id %v", wid)
		return
	}

	var par loginParams
	var res loginResult

	if err = json.Unmarshal(*params, &par); err != nil {
		return
	}

	if config.Debug {
		log.Printf("wallet %v, rpc login request", par.WalletID)
	}

	res.Endpoint, err = wallletServicesGet(par.WalletID)
	if err != nil {
		return
	}

	session.Set(RpcKeyWID, par.WalletID)
	result = &res

	return
}
