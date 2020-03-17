package services

import (
	"os"
	"time"
)

const (
	pipeBufferSize = 1024
)

func readPipe(pipe *os.File, timeout time.Duration) (res string, err error) {
	if timeout != 0 {
		if err = pipe.SetReadDeadline(time.Now().Add(timeout)); err != nil {
			return
		}
	}

	var dsize int
	var data = make([]byte, pipeBufferSize)
	dsize, err = pipe.Read(data)
	if err != nil {
		return
	}

	res = string(data[:dsize])
	return
}
