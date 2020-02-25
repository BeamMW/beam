package main

import (
	"log"
	"sync"
	"sync/atomic"
	"time"
)

type Endpoint struct {
	clientsCnt   int32
	service      int
	address      string
	WalletAlive  chan bool
	WalletLogout chan bool
	Dropped      chan struct{}
}

func (epoint *Endpoint) Use() (service int, address string) {
	atomic.AddInt32(&epoint.clientsCnt, 1)
	epoint.WalletAlive <- true
	return epoint.service, epoint.address
}

func (epoint *Endpoint) Release(wid string) int {
	cnt := atomic.AddInt32(&epoint.clientsCnt, -1)
	if cnt < 0 {
		log.Fatalf("wallet %v, negative clients count on endpoint, ccnt %v", wid, cnt)
	}
	return int(cnt)
}

func (epoint *Endpoint) GetClientsCnt() int {
	cnt := atomic.LoadInt32(&epoint.clientsCnt)
	return int(cnt)
}

func (epoint *Endpoint) GetServiceIdx() int {
	return epoint.service
}

type Endpoints struct {
	all   map[string] *Endpoint
	mutex sync.Mutex
}

func (points *Endpoints) DropServiceEndpoints (svcIdx int) (pointsCnt int, clientsCnt int) {
	points.mutex.Lock()
	defer points.mutex.Unlock()

	for key, epoint := range points.all {
		if epoint.service == svcIdx {
			pointsCnt  += 1
			clientsCnt += epoint.GetClientsCnt()
			close(epoint.Dropped)
			delete(points.all, key)
		}
	}

	return
}

func (points *Endpoints) Get(wid string) (*Endpoint, bool) {
	points.mutex.Lock()
	defer points.mutex.Unlock()

	epoint, ok := points.all[wid]
	return epoint, ok
}

func (points *Endpoints) Add(wid string, svcIdx int, address string) {
	points.mutex.Lock()
	defer points.mutex.Unlock()

	//
	// Endpoint might be already created by another thread
	// This should be a very very rare situation
	//
	if epoint, ok := points.all[wid]; ok {
		epoint.Use()
		log.Printf("wallet %v, add on exisiting endpoint [%v:%v]", wid, svcIdx, address)
		return
	}

	var epoint = &Endpoint{
		clientsCnt:   1,
		service:      svcIdx,
		address:      address,
		WalletAlive:  make(chan bool),
		WalletLogout: make(chan bool),
		Dropped:      make(chan struct{}),
	}
	points.all[wid] = epoint

	//
	//  Monitor the new endpoint
	//
	go func () {
		timeout := time.NewTimer(config.EndpointAliveTimeout)

		var releaseEndpoint = func () {
			points.mutex.Lock()
			defer points.mutex.Unlock()

			if clients := epoint.Release(wid); clients == 0 {
				delete(points.all, wid)
				timeout.Stop()
				log.Printf("wallet %v, endpoint removed", wid)
			} else {
				log.Printf("wallet %v, client removed, %v client(s) left", wid, clients)
			}
		}

		for {
			select {
			case <- timeout.C:
				// No alive signals from wallet for some time. Usually this means
				// that connection to the web wallet has been lost and we need to shutdown this endpoint
				log.Printf("wallet %v, endpoint timeout", wid)
				releaseEndpoint()
				return

			case <- epoint.WalletAlive:
				// This means that alive ping has been received
				// Timeout should be restarted
				if config.NoisyLogs {
					log.Printf("wallet %v, web wallet is alive, restarting alive timeout", wid)
				}
				timeout = time.NewTimer(config.EndpointAliveTimeout)

			case <- epoint.WalletLogout:
				// This means that web wallet notified us about exit
				// Need to shutdown this endpoint
				log.Printf("wallet %v, WalletLogout signal", wid)
				// TODO: consider releasing endpoint after some time, may be client would come again
				releaseEndpoint()
				return

			case <- epoint.Dropped:
				log.Printf("wallet %v, endpoint dropped, clients %v, service %v", wid, epoint.GetClientsCnt(), epoint.GetServiceIdx())
				timeout.Stop()
				return
			}
		}
	}()
	return
}

var (
	epoints = &Endpoints{
		all:   make(map[string] *Endpoint),
		mutex: sync.Mutex{},
	}
)
