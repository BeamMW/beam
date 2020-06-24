package services

import (
	"errors"
	"fmt"
	"log"
	"math"
	"sync"
	"time"
)

//
// Services collection
//
type Services struct  {
	Dropped     chan int
	Restarted   chan int
	logname     string
	servicePath string
	config      *Config
	all         []*service
	allMutex    sync.Mutex
	nextIdx     int
	nextPort    int
	portMutex   sync.Mutex
}

func (svcs* Services) GetActiveCnt() (cnt int) {
	svcs.allMutex.Lock()
	defer svcs.allMutex.Unlock()

	for i := 0; i < len(svcs.all); i++ {
		if svcs.all[i] != nil {
			cnt++
		}
	}

	return
}

func (svcs* Services) GetStats() (stats []*ServiceStats) {
	svcs.allMutex.Lock()
	defer svcs.allMutex.Unlock()

	stats = make([]*ServiceStats, len(svcs.all))
	for i := 0; i < len(svcs.all); i++ {
		if svcs.all[i] != nil {
			stats[i] = svcs.all[i].GetStats()
		}
	}

	return
}

func (svcs* Services) GetAt(index int) (*service, error) {
	svcs.allMutex.Lock()
	defer svcs.allMutex.Unlock()

	if index < 0 || index >= len(svcs.all) {
		return nil, fmt.Errorf("bad index %v (services cnt is %v)", index, len(svcs.all))
	}
	return svcs.all[index], nil
}

func (svcs* Services) getNextByUsage(lock bool) (int, *service, error) {
	if lock {
		svcs.allMutex.Lock()
		defer svcs.allMutex.Unlock()
	}

	var invalidIdx     = -1
	var svcIdx         = invalidIdx
	var minUsage int64 = math.MaxInt64

	for idx, service := range svcs.all {
		if service == nil || service.command == nil || service.command.Process == nil {
			continue
		}

		pid := service.command.Process.Pid
		usage, err := getPIDUsage(pid)
		if err != nil {
			log.Printf("Failed to get %v rusage, %v", pid, err)
			continue
		}

		if usage < minUsage {
			svcIdx = idx
			minUsage = usage
		}
	}

	if svcIdx == invalidIdx {
		// This is really bad and MUST never happen in normal flow
		return 0, nil, errors.New("unable to find service")
	}

	svcs.nextIdx = svcIdx + 1
	return svcIdx, svcs.all[svcIdx], nil
}

func (svcs* Services) getNextByIndex(lock bool) (int, *service, error) {
	if lock {
		svcs.allMutex.Lock()
		defer svcs.allMutex.Unlock()
	}

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

func (svcs* Services) GetNext() (int, *service, error) {
	svcs.allMutex.Lock()
	defer svcs.allMutex.Unlock()

	if idx, svc, err := svcs.getNextByUsage(false); err == nil {
		return idx, svc, nil
	}

	return svcs.getNextByIndex(false)
}

func (svcs *Services) ShutdownAll () {
	svcs.allMutex.Lock()
	defer svcs.allMutex.Unlock()

	for i := 0; i < len(svcs.all); i++ {
		if svcs.all[i] != nil && svcs.all[i].cancelCmd != nil {
			log.Printf("serivce %v, closing", i)
			svcs.all[i].Shutdown()
		}
	}
}

func (svcs *Services) getNextPort() (port int) {
	svcs.portMutex.Lock()
	defer svcs.portMutex.Unlock()

	port = svcs.nextPort
	svcs.nextPort++

	if svcs.nextPort > svcs.config.LastPort {
		svcs.nextPort = svcs.config.FirstPort
	}

	return
}

func (svcs *Services) DropAndRelaunch (svcIdx int) (service *service, err error) {
	svcs.allMutex.Lock()
	defer svcs.allMutex.Unlock()

	svcs.all[svcIdx] = nil
	svcs.Dropped <- svcIdx

	service, err = newService(svcs.config, svcIdx, svcs.getNextPort(), svcs.logname)
	if err != nil {
		return
	}

	svcs.all[svcIdx] = service
	svcs.Restarted <- svcIdx
	return
}

func NewServices(cfg *Config, svcsCnt int, logname string) (result *Services, err error) {
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
		allMutex:    sync.Mutex{},
		all:         make([]*service, svcsCnt),
		servicePath: cfg.ServiceExePath,
		nextPort:    cfg.FirstPort,
		logname:     logname,
		config:      cfg,
	}

	for i := 0; i < svcsCnt; i++ {
		service, launchErr := newService(cfg, i, result.getNextPort(), logname)
		if launchErr != nil {
			err = launchErr
			return
		}

		result.all[i] = service
		var svcIdx = i

		go func() {
			var aliveTimeout = time.NewTimer(cfg.AliveTimeout)

			for {
				select {
				case <- aliveTimeout.C:
					// No alive signals from service for some time. Usually this means
					// that connection to the service has been lost and we need to restart it
					log.Printf("%v %v, alive timeout", logname, svcIdx)
					service.Shutdown()

				case <- service.serviceAlive:
					// This means that alive ping has been received. Timeout timer should be restarted
					if cfg.NoisyLogs {
						log.Printf("%v %v, service is alive, restarting alive timeout", logname, svcIdx)
					}
					aliveTimeout = time.NewTimer(cfg.AliveTimeout)

				case <- service.serviceExit:
					log.Printf("%v %v, unexpected shutdown [%v]", logname, svcIdx, service.command.ProcessState)
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


