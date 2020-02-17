package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/exec"
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

func loginRequest(w http.ResponseWriter, r *http.Request) {
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

	res.Endpoint = monitorGet(req.WalletID)
	if len(res.Endpoint) == 0 {
		if res.Endpoint, err = launchService(req.WalletID); err != nil {
			return
		}
		log.Printf("wallet %v, endpoint is %v, service launched", req.WalletID, res.Endpoint)
	} else {
		log.Printf("wallet %v, endpoint is %v, existing service", req.WalletID, res.Endpoint)
	}
}

// TODO: pass 'delete database' flag to the service if GUID is used instead of wallet ID
func launchService(wid string) (epoint string, err error) {
	ctxWS, cancelWS := context.WithCancel(context.Background())
	store(ctxWS, CtiWalletID, wid)

	cmd := exec.CommandContext(ctxWS, config.ServicePath, "-n", config.Node)
	ctxWS = store(ctxWS, CtiCommand, cmd)

	// TODO: save logs to WalletID:sessionID (timestamp) file
	// TODO: monitor and restart process
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	// Setup pipes
	pipeR, pipeW, err := os.Pipe()
	if err != nil {
		return
	}

	defer pipeR.Close()
	defer pipeW.Close()

	cmd.ExtraFiles = []*os.File {
		pipeW,
	}

	// Start wallet service
	log.Printf("wallet %v, starting wallet service [%v %v]", wid, config.ServicePath, "-n " + config.Node)
	if err = cmd.Start(); err != nil {
		return
	}
	_ = pipeW.Close()
	log.Printf("wallet %v, service pid is %v", wid, cmd.Process.Pid)

	//
	// Wait for the service spin-up & listening
	//
	presp, err := readPipe(pipeR)
	_ = pipeR.Close()

	if err != nil {
		cancelWS()
		err = fmt.Errorf("failed to read from sync pipe, %v", err)
		return
	}

	if "LISTENING" != presp {
		cancelWS()
		err = fmt.Errorf("failed to start wallet service. Pipe response: %v", presp)
		return
	}

	//
	// Everything is ok. Start monitoring and return result
	//
	epoint = "127.0.0.1:8080"
	monitorReg(wid, epoint, cmd, cancelWS)

	log.Printf("wallet %v, service successfully started, sync pipe response %v", wid, presp)
	return
}

func readPipe(pipe *os.File) (res string, err error) {
	if err = pipe.SetReadDeadline(time.Now().Add(LAUNCH_TIMEOUT)); err != nil {
		return
	}

	var dsize int
	data := make([]byte, 1024)
	dsize, err = pipe.Read(data)
	if err != nil {
		return
	}

	res = string(data[:dsize])
	return
}
