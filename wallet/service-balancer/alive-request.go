package main

import (
	"encoding/json"
	"fmt"
	"github.com/olahol/melody"
	"net/http"
)

type alive struct {
	WalletID string
}

func aliveRequest(r *http.Request) (res interface{}, err error) {
	var req alive

	decoder := json.NewDecoder(r.Body)
	if err = decoder.Decode(&req); err != nil {
		return
	}

	if len(req.WalletID) == 0 {
		err = fmt.Errorf("alive request, bad wallet id %v", req.WalletID)
		return
	}

	err = monitorAlive(req.WalletID)
	return
}

func rpcAlive(session *melody.Session) (err error) {
	wid, err := getValidWID(session)
	if err != nil {
		return
	}

	err = monitorAlive(wid)
	return
}
