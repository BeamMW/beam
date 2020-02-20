package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"sync"
)

//
// Single wallet service
//
type Service struct {
	Address     string
	command     *exec.Cmd
	cancelCmd   context.CancelFunc
	context     context.Context
	ServiceExit chan struct{}
}

func (svc* Service) Shutdown() {
	close(svc.ServiceExit)
	svc.cancelCmd()
}

func NewService(index int) (*Service, error) {
	var svc  = Service{}
	var port = config.ServiceFirstPort + index

	ctxWS, cancelWS := context.WithCancel(context.Background())
	svc.cancelCmd   = cancelWS
	svc.context     = ctxWS
	svc.command     = exec.CommandContext(ctxWS, config.ServicePath, "-n", config.BeamNode, "-p", strconv.Itoa(port))
	svc.Address     = config.SerivcePublicAddress + ":" + strconv.Itoa(port)
	svc.ServiceExit = make(chan struct{})

	if config.Debug {
		svc.command.Stdout = os.Stdout
		svc.command.Stderr = os.Stderr
	}

	// Setup pipes
	pipeR, pipeW, err := os.Pipe()
	if err != nil {
		return nil, err
	}

	defer pipeR.Close()
	defer pipeW.Close()

	svc.command.ExtraFiles = []*os.File {
		pipeW,
	}

	// Start wallet service
	log.Printf("service %v, starting as [%v %v %v %v]", index, config.ServicePath, "-n " + config.BeamNode, "-p", port)
	if err = svc.command.Start(); err != nil {
		return nil, err
	}
	_ = pipeW.Close()
	log.Printf("service %v, pid is %v", index, svc.command.Process.Pid)

	//
	// Wait for the service spin-up & listening
	//
	presp, err := readPipe(pipeR)
	_ = pipeR.Close()

	if err != nil {
		cancelWS()
		err = fmt.Errorf("service %v, failed to read from sync pipe, %v", index, err)
		return nil, err
	}

	if "LISTENING" != presp {
		cancelWS()
		err = fmt.Errorf("service %v, failed to start. Wrong pipe response %v", index, presp)
		return nil, err
	}

	//
	// Everything is OK
	//
	go func () {
		_ = svc.command.Wait()
		close(svc.ServiceExit)
	} ()

	log.Printf("service %v, successfully started, sync pipe response %v", index, presp)
	return &svc, nil
}

//
// Services collection
//
const (
	MaxServices = 2
	MinServices = 2
)

type Services struct  {
	mutex     sync.Mutex
	all       []*Service
	Dropped   chan int
	next      int
}

func (svcs* Services) GetNext() (int, *Service, error) {
	svcs.mutex.Lock()
	defer svcs.mutex.Unlock()

	var initialIdx = svcs.next
	var svcIdx     = svcs.next

	for  {
		svcIdx = svcs.next
		svcs.next++

		if svcs.next == MaxServices {
			svcs.next = 0
		}

		var service = svcs.all[svcIdx]
		if service == nil {
			if svcs.next == initialIdx {
				// This is really bad and MUST never happen in normal flow
				return 0, nil, errors.New("no active services found")
			}
			continue
		}

		return svcIdx, service, nil
	}
}

func (svcs *Services) ShutdownAll () {
	svcs.mutex.Lock()
	defer svcs.mutex.Unlock()

	for i := 0; i < len(svcs.all); i++ {
		if svcs.all[i] != nil && svcs.all[i].cancelCmd != nil {
			log.Printf("serivce %v, closing", i)
			svcs.all[i].Shutdown()
		}
	}
}

func (svcs *Services) Drop (svcIdx int) {
	svcs.mutex.Lock()
	defer svcs.mutex.Unlock()

	svcs.all[svcIdx] = nil
	svcs.Dropped <- svcIdx
}

func (svcs* Services) Get (svcIdx int) *Service {
	svcs.mutex.Lock()
	defer svcs.mutex.Unlock()

	return svcs.all[svcIdx]
}

func NewServices() (result *Services, err error) {
	defer func () {
		if err != nil {
			if result != nil {
				result.ShutdownAll()
			}
		}
	}()

	if MinServices < 2 {
		return nil, fmt.Errorf("MinServices %v is less than 2. Code assumes having at least 2 wallet services", MinServices)
	}

	var cpus = runtime.NumCPU()
	var svcsCnt = MinServices

	if !config.Debug {
		svcsCnt = MaxServices

		if svcsCnt > cpus {
			svcsCnt = cpus
		}

		if svcsCnt < MinServices {
			svcsCnt = MinServices
		}
	}

	log.Printf("initializing wallet services, CPU count %v, service count %v", cpus, svcsCnt)
	result = &Services{
		mutex:     sync.Mutex{},
		all:       make([]*Service, svcsCnt),
		Dropped:   make(chan int),
	}

	for i := 0; i < svcsCnt; i++ {
		service, launchErr := NewService(i)
		if launchErr != nil {
			err = launchErr
			return
		}

		result.all[i] = service
		var svcIdx = i

		go func() {
			var serviceExit = service.ServiceExit
			// TODO: consider implementing alive check
			// aliveTimeout := time.NewTimer(ServiceAliveTimeout)
			var shutdownService = func (exitProcess bool) {
				serviceExit = nil
				if exitProcess {
					service.Shutdown()
				}
				result.Drop(svcIdx)
			}

			for {
				select {
				/*
					case <- aliveTimeout.C:
						// No alive signals from service for some time. Usually this means
						// that connection to the service has been lost and we need to restart it
						log.Printf("service %v, alive timeout", svcIdx)
						shutdown(true)

					case <- service.ServiceAlive:
						// This means that alive ping has been received
						// timeout timer should be restarted
						// TODO:DEBUG LOG
						log.Printf("service %v, service is alive, restarting alive timeout", svcIdx)
						aliveTimeout = time.NewTimer(ServiceAliveTimeout)
				*/

				case <- serviceExit:
					//
					// Now we just drop all endpoints for this service and continue without it
					// TODO: If there are 0 services left - panic
					// TODO: consider restart on the same port, Do not drop endpoints then, bad clients might try to reconnect.
					// TODO: consider restart on new port, drop endpoints. This seems to be better solution (?)
					//
					// This means that something really bad happened.
					// May be wallet service has been killed by OOM/admin/other process
					log.Printf("service %v, unexpected shutdown [%v]", svcIdx, service.command.ProcessState)
					// TODO: consider implementing service restart. This might be not so trivial
					shutdownService(false)
					return
				}
			}
		}()
	}

	return
}
