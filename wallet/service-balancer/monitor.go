package main

import (
	"context"
	"log"
	"os/exec"
	"sync"
	"time"
)

type endPpoint struct {
	Address      string
	Command      *exec.Cmd
	CancelCmd    context.CancelFunc
	WalletAlive  chan bool
	WalletClose  chan bool
	ServiceExit  chan struct{}
}

func (epoint *endPpoint) Shutdown () {
	epoint.CancelCmd()
}

const (
	// TODO: discuss real interval, now value is set only for testing
	AliveTimeout = 20 * time.Second
)

var epointsMutex = &sync.Mutex{}

// TODO: may be store endPpoint-s by pointer
var epoints = make(map[string]endPpoint)

func monitorInitialize () error {
	return nil
}

func monitorReg (wid string, address string, cmd* exec.Cmd, cancelCmd context.CancelFunc) {
	epointsMutex.Lock()
	defer epointsMutex.Unlock()

	var shutdownEpoint = func (lock bool) {
		if lock {
			epointsMutex.Lock()
		}

		if epoint, ok := epoints[wid]; ok {
			// TODO: send SIGINT signal. Now hard kill
			// TODO: consider using 'release' api
			epoint.Shutdown()
			delete(epoints, wid)
			log.Printf("wallet %v, endpoint removed", wid)
		} else {
			log.Printf("wallet %v, shutdownEpoint requested but it has been already removed", wid)
		}

		if lock {
			epointsMutex.Unlock()
		}
	}

	if _, ok := epoints[wid]; ok {
		shutdownEpoint(false)
	}

	var epoint = endPpoint{}
	epoint.WalletAlive = make(chan bool)
	epoint.WalletClose = make(chan bool)
	epoint.ServiceExit = make(chan struct{})
	epoint.Address     = address
	epoint.Command     = cmd
	epoint.CancelCmd   = cancelCmd
	epoints[wid]       = epoint

	go func () {
		var serviceExit = epoint.ServiceExit
		timeout := time.NewTimer(AliveTimeout)

		var shutdown = func () {
			serviceExit = nil
			shutdownEpoint(true)
		}

		for {
			select {
			case <- timeout.C:
				//
				// Service has been running in background for a while no alive signals came
				// from web wallet. Usually this means that connection to the web wallet
				// has been lost and we need to shutdown this endpoint and terminate this monitor
				//
				log.Printf("wallet %v, alive timeout", wid)
				shutdown()
				return

			case <- epoint.WalletAlive:
				// This means that alive ping has been received
				// timeout timer should be restarted
				log.Printf("wallet %v, web wallet is alive, restarting alive timeout", wid)
				timeout = time.NewTimer(AliveTimeout)

			case <- epoint.WalletClose:
				// This means that web wallet notified us about exit
				// Need to shutdown this endpoint and terminate this monitor
				log.Printf("wallet %v, close request", wid)
				shutdown()
				return

			case <- epoint.ServiceExit:
				// This means that something really bad happened to the wallet service
				// May be it has been killed. Shutdown this endpoint and monitor
				// TODO: consider implementing service restart. This might be not so trivial
				log.Printf("wallet %v, unexpected wallet service shutdown %v", wid, cmd.ProcessState)
				shutdown()
				return
			}
		}
	}()

	go func () {
		_ = cmd.Wait()
		close(epoint.ServiceExit)
		log.Printf("wallet %v, wallet service stopped. Exited %v, exit code %v", wid, cmd.ProcessState.Exited(), cmd.ProcessState.ExitCode())
	} ()

	// TODO: send alive ping to the service
	// TODO: handle termination
}

func monitorAlive (wid string) {
	epointsMutex.Lock()
	defer epointsMutex.Unlock()

	if epoint, ok := epoints[wid]; ok {
		epoint.WalletAlive <- true
	}
}

func monitorGet(wid string) string {
	epointsMutex.Lock()
	defer epointsMutex.Unlock()

	if epoint, ok := epoints[wid]; ok {
		epoint.WalletAlive <- true
		return epoint.Address
	} else {
		// This usually means issue in the web wallet code
		log.Printf("wallet %v, alive request on missing endpoint", wid)
	}

	return ""
}

func monitorClose(wid string) {
	epointsMutex.Lock()
	defer epointsMutex.Unlock()

	if epoint, ok := epoints[wid]; ok {
		epoint.WalletClose <- true
	} else {
		// This usually means issue in the web wallet code
		log.Printf("wallet %v, close request on missing endpoint", wid)
	}
}
