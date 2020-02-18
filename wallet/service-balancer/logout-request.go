package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
)

type logout struct {
	WalletID string
}

type logoutR struct {
	Error string  `json:"error,omitempty"`
}

func logoutRequest2(w http.ResponseWriter, r *http.Request) {
	allowCORS(w, r)

	if r.Method == "OPTIONS" {
		return
	}

	var req logout
	var res logoutR
	var err error

	defer func () {
		if err != nil {
			log.Printf("wallet %v, %v" , req.WalletID, err)
			res.Error = err.Error()
			w.WriteHeader(http.StatusInternalServerError)
		}

		encoder := json.NewEncoder(w)
		if jerr := encoder.Encode(res); jerr != nil {
			log.Printf("wallet %v, failed to encode close result %v", req.WalletID, jerr)
		}
	} ()

	decoder := json.NewDecoder(r.Body)
	if err = decoder.Decode(&req); err != nil {
		return
	}

	if len(req.WalletID) == 0 {
		err = fmt.Errorf("logout, bad wallet id %v", req.WalletID)
		return
	}

	monitor2Logout(req.WalletID)
}