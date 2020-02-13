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
	Node          string
	ServicePath   string
	ListenAddress string
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

	if len(cfg.Node) == 0 {
		return errors.New("Config: missing Node")
	}

	if len(cfg.ServicePath) == 0 {
		return errors.New("Config: missing ServicePath")
	}

	if len(cfg.ListenAddress) == 0 {
		return errors.New("Config: missing ListenAddress")
	}

	return nil
}
