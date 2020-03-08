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
	"sync/atomic"
	"time"
)

//
// Single wallet service
//
type Service struct {
	Address      string
	Port         int
	command      *exec.Cmd
	cancelCmd    context.CancelFunc
	context      context.Context
	ServiceExit  chan struct{}
	ServiceAlive chan bool
}

type ServiceStats struct {
	Pid          int
	Address      string
	Args         []string
	ProcessState *os.ProcessState
	EndpointsCnt int
	ClientsCnt   int
}

func (svc* Service) GetStats() (stats *ServiceStats) {
	stats = &ServiceStats{
		Pid:          svc.command.Process.Pid,
		Address:      svc.Address,
		Args:         svc.command.Args,
		ProcessState: svc.command.ProcessState,
	}
	return
}

func (svc* Service) Shutdown() {
	svc.cancelCmd()
}

func NewService(servicePath string, index int, port int, logname string) (svc *Service, err error) {
	svc = &Service{}

	ctxWS, cancelWS := context.WithCancel(context.Background())
	svc.cancelCmd    = cancelWS
	svc.context      = ctxWS
	svc.command      = exec.CommandContext(ctxWS, servicePath, "-n", config.BeamNode, "-p", strconv.Itoa(port))
	svc.Address      = config.SerivcePublicAddress + ":" + strconv.Itoa(port)
	svc.ServiceExit  = make(chan struct{})
	svc.ServiceAlive = make(chan bool)
	svc.Port         = port

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
	log.Printf("%v %v, starting as [%v %v %v %v]", logname, index, servicePath, "-n " + config.BeamNode, "-p", port)
	if err = svc.command.Start(); err != nil {
		return
	}
	log.Printf("%v %v, pid is %v", logname, index, svc.command.Process.Pid)

	//
	// Wait for the service spin-up & listening
	//
	presp, err := readPipe(startPipeR, config.ServiceLaunchTimeout)
	if err != nil {
		cancelWS()
		err = fmt.Errorf("%v %v, failed to read from sync pipe, %v", logname, index, err)
		return
	}

	if "LISTENING" != presp {
		cancelWS()
		err = fmt.Errorf("%v %v, failed to start. Wrong pipe response %v", logname, index, presp)
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
					log.Printf("%v %v, aborting hearbeat pipe %v", logname, index, err)
				}
				return
			}
			svc.ServiceAlive <- true
		}
	} ()

	log.Printf("%v %v, successfully started, sync pipe response %v", logname, index, presp)
	return
}

//
// Services collection
//
const (
	// TODO: remove min services and resolve dep in GetNext() code
	MinServices = 2
)

type Services struct  {
	Dropped     chan int
	Restarted   chan int
	mutex       sync.Mutex
	all         []*Service
	logname     string
	servicePath string
	nextIdx     int
	nextPort    int32
}

func (svcs* Services) GetActiveCnt() (cnt int) {
	svcs.mutex.Lock()
	for i := 0; i < len(svcs.all); i++ {
		if svcs.all[i] != nil {
			cnt++
		}
	}
	svcs.mutex.Unlock()
	return
}

func (svcs* Services) GetStats() (stats []*ServiceStats) {
	svcs.mutex.Lock()
		stats = make([]*ServiceStats, len(svcs.all))
		for i := 0; i < len(svcs.all); i++ {
			if svcs.all[i] != nil {
				stats[i] = svcs.all[i].GetStats()
			}
		}
	svcs.mutex.Unlock()

	for i := 0; i < len(stats); i++ {
		stats[i].EndpointsCnt, stats[i].ClientsCnt = epoints.GetSvcCounts(i)
	}

	return
}

func (svcs* Services) GetAt(index int) (*Service, error) {
	svcs.mutex.Lock()
	defer svcs.mutex.Unlock()

	if index < 0 || index >= len(svcs.all) {
		return nil, fmt.Errorf("bad index %v (services cnt is %v)", index, len(svcs.all))
	}

	return svcs.all[index], nil
}

