package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"github.com/olahol/melody"
	"log"
	"os"
	"path/filepath"
	"time"
)

const (
	ConfigFile = "config.json"
)

type Config struct {
	BeamNode                string
	ServicePath             string
	ServiceFirstPort        int
	BbsServicePath			string
	BbsServiceFirstPort     int
	SerivcePublicAddress    string
	ListenAddress           string
	Debug				    bool
	NoisyLogs               bool
	EndpointAliveTimeout    time.Duration
	ServiceLaunchTimeout    time.Duration
	ServiceAliveTimeout     time.Duration
	ServiceHeartbeatTimeout time.Duration
	VAPIDPublic             string
	VAPIDPrivate            string
	VAPIDPublicKey          PublicKey
	VAPIDPrivateKey         PrivateKey
}

var config = Config{
	NoisyLogs:     false,
	Debug:         true,
}

func loadConfig (m *melody.Melody) error {
	return config.Read(ConfigFile, m)
}

func (cfg* Config) Read(fname string, m *melody.Melody) error {
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

	if len(cfg.ServicePath) == 0 {
		return errors.New("config, missing ServicePath")
	}

	if cfg.ServiceFirstPort <= 0 {
		return errors.New("config, invalid wallet serivce port")
	}

	if len(cfg.SerivcePublicAddress) == 0 {
		return errors.New("config, missing public address")
	}

	if cfg.EndpointAliveTimeout == 0 {
		cfg.EndpointAliveTimeout = m.Config.PingPeriod + m.Config.PingPeriod/2
	}

	if len(cfg.BbsServicePath) == 0 {
		return errors.New("config, missing BbsServicePath")
	}

	if cfg.BbsServiceFirstPort <= 0 {
		return errors.New("config, invalid bbs serivce port")
	}

	if key, err := LoadVAPIDPrivateKey(cfg.VAPIDPrivate); err != nil {
		return fmt.Errorf("failed to load VAPID private key, %v", err)
	} else {
		cfg.VAPIDPrivateKey = *key
	}

	if key, err := LoadVAPIDPublicKey(cfg.VAPIDPublic); err != nil {
		return fmt.Errorf("failed to load VAPID private key, %v", err)
	} else {
		cfg.VAPIDPublicKey = *key
	}

	if len(cfg.BeamNode) == 0 {
		return errors.New("config, missing Node")
	}

	if len(cfg.ListenAddress) == 0 {
		return errors.New("config, missing ListenAddress")
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
