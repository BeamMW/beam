package main

import (
	"encoding/json"
	"fmt"
	"github.com/olahol/melody"
	"log"
	"net/http"
	"os"
	"time"
)

type login struct {
	WalletID string
}

type loginR struct {
	Endpoint string  `json:"endpoint,omitempty"`
	Error    string  `json:"error,omitempty"`
}

func readPipe(pipe *os.File, timeout time.Duration) (res string, err error) {
	if timeout != 0 {
		if err = pipe.SetReadDeadline(time.Now().Add(timeout)); err != nil {
			return
		}
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

type rpcLoginParams struct {
	WalletID string `json:"WalletID"`
}

type rpcLoginResult struct {
	Endpoint string  `json:"endpoint"`
}

func rpcLoginRequest(session* melody.Session, params *json.RawMessage) (result interface{}, err error) {
	var loginParms  rpcLoginParams
	var loginResult rpcLoginResult

	if err = json.Unmarshal(*params, &loginParms); err != nil {
		return
	}

	loginResult.Endpoint, err = monitorGet(loginParms.WalletID)
	if err != nil {
		return
	}

	log.Printf("wallet %v, rpc login request", loginParms.WalletID)
	session.Set(RpcKeyWID, loginParms.WalletID)
	result = &loginResult

	return
}

