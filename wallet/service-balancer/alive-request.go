package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
)

type alive struct {
	WalletID string
}

type aliveR struct {
	Error string  `json:"error,omitempty"`
}

func aliveRequest(w http.ResponseWriter, r *http.Request) {
	allowCORS(w, r)

	if r.Method == "OPTIONS" {
		return
	}

	var req alive
	var res aliveR
	var err error

	defer func () {
		if err != nil {
			log.Printf("wallet %v, %v" , req.WalletID, err)
			res.Error = err.Error()
			w.WriteHeader(http.StatusInternalServerError)
		}

		encoder := json.NewEncoder(w)
		if jerr := encoder.Encode(res); jerr != nil {
			log.Printf("wallet %v, failed to encode alive result %v", req.WalletID, jerr)
		}
	} ()

	decoder := json.NewDecoder(r.Body)
	if err = decoder.Decode(&req); err != nil {
		return
	}

	if len(req.WalletID) == 0 {
		err = fmt.Errorf("alive, bad wallet id %v", req.WalletID)
		return
	}

	monitorAlive(req.WalletID)
}