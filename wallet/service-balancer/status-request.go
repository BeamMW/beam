package main

import (
	"beam.mw/service-balancer/services"
	"errors"
	"github.com/capnm/sysinfo"
	"net/http"
	"runtime"
	"runtime/debug"
)

type WalletStats struct {
	services.ServiceStats
	EndpointsCnt int
	ClientsCnt int
}

type statusRes struct {
	GoMemory          runtime.MemStats
	SysInfo			  *sysinfo.SI
	DBDiskTotal       uint64
	DBDiskFree        uint64
	GC                debug.GCStats
	NumCPU            int
	NumGos            int
	MaxWalletServices int
	MaxBbsServices    int
	WalletServices    []*WalletStats
	BbsServices       []*services.ServiceStats
	Config            *Config
	Counters          Counters
	WalletSockets     int64
}

func KbToMB(kbytes uint64) uint64 {
	return kbytes / 1024
}

func statusRequest (r *http.Request)(interface{}, error) {
	if len(config.APISecret) == 0 && !config.Debug {
		return nil, errors.New("no secret provided in config")
	}

	if len(config.APISecret) != 0 {
		if r.FormValue("secret") != config.APISecret {
			return nil, errors.New("bad access token")
		}
	}

	res := &statusRes {
		NumCPU:     runtime.NumCPU(),
		NumGos:     runtime.NumGoroutine(),
		Config:     &config,
	}

	// Do not expose sensitive info
	res.Config.VAPIDPrivate = "--not exposed--"
	counters.CopyTo(&res.Counters)
	res.WalletSockets = res.Counters.WConnect - res.Counters.WDisconnect

	runtime.ReadMemStats(&res.GoMemory)
	res.SysInfo = sysinfo.Get()
	debug.ReadGCStats(&res.GC)

	wstats := walletServices.GetStats()
	if len(wstats) != 0 {
		res.WalletServices = make([]*WalletStats, len(wstats))
		for i, stat := range wstats {
			full := WalletStats{}
			full.Port = stat.Port
			full.Pid = stat.Pid
			full.Args = stat.Args
			full.ProcessState = stat.ProcessState
			full.EndpointsCnt, full.ClientsCnt = epoints.GetSvcCounts(i)
			res.WalletServices[i] = &full
		}
	}

	res.BbsServices       = sbbsServices.GetStats()
	res.MaxWalletServices = len(res.WalletServices)
	res.MaxBbsServices    = len(res.BbsServices)

	// TODO: add database stats & disk usage info
	// TODO: add subscriptions stats

	return res, nil
}
