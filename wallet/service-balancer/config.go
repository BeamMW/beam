package main

import (
	"os"
	"log"
	"encoding/json"
	"path/filepath"
	"errors"
)

const (
	ConfigFile = "config.json"
)

type Config struct {
	BeamNode             string
	ServicePath          string
	ServiceFirstPort     int
	SerivcePublicAddress string
	ListenAddress        string
}

var config = Config{}

func loadConfig () error {
	return config.Read(ConfigFile)
}

func (cfg* Config) Read(fname string) error {
	
	fpath, err := filepath.Abs(fname)
	if err != nil {
		return err
	}

	log.Println("Reading configuration from", fpath)

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

	if len(cfg.BeamNode) == 0 {
		return errors.New("config, missing Node")
	}

	if len(cfg.ServicePath) == 0 {
		return errors.New("config, missing ServicePath")
	}

	if len(cfg.ListenAddress) == 0 {
		return errors.New("config, missing ListenAddress")
	}

	if cfg.ServiceFirstPort <= 0 {
		return errors.New("config, invalid wallet serivce port")
	}

	if len(cfg.SerivcePublicAddress) == 0 {
		return errors.New("config, missing public address")
	}

	return nil
}
