package main

import (
	"net/http"
	"runtime"
	"runtime/debug"
)

type statusRes struct {
	Memory            runtime.MemStats
	GC                debug.GCStats
	NumCPU            int
	NumGos            int
	MaxWalletServices int
	MaxBbsServices    int
	WalletServices    []*ServiceStats
	BbsServices       []*ServiceStats
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
	res.WalletServices    = walletServices.GetStats()
	res.BbsServices       = sbbsServices.GetStats()
	res.MaxWalletServices = len(res.WalletServices)
	res.MaxBbsServices    = len(res.BbsServices)

	return res, nil
}
