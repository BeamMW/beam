package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"github.com/olahol/melody"
)

type rpcRequest struct {
	Jsonrpc string           `json:"jsonrpc"`
	Id      *json.RawMessage `json:"id"`
	Method  string           `json:"method"`
	Params  *json.RawMessage `json:"params"`
}

type rpcResponse struct {
	Jsonrpc string           `json:"jsonrpc"`
	Id      *json.RawMessage `json:"id"`
	Result  *json.RawMessage `json:"result"`
}

const (
	RpcKeyWID = "wid"
)

func jsonRpcProcess(session *melody.Session, msg []byte) (response []byte) {
	var requestId  *json.RawMessage
	var requestResult interface{}

	var err error
	var errCode  = -32000

	defer func () {
		if err == nil {
			var resp = rpcResponse {
				Jsonrpc: "2.0",
				Id: requestId,
			}
			rawres, err := json.Marshal(requestResult)
			if err == nil {
				rawmsg := json.RawMessage(rawres)
				resp.Result = &rawmsg
				response, err = json.Marshal(resp)
			}
		}
		if err != nil {
			var rpcError = `{"jsonrpc":"2.0", id:nil, error: {code: %v, message: "%v"}`
			response = []byte(fmt.Sprintf(rpcError, errCode, err.Error()))
		}
	} ()

	var header rpcRequest
	if err = json.Unmarshal(msg, &header); err != nil {
		errCode = -32700
		return
	}

	requestId = header.Id
	if header.Jsonrpc != "2.0" {
		err = fmt.Errorf("bad jsonrpc version %v", header.Jsonrpc)
		errCode = -32600
		return
	}

	if header.Params == nil {
		err = errors.New("bad jsonrpc, params are nil")
		errCode = -32600
		return
	}

	if header.Method == "login" {
		requestResult, err = rpcLoginRequest(session, header.Params)
		return
	}

	if header.Method == "logout" {
		requestResult, err = rpcLogoutRequest(session, header.Params)
		return
	}

	err = fmt.Errorf("method not found")
	errCode = -32601
	return
}


