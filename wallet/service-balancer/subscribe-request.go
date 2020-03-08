package main

import (
	"encoding/json"
	"errors"
	"github.com/olahol/melody"
	"log"
)

type rpcSubscribeParams struct {
	SbbsAddress          string `json:"SbbsAddress"`
	SbbsAddressPrivate   string `json:"SbbsAddressPrivate"`
	NotificationEndpoint string `json:"NotificationEndpoint"`
	ServerKey            string `json:"ServerKey"`
}

func rpcSubscribeRequest(session* melody.Session, params *json.RawMessage) (result interface{}, err error) {
	wid, err := getValidWID(session)
	if err != nil {
		return
	}

	log.Printf("wallet %v, rpc subscribe request", wid)

	var sub rpcSubscribeParams
	if err = json.Unmarshal(*params, &sub); err != nil {
		return
	}

	if len(sub.SbbsAddress) == 0 {
		return nil, errors.New("invalid SBBS address")
	}

	if len(sub.SbbsAddressPrivate) == 0 {
		return nil, errors.New("invalid SBBS key")
	}

	if len(sub.NotificationEndpoint) == 0 {
		// TODO: check for valid https:// 
		return nil, errors.New("invalid notification endpoint")
	}

	if sub.ServerKey != config.VAPIDPublic {
		return nil, errors.New("server key mismatch")
	}

	//var, err = monitorGet(loginParms.WalletID)
	//if err != nil {
	//	return
	//}

	//session.Set(RpcKeyWID, loginParms.WalletID)
	///result = &loginResult
	// Send test notification
	go func() {

	}()

	return
}
