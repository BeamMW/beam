package main

import (
	"encoding/json"
	"errors"
	"github.com/olahol/melody"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"time"
)

const (
	ConfigFile = "config.json"
)

type Config struct {
	BeamNodeAddress         string
	WalletServicePath       string
	WalletServiceFirstPort  int
	WalletServiceLastPort   int
	WalletServiceCnt        int
	BbsMonitorPath			string
	BbsMonitorFirstPort     int
	BbsMonitorLastPort      int
	BbsMonitorCnt           int
	ServicePublicAddress    string
	ReturnRawSvcPort        bool // if true login returns ServicePublicAddress:port, on false ServicePublicAddress?service=port
	ListenAddress           string
	PushContactMail			string
	Debug				    bool
	NoisyLogs               bool
	EndpointAliveTimeout    time.Duration
	ServiceLaunchTimeout    time.Duration
	ServiceAliveTimeout     time.Duration
	ServiceHeartbeatTimeout time.Duration
	VAPIDPublic             string
	VAPIDPrivate            string
	DatabasePath            string
	APISecret               string
	AllowedOrigin           string
	ActivityLogInterval     time.Duration
}

var config = Config{
	NoisyLogs:        false,
	Debug:            true,
}

func loadConfig (m *melody.Melody) error {
	return config.Read(ConfigFile, m)
}

func (cfg* Config) Read(fname string, m *melody.Melody) error {
	// Set melody params
	m.Config.MaxMessageSize = 1024

	// Read config file
	fpath, err := filepath.Abs(fname)
	if err != nil {
		return err
	}

	log.Println("reading configuration from", fpath)

	file, err := os.Open(fname)
	if err != nil {
		return err
	}
	defer file.Close()

	decoder := json.NewDecoder(file)
	decoder.DisallowUnknownFields()

	err = decoder.Decode(cfg)
	if err != nil {
		return err
	}

	if len(cfg.WalletServicePath) == 0 {
		return errors.New("config, missing WalletServicePath")
	}

	if cfg.WalletServiceFirstPort <= 0 {
		return errors.New("config, invalid wallet serivce first port")
	}

	if  cfg.WalletServiceLastPort <= 0 ||
		cfg.WalletServiceFirstPort > cfg.WalletServiceLastPort ||
		cfg.WalletServiceFirstPort + runtime.NumCPU() > cfg.WalletServiceLastPort {
		return errors.New("config, invalid wallet service last port")
	}

	if len(cfg.ServicePublicAddress) == 0 {
		return errors.New("config, missing public address")
	}

	if cfg.EndpointAliveTimeout == 0 {
		cfg.EndpointAliveTimeout = m.Config.PingPeriod + m.Config.PingPeriod/2
	}

	if len(cfg.BbsMonitorPath) == 0 {
		return errors.New("config, missing BbsMonitorPath")
	}

	if cfg.BbsMonitorFirstPort <= 0 {
		return errors.New("config, invalid bbs monitor first port")
	}

	if  cfg.BbsMonitorLastPort <= 0 ||
		cfg.BbsMonitorFirstPort > cfg.BbsMonitorLastPort ||
		cfg.BbsMonitorFirstPort + runtime.NumCPU() > cfg.BbsMonitorLastPort {
		return errors.New("config, invalid bbs monitor last port")
	}

	if len(cfg.BeamNodeAddress) == 0 {
		return errors.New("config, missing Node")
	}

	if len(cfg.ListenAddress) == 0 {
		return errors.New("config, missing ListenAddress")
	}

	if len(cfg.PushContactMail) == 0 {
		return errors.New("config, missing push contact email")
	}

	if cfg.ServiceLaunchTimeout == 0 {
		cfg.ServiceLaunchTimeout = 10  * time.Second
	}

	if cfg.ServiceAliveTimeout == 0 {
		cfg.ServiceAliveTimeout = 15 * time.Second
	}

	if cfg.ServiceHeartbeatTimeout == 0 {
		cfg.ServiceHeartbeatTimeout = 11 * time.Second // wait for 2 heartbeats via pipe + 1 second
	}

	if len(cfg.DatabasePath) == 0 {
		return errors.New("missing database path")
	}

	if cfg.WalletServiceCnt == 0 {
		if cfg.Debug {
			cfg.WalletServiceCnt = 1
		} else {
			cfg.WalletServiceCnt = runtime.NumCPU() - 2
			if cfg.WalletServiceCnt == 0 {
				log.Printf("CPU count %v, no count in settings, defaulting to 2", runtime.NumCPU())
				cfg.WalletServiceCnt = 2
			}
		}
	}

	var mode = "RELEASE"
	if cfg.Debug {
		mode = "DEBUG"
	} else {
		if cfg.NoisyLogs {
			log.Printf("NoisyLogs set to true in config in RELEASE mode, forced to false")
			cfg.NoisyLogs = false
		}
	}

	if cfg.ActivityLogInterval == 0 {
		if cfg.Debug {
			cfg.ActivityLogInterval = time.Duration(5 * time.Second) //(1 * time.Minute)
		} else {
			cfg.ActivityLogInterval = time.Duration(10 * time.Minute)
		}
	}
	log.Printf("Activity log interval %v", cfg.ActivityLogInterval)

	if cfg.BbsMonitorCnt == 0 {
		cfg.BbsMonitorCnt = 1
	}

	if cfg.BbsMonitorCnt != 1 {
		log.Printf("WARNING: %d bbs monitors requests, currenlty only 1 is supported. Resetting to 1.", cfg.BbsMonitorCnt)
		cfg.BbsMonitorCnt = 1
	}

	if len(cfg.APISecret) == 0 {
		log.Println("Warning: APISecret is not provided. Balancer stats via /stats?secret=APISecret would be inaccessible")
	}

	log.Printf("starting in %v mode", mode)
	return nil
}

func (cfg* Config) ShouldLaunchBBSMonitor () bool {
	return len(cfg.VAPIDPublic) > 0 && len(cfg.VAPIDPrivate) > 0 && len(cfg.PushContactMail) > 0
}
