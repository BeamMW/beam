package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"sync"
	"time"
)

type service struct {
	Address     string
	Command     *exec.Cmd
	CancelCmd   context.CancelFunc
	Context     context.Context
	ServiceExit chan struct{}
}

func (svc* service) Shutdown() {
	close(svc.ServiceExit)
	svc.CancelCmd()
}

// TODO: RW Mutex
// TODO: make struct and private members
var svcMutex = &sync.Mutex{}
var services []*service // TODO: consider raw struct
var serviceDropped = make(chan int)

const (
	MaxServices = 2
	MinServices = 2
	EndpointAliveTimeout = 20 * time.Second
	ServiceAliveTimeout  = 10 * time.Second
)

func monitor2Initialize() (err error) {
	if MinServices < 2 {
		log.Fatalf("MinServices %v, is less than 2. Code assumes having at least 2 wallet services", MinServices)
	}

	var cpus = runtime.NumCPU()
	var svcs = MaxServices

	if svcs > cpus {
		svcs = cpus
	}

	if svcs < MinServices {
		svcs = MinServices
	}

	log.Printf("initializing wallet services, CPU count %v, service count %v", cpus, svcs)

	services = make([]*service, cpus)
	defer func () {
		if err != nil {
			for i := 0; i < len(services); i++ {
				if services[i] != nil && services[i].CancelCmd != nil {
					log.Printf("serivce index %v, closing", i)
					services[i].Shutdown()
				}
			}
		}
	} ()

	for i := 0; i < svcs; i++ {
		service, launchErr := launchService2(i)
		if launchErr != nil {
			err = launchErr
			return
		}

		services[i] = service
		var svcIdx = i

		var checkAlive = func () {

		}

		go checkAlive()

		go func () {
			// TODO: consider moving to service.Command.Run()
			_ = service.Command.Wait()
			close(service.ServiceExit)
			log.Printf("service index %v, service stopped. Exited %v, exit code %v", svcIdx,
				service.Command.ProcessState.Exited(),
				service.Command.ProcessState.ExitCode())
		} ()

		go func () {
			// TODO: implement graceful exit (check if necessary)
			for {
				select {
				case svcIdx := <- serviceDropped:
					epointsMutex2.Lock()
					var dPoints = 0
					var dClients int32 = 0
					for key, epoint:= range epoints2 {
						if epoint.Service == svcIdx {
							dPoints += 1
							dClients += epoint.ClientsCnt
							close(epoint.Dropped)
							delete(epoints2, key)
						}
					}
					epointsMutex2.Unlock()
					log.Printf("service index %v, dropped %v endpoint(s) with %v client(s)", svcIdx, dPoints, dClients)
				}
			}
		} ()

		go func() {
			var serviceExit = service.ServiceExit
			// TODO: consider implementing alive check
			// aliveTimeout := time.NewTimer(ServiceAliveTimeout)


			var shutdown = func (exitProcess bool) {
				serviceExit = nil
				if exitProcess {
					service.Shutdown()
				}

				svcMutex.Lock()
				services[svcIdx] = nil
				svcMutex.Unlock()
				serviceDropped <- svcIdx
			}

			for {
				select {
				/*
				case <- aliveTimeout.C:
					// No alive signals from service for some time. Usually this means
					// that connection to the service has been lost and we need to restart it
					log.Printf("service index %v, alive timeout", svcIdx)
					shutdown(true)

				case <- service.ServiceAlive:
					// This means that alive ping has been received
					// timeout timer should be restarted
					// TODO:DEBUG LOG
					log.Printf("service index %v, service is alive, restarting alive timeout", svcIdx)
					aliveTimeout = time.NewTimer(ServiceAliveTimeout)
				*/

				case <- service.ServiceExit:
					// TODO: test what happens with this on restart. Should be OK (parent killed and then all childs)
					// TODO: consider implementing service restart. This might be not so trivial
					//
					// Now we just drop all endpoints for this service and continue without it
					// TODO: If there are 0 services left - panic
					// TODO: consider restart on the same port, Do not drop endpoints then, bad clients might try to reconnect.
					// TODO: consider restart on new port, drop endpoints. This seems to be better solution (?)
					//
					// This means that something really bad happened.
					// May be wallet service has been killed by OOM/admin/other process
					log.Printf("service index %v, unexpected shutdown [%v]", svcIdx, service.Command.ProcessState)
					shutdown(false)
					return
				}
			}
		}()
	}

	return
}

