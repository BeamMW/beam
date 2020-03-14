package main

import (
	"beam.mw/service-balancer/services"
	"beam.mw/service-balancer/wsclient"
	"encoding/json"
	"errors"
	"log"
	"strconv"
	"sync"
	"time"
)

type SbbsMonitor struct {
	Dropped     chan struct{}
	Subscribe   chan *SubParams
	Unsubscribe chan *UnsubParams
}

type SbbsMonitors struct {
	monitor *SbbsMonitor
	mutex   sync.Mutex
}

var (
	monitors = &SbbsMonitors{
		monitor: nil,
		mutex:   sync.Mutex{},
	}
)

func (mons *SbbsMonitors) DropMonitor(sbbsIdx int) {
	mons.mutex.Lock()
	defer mons.mutex.Unlock()

	close(mons.monitor.Dropped)
	mons.monitor = nil
}

func (mons *SbbsMonitors) ListenMonitor(sbbsIdx int) error {
	mons.mutex.Lock()
	defer mons.mutex.Unlock()

	service, err := sbbsServices.GetAt(sbbsIdx)
	if err != nil {
		return err
	}

	var address = "127.0.0.1:" + strconv.Itoa(service.Port)
	wsc, err := wsclient.NewWSClient(address, "/")
	if err != nil {
		return err
	}

	var wsGenericError = "websocket bbs client processing error, %v"
	wsc.HandleError(func (client *wsclient.WSClient, err error){
		counters.CountBbsError()
		log.Printf(wsGenericError, err)
	})

	wsc.HandleMessage(func (client *wsclient.WSClient, message [] byte){
		go func() {
			if resp := jsonRpcProcessBbs(client, message); resp != nil {
				client.Write(resp)
			}
		} ()
	})

	var monitor = &SbbsMonitor{
		Dropped:      make(chan struct{}),
		Subscribe:    make(chan *SubParams),
		Unsubscribe:  make(chan *UnsubParams),
	}
	mons.monitor = monitor

	if config.Debug {
		log.Printf("bbs %v, listening to at %v", sbbsIdx, address)
	}

	go func () {
		for {
			select {
			case req := <- monitor.Subscribe:
				if config.Debug {
					log.Printf("bbs %v, subscribe %v, %v", sbbsIdx, req.SbbsAddress, req.SbbsAddressPrivate)
				}
				go func () {
					err := sbbsMonitorSubscribe(wsc, req.SbbsAddress, req.SbbsAddressPrivate, req.ExpiresAt)
					if err != nil {
						log.Printf("bbs %v, failed to subscribe %v", sbbsIdx, err)
					}
				}()

			case req := <- monitor.Unsubscribe:
				if config.Debug {
					log.Printf("bbs %v, unsubscribe %v, %v", sbbsIdx, req.SbbsAddress, req.SbbsAddressPrivate)
				}
				go func () {
					err := sbbsMonitorUnsubscribe(wsc, req.SbbsAddress, req.SbbsAddressPrivate)
					if err != nil {
						log.Printf("bbs %v, failed to unsubscribe %v", sbbsIdx, err)
					}
				}()

			case <- monitor.Dropped:
				log.Printf("bbs %v, exit & leave", sbbsIdx)
				wsc.Stop()
				return
			}
		}
	}()

	go func() {
		err := forAllSubs(func(entry *Subscription) {
			err := sbbsMonitorSubscribe(wsc, entry.SbbsAddress, entry.SbbsAddressPrivate, entry.ExpiresAt)
			if err != nil {
				log.Printf("sbbsMonitorSubscribe error %v", err)
			}
		})
		if err != nil {
			log.Printf("forAllSubs error %v", err)
		}
	}()

	return nil
}

var (
	sbbsServices *services.Services
)

func sbbsMonitorInitialize() (err error) {
	if err = sbbsDBInit(); err != nil {
		return
	}

	sbbsServices, err = NewBbsServices()
	if err != nil {
		return
	}

	go func () {
		for {
			select {
			case svcIdx := <- sbbsServices.Dropped:
				counters.CountBBSDrop()
				log.Printf("bbs %v, dropped", svcIdx)
				monitors.DropMonitor(svcIdx)

			case svcIdx := <- sbbsServices.Restarted:
				log.Printf("bbs %v, restarted", svcIdx)
				if err = monitors.ListenMonitor(svcIdx); err != nil {
					log.Fatalf("bbs %v, failed to start listening", err)
					return
				}
			}
		}
	} ()

	if err = monitors.ListenMonitor(0); err != nil {
		return
	}

	return
}

func sbbsMonitorSubscribe(wsc *wsclient.WSClient, address string, private string, expires int64) error {
	params, err := json.Marshal(map[string]interface{}{
		"address":    address,
		"privateKey": private,
		"expires":    expires,
	})

	if err != nil {
		return err
	}

	rawParams := json.RawMessage(params)
	rawid := json.RawMessage([]byte(`"SUB-` + address + `"`))

	jsonv := rpcRequest {
		Jsonrpc: "2.0",
		Id:      &rawid,
		Method:  "subscribe",
		Params:  &rawParams,
	}

	wsc.Write(jsonv)
	return nil
}

func sbbsMonitorUnsubscribe(wsc *wsclient.WSClient, address string, private string) error {
	params, err := json.Marshal(map[string]interface{}{
		"address": address,
		"privateKey": private,
	})

	if err != nil {
		return err
	}

	rawParams := json.RawMessage(params)
	rawid := json.RawMessage([]byte(`"UNSUB-` + address + `"`))

	jsonv := rpcRequest {
		Jsonrpc: "2.0",
		Id:      &rawid,
		Method:  "unsubscribe",
		Params:  &rawParams,
	}

	wsc.Write(jsonv)
	return nil
}

func monitorSubscribe(params *SubParams) error {
	if params.ExpiresAt <= time.Now().Unix() {
		return errors.New("subscrption is already expired")
	}

	if err := storeSub(params); err != nil {
		return err
	}

	monitors.mutex.Lock()
	defer monitors.mutex.Unlock()
	monitors.monitor.Subscribe <- params

	return nil
}

func monitorUnsubscribe(params *UnsubParams) error {
	if err := removeSub(params); err != nil {
		return err
	}

	monitors.mutex.Lock()
	defer monitors.mutex.Unlock()
	monitors.monitor.Unsubscribe <- params

	return nil
}
