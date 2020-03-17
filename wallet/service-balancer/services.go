package main

import (
	"beam.mw/service-balancer/services"
	"log"
	"runtime"
)

func NewWalletServices () (* services.Services, error)  {
	var svcsCnt = runtime.NumCPU() - 2
	if config.Debug {
		svcsCnt = 2
	}

	log.Printf("initializing wallet services, CPU count %v, service count %v", runtime.NumCPU(), svcsCnt)
	cfg := services.Config{
		BeamNodeAddress:  config.BeamNodeAddress,
		ServiceExePath:   config.WalletServicePath,
		StartTimeout:     config.ServiceLaunchTimeout,
		HeartbeatTimeout: config.ServiceHeartbeatTimeout,
		AliveTimeout:     config.ServiceAliveTimeout,
		FirstPort:        config.WalletServiceFirstPort,
		LastPort:         config.WalletServiceLastPort,
		Debug:            config.Debug,
		NoisyLogs:        config.NoisyLogs,
	}
	return services.NewServices(&cfg, svcsCnt, "service")
}

func NewBbsServices () (* services.Services, error) {
	var svcsCnt = 1
	log.Printf("initializing wallet services, CPU count %v, service count %v", runtime.NumCPU(), svcsCnt)
	cfg := services.Config{
		BeamNodeAddress:  config.BeamNodeAddress,
		ServiceExePath:   config.BbsMonitorPath,
		StartTimeout:     config.ServiceLaunchTimeout,
		HeartbeatTimeout: config.ServiceHeartbeatTimeout,
		AliveTimeout:     config.ServiceAliveTimeout,
		FirstPort:        config.BbsMonitorFirstPort,
		LastPort:         config.BbsMonitorLastPort,
		Debug:            config.Debug,
		NoisyLogs:        config.NoisyLogs,
	}
	return services.NewServices(&cfg, svcsCnt, "bbs")
}