func (svcs* Services) GetNext() (int, *Service, error) {
	svcs.mutex.Lock()
	defer svcs.mutex.Unlock()

	var initialIdx = svcs.nextIdx
	var svcIdx     = svcs.nextIdx
	var maxSvcs    = len(svcs.all)

	for  {
		svcIdx = svcs.nextIdx
		svcs.nextIdx++

		if svcs.nextIdx == maxSvcs {
			svcs.nextIdx = 0
		}

		var service = svcs.all[svcIdx]
		if service == nil {
			if svcs.nextIdx == initialIdx {
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

func (svcs *Services) getNextPort() (port int) {
	port = int(atomic.AddInt32(&svcs.nextPort, 1)) - 1
	return
}

func (svcs *Services) DropAndRelaunch (svcIdx int) (service *Service, err error) {
	svcs.mutex.Lock()
	defer svcs.mutex.Unlock()

	svcs.all[svcIdx] = nil
	svcs.Dropped <- svcIdx

	service, err = NewService(svcs.servicePath, svcIdx, svcs.getNextPort(), svcs.logname)
	if err != nil {
		return
	}

	svcs.all[svcIdx] = service
	svcs.Restarted <- svcIdx
	return
}

func (svcs* Services) Get (svcIdx int) *Service {
	svcs.mutex.Lock()
	defer svcs.mutex.Unlock()

	return svcs.all[svcIdx]
}

func NewServices(svcsCnt int, port int, servicePath string, logname string) (result *Services, err error) {
	defer func () {
		if err != nil {
			if result != nil {
				result.ShutdownAll()
			}
		}
	}()

	result = &Services{
		Dropped:     make(chan int),
		Restarted:   make(chan int),
		mutex:       sync.Mutex{},
		all:         make([]*Service, svcsCnt),
		servicePath: servicePath,
		nextPort:    int32(port),
		logname:     logname,
	}

	for i := 0; i < svcsCnt; i++ {
		service, launchErr := NewService(result.servicePath, i, result.getNextPort(), logname)
		if launchErr != nil {
			err = launchErr
			return
		}

		result.all[i] = service
		var svcIdx = i

		go func() {
			var aliveTimeout = time.NewTimer(config.ServiceAliveTimeout)

			for {
				select {
				case <- aliveTimeout.C:
					// No alive signals from service for some time. Usually this means
					// that connection to the service has been lost and we need to restart it
					log.Printf("%v %v, alive timeout", logname, svcIdx)
					service.Shutdown()

				case <- service.ServiceAlive:
					// This means that alive ping has been received. Timeout timer should be restarted
					if config.NoisyLogs {
						log.Printf("%v %v, service is alive, restarting alive timeout", logname, svcIdx)
					}
					aliveTimeout = time.NewTimer(config.ServiceAliveTimeout)

				case <- service.ServiceExit:
					log.Printf("%v %v, unexpected shutdown [%v]", logname, svcIdx, service.command.ProcessState)
					// TODO: consider restart on the same port, Do not drop endpoints then, bad clients might try to reconnect.
					//       this might fail with busy ports and cause other issues. While this service is not public
					//       it seems to be better to continue using new ports
					if service, err = result.DropAndRelaunch(svcIdx); err != nil {
						log.Printf("%v %v, failed to relaunch", logname, err)
						if result.GetActiveCnt() == 0 {
							log.Fatalf("no active services left, halting")
						}
					}
				}
			}
		}()
	}

	return
}

func NewWalletServices () (*Services, error)  {
	var svcsCnt = runtime.NumCPU() - 2
	if config.Debug {
		svcsCnt = 2
	}

	log.Printf("initializing wallet services, CPU count %v, service count %v", runtime.NumCPU(), svcsCnt)
	return NewServices(svcsCnt, config.ServiceFirstPort, config.ServicePath, "service")
}

func NewBbsServices () (*Services, error) {
	var svcsCnt = 1
	log.Printf("initializing wallet services, CPU count %v, service count %v", runtime.NumCPU(), svcsCnt)
	return NewServices(svcsCnt, config.BbsServiceFirstPort, config.BbsServicePath, "bbs")
}
