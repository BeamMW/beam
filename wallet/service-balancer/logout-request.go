package main

import (
	"encoding/json"
	"fmt"
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
