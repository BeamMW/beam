package main

import (
	"beam.mw/service-balancer/services"
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
	Memory            runtime.MemStats
	GC                debug.GCStats
	NumCPU            int
	NumGos            int
	MaxWalletServices int
	MaxBbsServices    int
	WalletServices    []*WalletStats
	BbsServices       []*services.ServiceStats
}

func statusRequest (r *http.Request)(interface{}, error) {
	if !config.Debug {
		// TODO: add secret-based access
		return nil, nil
	}

	res := &statusRes {
		NumCPU: runtime.NumCPU(),
		NumGos: runtime.NumGoroutine(),
	}

	runtime.ReadMemStats(&res.Memory)
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

	return res, nil
}
