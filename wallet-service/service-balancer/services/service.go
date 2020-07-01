package services

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/exec"
	"strconv"
)

//
// This represents a single monitored process which listens on specific port
//
type service struct {
	Port         int
	command      *exec.Cmd
	cancelCmd    context.CancelFunc
	context      context.Context
	serviceExit  chan struct{}
	serviceAlive chan bool
}

func (svc* service) Shutdown() {
	svc.cancelCmd()
}

func newService(cfg *Config, index int, port int, logname string) (svc *service, err error) {
	svc = &service{}

	ctxWS, cancelWS := context.WithCancel(context.Background())
	svc.cancelCmd    = cancelWS
	svc.context      = ctxWS
	svc.command      = exec.CommandContext(ctxWS, cfg.ServiceExePath, "-n", cfg.BeamNodeAddress, "-p", strconv.Itoa(port))

	var cliOptions = ""
	for _, arg := range cfg.CliOptions {
		svc.command.Args = append(svc.command.Args, arg)
		cliOptions = cliOptions + " " + arg
	}

	svc.serviceExit  = make(chan struct{})
	svc.serviceAlive = make(chan bool)
	svc.Port         = port

	if cfg.Debug {
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
	log.Printf("%v %v, starting as [%v %v %v %v%v]", logname, index, cfg.ServiceExePath, "-n " + cfg.BeamNodeAddress, "-p", port, cliOptions)
	if err = svc.command.Start(); err != nil {
		return
	}
	log.Printf("%v %v, pid is %v", logname, index, svc.command.Process.Pid)

	//
	// Wait for the service spin-up & listening
	//
	presp, err := readPipe(startPipeR, cfg.StartTimeout)
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
		close(svc.serviceExit)
	} ()

	// This goroutine reads process heartbeat
	go func () {
		for {
			_, err := readPipe(alivePipeR, cfg.HeartbeatTimeout)
			if err != nil {
				if cfg.Debug {
					log.Printf("%v %v, aborting hearbeat pipe %v", logname, index, err)
				}
				return
			}
			svc.serviceAlive <- true
		}
	} ()

	log.Printf("%v %v, successfully started, sync pipe response %v", logname, index, presp)
	return
}

type ServiceStats struct {
	Pid          int
	Port         int
	Args         []string
	ProcessState *os.ProcessState
}

func (svc* service) GetStats() (stats *ServiceStats) {
	stats = &ServiceStats{
		Pid:          svc.command.Process.Pid,
		Port:         svc.Port,
		Args:         svc.command.Args,
		ProcessState: svc.command.ProcessState,
	}
	return
}
