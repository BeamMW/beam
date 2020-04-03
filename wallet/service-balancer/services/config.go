package services

import "time"

type Config struct {
	BeamNodeAddress   string
	ServiceExePath    string
	CliOptions        []string
	StartTimeout      time.Duration
	HeartbeatTimeout  time.Duration
	AliveTimeout      time.Duration
	FirstPort         int
	LastPort          int
	Debug             bool
	NoisyLogs		  bool
}