// TODO: pass 'delete database' flag to the service if GUID is used instead of wallet ID
func launchService2(index int) (*service, error) {
	var svc  = &service{}
	var port = config.ServiceFirstPort + index

	ctxWS, cancelWS := context.WithCancel(context.Background())
	svc.CancelCmd   = cancelWS
	svc.Context     = ctxWS
	svc.Command     = exec.CommandContext(ctxWS, config.ServicePath, "-n", config.BeamNode, "-p", strconv.Itoa(port))
	svc.Address     = config.SerivcePublicAddress + ":" + strconv.Itoa(port)
	svc.ServiceExit = make(chan struct{})

	// TODO: reconsider logs management
	// TODO: monitor and restart process
	svc.Command.Stdout = os.Stdout
	svc.Command.Stderr = os.Stderr

	// Setup pipes
	pipeR, pipeW, err := os.Pipe()
	if err != nil {
		return nil, err
	}

	defer pipeR.Close()
	defer pipeW.Close()

	svc.Command.ExtraFiles = []*os.File {
		pipeW,
	}

	// Start wallet service
	log.Printf("service index %v, starting as [%v %v %v %v]", index, config.ServicePath, "-n " + config.BeamNode, "-p", port)
	if err = svc.Command.Start(); err != nil {
		return nil, err
	}
	_ = pipeW.Close()
	log.Printf("service index %v, pid is %v", index, svc.Command.Process.Pid)

	//
	// Wait for the service spin-up & listening
	//
	presp, err := readPipe(pipeR)
	_ = pipeR.Close()

	if err != nil {
		cancelWS()
		err = fmt.Errorf("service index %v, failed to read from sync pipe, %v", index, err)
		return nil, err
	}

	if "LISTENING" != presp {
		cancelWS()
		err = fmt.Errorf("service index %v, failed to start. Wrong pipe response %v", index, presp)
		return nil, err
	}

	//
	// Everything is OK
	//
	// TODO:2Start monitoring services, restart on close
	log.Printf("service index %v, successfully started, sync pipe response %v", index, presp)
	return svc, nil
}

// WalletID -> service index
type endPoint2 struct {
	ClientsCnt   int32
	Service      int
	Address      string
	WalletAlive  chan bool
	WalletLogout chan bool
	Dropped      chan struct{}
}

// TODO: make struct and private members
var (
	epoints2      = make(map[string]*endPoint2)
	epointsMutex2 = &sync.Mutex{} // TODO: RW Mutex
	nextService   = 0
)

func monitor2Get(wid string) string {
	epointsMutex2.Lock()
	defer epointsMutex2.Unlock()

	if epoint, ok := epoints2[wid]; ok {
		epoint.ClientsCnt++
		epoint.WalletAlive <- true
		log.Printf("wallet %v, existing endpoint is [%v:%v]", wid, epoint.Service, epoint.Address)
		return epoint.Address
	}

	//
	// Choose service, some might be null
	//
	var initialIdx = nextService
	var svcIdx     = nextService
	var address    = ""

	for len(address) == 0  {
		svcIdx = nextService
		nextService++

		if nextService == MaxServices {
			nextService = 0
		}

		svcMutex.Lock()
		var service = services[svcIdx]
		svcMutex.Unlock()

		// TODO: service might be not always online, check additional check here for process status?
		if service == nil {
			if nextService == initialIdx {
				// This is really bad and MUST never happen in normal flow
				log.Fatalf("no active services found. Halting")
			}
			continue
		}

		address = services[svcIdx].Address
	}

	var epoint = &endPoint2{
		ClientsCnt:   1,
		Service:      svcIdx,
		Address:      address,
		WalletAlive:  make(chan bool),
		WalletLogout: make(chan bool),
		Dropped:      make(chan struct{}),
	}
	epoints2[wid] = epoint

	go func () {
		timeout := time.NewTimer(EndpointAliveTimeout)

		var shutdownEndpoint = func () {
			// TODO: consider using atomic for RefCnt management
			epointsMutex2.Lock()
			defer epointsMutex2.Unlock()

			// TODO: test RefCnt
			epoint.ClientsCnt--
			if epoint.ClientsCnt < 0 {
				log.Fatalf("wallet %v, negative clients count on endpoint, ccnt %v", wid, epoint.ClientsCnt)
			}

			// TODO: inform wallet service about closed wallet?
			delete(epoints2, wid)
			timeout.Stop()
		}

		for {
			select {
			case <- timeout.C:
				// No alive signals from wallet for some time. Usually this means
				// that connection to the web wallet has been lost and we need to shutdown this endpoint
				log.Printf("wallet %v, endpoint timeout", wid)
				shutdownEndpoint()
				return

			case <- epoint.WalletAlive:
				// This means that alive ping has been received
				// Timeout should be restarted
				// TODO:DEBUG LOG
				log.Printf("wallet %v, web wallet is alive, restarting alive timeout", wid)
				timeout = time.NewTimer(EndpointAliveTimeout)

			case <- epoint.WalletLogout:
				// This means that web wallet notified us about exit
				// Need to shutdown this endpoint
				log.Printf("wallet %v, logout request", wid)
				shutdownEndpoint()
				return

			case <- epoint.Dropped:
				// Usually this happens when service exits unexpectedly
				// Just do nothing and leave
				log.Printf("wallet %v, endpoint dropped, clients %v, service index", epoint.ClientsCnt, epoint.Service)
				timeout.Stop()
				return
			}
		}
	}()

	log.Printf("wallet %v, new endpoint is [%v:%v]", wid, svcIdx, address)
	return address
}

func monitor2Alive (wid string) {
	epointsMutex2.Lock()
	defer epointsMutex2.Unlock()

	if epoint, ok := epoints2[wid]; ok {
		epoint.WalletAlive <- true
	} else {
		// This usually means issue in the web wallet code
		log.Printf("wallet %v, alive request on missing endpoint", wid)
	}
}

func monitor2Logout(wid string) {
	epointsMutex2.Lock()
	defer epointsMutex2.Unlock()

	if epoint, ok := epoints2[wid]; ok {
		epoint.WalletLogout <- true
	} else {
		// This usually means issue in the web wallet code
		log.Printf("wallet %v, logout request on missing endpoint", wid)
	}
}