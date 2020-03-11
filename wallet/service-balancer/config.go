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

// TODO: add counters

const (
	ConfigFile = "config.json"
)

type Config struct {
	BeamNodeAddress         string
	WalletServicePath       string
	WalletServiceFirstPort  int
	WalletServiceLastPort   int
	BbsMonitorPath			string
	BbsMonitorFirstPort     int
	BbsMonitorLastPort      int
	SerivcePublicAddress    string
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
}

var config = Config{
	NoisyLogs:     false,
	Debug:         true,
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
		return errors.New("config, invalid wallet serivce last port")
	}

	if len(cfg.SerivcePublicAddress) == 0 {
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
		cfg.ServiceLaunchTimeout = 5  * time.Second
	}

	if cfg.ServiceAliveTimeout == 0 {
		cfg.ServiceAliveTimeout = 15 * time.Second
	}

	if cfg.ServiceHeartbeatTimeout == 0 {
		cfg.ServiceHeartbeatTimeout = 10 * time.Second
	}

	var mode = "RELEASE"
	if cfg.Debug {
		mode = "DEBUG"
	}

	log.Printf("starting in %v mode", mode)
	return nil
}
