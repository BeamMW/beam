package main

import (
	"encoding/json"
	"fmt"
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

func loginRequest(w http.ResponseWriter, r *http.Request) (interface{}, error){
	var req login
	var res loginR

	decoder := json.NewDecoder(r.Body)
	if err := decoder.Decode(&req); err != nil {
		return nil, err
	}

	if len(req.WalletID) == 0 {
		return nil, fmt.Errorf("login, bad wallet id %v", req.WalletID)
	}

	epoint, err := monitorGet(req.WalletID)
	if err != nil {
		return nil, err
	}

	res.Endpoint = epoint
	return res, nil
}
