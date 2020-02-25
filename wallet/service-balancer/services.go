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
	"time"
)

//
// Single wallet service
//
type Service struct {
	Address      string
	command      *exec.Cmd
	cancelCmd    context.CancelFunc
	context      context.Context
	ServiceExit  chan struct{}
	ServiceAlive chan bool
}

func (svc* Service) Shutdown() {
	svc.cancelCmd()
}

func NewService(index int) (svc *Service, err error) {
	svc = &Service{}
	var port = config.ServiceFirstPort + index

	ctxWS, cancelWS := context.WithCancel(context.Background())
	svc.cancelCmd    = cancelWS
	svc.context      = ctxWS
	svc.command      = exec.CommandContext(ctxWS, config.ServicePath, "-n", config.BeamNode, "-p", strconv.Itoa(port))
	svc.Address      = config.SerivcePublicAddress + ":" + strconv.Itoa(port)
	svc.ServiceExit  = make(chan struct{})
	svc.ServiceAlive = make(chan bool)

	if config.Debug {
		svc.command.Stdout = os.Stdout
		svc.command.Stderr = os.Stderr
	}

	// Setup pipes
	startPipeR, startPipeW, err := os.Pipe()
	if err != nil {
		return
	}

	alivePipeR, alivePipeW, err := os.Pipe()
	if err != nil {
		return
	}

	defer func () {
		if err != nil {
			svc = nil
			if alivePipeR != nil {
				_ = alivePipeR.Close()
			}
		}

		if startPipeW != nil {
			_ = startPipeW.Close()
		}
		if startPipeR != nil {
			_ = startPipeR.Close()
		}
		if alivePipeW != nil {
			_ = alivePipeW.Close()
		}
	} ()

	svc.command.ExtraFiles = []*os.File {
		startPipeW,
		alivePipeW,
	}

	// Start wallet service
	log.Printf("service %v, starting as [%v %v %v %v]", index, config.ServicePath, "-n " + config.BeamNode, "-p", port)
	if err = svc.command.Start(); err != nil {
		return
	}
	log.Printf("service %v, pid is %v", index, svc.command.Process.Pid)

	//
	// Wait for the service spin-up & listening
	//
	presp, err := readPipe(startPipeR, config.ServiceLaunchTimeout)
	if err != nil {
		cancelWS()
		err = fmt.Errorf("service %v, failed to read from sync pipe, %v", index, err)
		return
	}

	if "LISTENING" != presp {
		cancelWS()
		err = fmt.Errorf("service %v, failed to start. Wrong pipe response %v", index, presp)
		return
	}

	// This goroutine waits for process exit
	go func () {
		_ = svc.command.Wait()
		_ = alivePipeR.Close()
		close(svc.ServiceExit)
	} ()

	// This goroutine reads process heartbeat
	go func () {
		for {
			_, err := readPipe(alivePipeR, config.ServiceHeartbeatTimeout)
			if err != nil {
				if config.Debug {
					log.Printf("service %v, aborting hearbeat pipe %v", index, err)
				}
				return
			}
			svc.ServiceAlive <- true
		}
	} ()

	log.Printf("service %v, successfully started, sync pipe response %v", index, presp)
	return
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
			var serviceExit  = service.ServiceExit
			var aliveTimeout = time.NewTimer(config.ServiceAliveTimeout)

			for {
				select {
				case <- aliveTimeout.C:
					// No alive signals from service for some time. Usually this means
					// that connection to the service has been lost and we need to restart it
					log.Printf("service %v, alive timeout", svcIdx)
					service.Shutdown()

				case <- service.ServiceAlive:
					// This means that alive ping has been received. Timeout timer should be restarted
					if config.NoisyLogs {
						log.Printf("service %v, service is alive, restarting alive timeout", svcIdx)
					}
					aliveTimeout = time.NewTimer(config.ServiceAliveTimeout)

				case <- serviceExit:
					//
					// Now we just drop all endpoints for this service and continue without it
					// TODO: If there are 0 services left - panic OR
					//       - consider restart on the same port, Do not drop endpoints then, bad clients might try to reconnect.
					//       - consider restart on new port, drop endpoints. This seems to be better solution (?)
					//
					// This means that something really bad happened.
					// May be wallet service has been killed by OOM/admin/other process
					// TODO: check all 'dropped' messages, if counters are correct
					log.Printf("service %v, unexpected shutdown [%v]", svcIdx, service.command.ProcessState)
					result.Drop(svcIdx)
					return
				}
			}
		}()
	}

	return
}
