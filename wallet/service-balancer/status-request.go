package main

import (
	"net/http"
	"runtime"
	"runtime/debug"
)

type statusRes struct {
	Memory      runtime.MemStats
	GC          debug.GCStats
	NumCPU      int
	NumGos      int
	MaxServices int
	Services    []*ServiceStats
}

func statusRequest (r *http.Request)(interface{}, error) {
	if !config.Debug {
		return nil, nil
	}

	res := &statusRes {
		NumCPU: runtime.NumCPU(),
		NumGos: runtime.NumGoroutine(),
	}

	runtime.ReadMemStats(&res.Memory)
	debug.ReadGCStats(&res.GC)
	res.Services = services.GetStats()
	res.MaxServices = len(res.Services)

	return res, nil
}
