package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
)

type clse struct {
	WalletID string
}

type clseR struct {
	Error string  `json:"error,omitempty"`
}

func closeRequest(w http.ResponseWriter, r *http.Request) {
	allowCORS(w, r)

	if r.Method == "OPTIONS" {
		return
	}

	var req clse
	var res clseR
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
		err = fmt.Errorf("close, bad wallet id %v", req.WalletID)
		return
	}

	monitorClose(req.WalletID)
}
