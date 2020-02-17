package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"time"
)

const (
	LAUNCH_TIMEOUT = 5 * time.Second
)

type login struct {
	WalletID string
}

type loginR struct {
	Endpoint string  `json:"endpoint,omitempty"`
	Error    string  `json:"error,omitempty"`
}

func readPipe(pipe *os.File) (res string, err error) {
	if err = pipe.SetReadDeadline(time.Now().Add(LAUNCH_TIMEOUT)); err != nil {
		return
	}

	var dsize int
	var data = make([]byte, 100)
	dsize, err = pipe.Read(data)
	if err != nil {
		return
	}

	res = string(data[:dsize])
	return
}

func loginRequest2(w http.ResponseWriter, r *http.Request) {
	allowCORS(w ,r)

	if r.Method == "OPTIONS" {
		return
	}

	var req login
	var res loginR
	var err error

	defer func () {
		if err != nil {
			log.Printf("wallet %v, %v" , req.WalletID, err)
			res.Endpoint = ""
			res.Error = err.Error()
			w.WriteHeader(http.StatusInternalServerError)
		}

		encoder := json.NewEncoder(w)
		if jerr := encoder.Encode(res); jerr != nil {
			log.Printf("wallet %v, failed to encode login result %v", req.WalletID, jerr)
		}
	} ()

	decoder := json.NewDecoder(r.Body)
	if err = decoder.Decode(&req); err != nil {
		return
	}

	if len(req.WalletID) == 0 {
		err = fmt.Errorf("login, bad wallet id %v", req.WalletID)
		return
	}

	res.Endpoint = monitor2Get(req.WalletID)
	if len(res.Endpoint) == 0 {
		// THIS SHOULD NEVER HAPPEN
		log.Fatalf("wallet %v, failed to get an endpoint", req.WalletID)
		return
	}
}
