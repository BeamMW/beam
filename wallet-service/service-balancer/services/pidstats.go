package services

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
)

func getPIDUsage(pid int) (int64, error) {
	if runtime.GOOS != "linux" {
		return 0, fmt.Errorf("%s is not supported", runtime.GOOS)
	}

	filename := filepath.Join("/proc", strconv.FormatInt(int64(pid), 10), "stat")
	file, err := os.Open(filename)
	if err != nil {
		return 0, err
	}
	defer file.Close()

	var spid, ppid, gid, sid, tty, fgid int
	var flags, minflt, cminflt, majflt, cmajflt, utime, stime, cutime, cstime int64
	var cmd string
	var state byte

	_, err = fmt.Fscanf(file, "%d %s %c %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
		&spid,
		&cmd,
		&state,
		&ppid,
		&gid,
		&sid,
		&tty,
		&fgid,
		&flags,
		&minflt,
		&cminflt,
		&majflt,
		&cmajflt,
		&utime,
		&stime,
		&cutime,
		&cstime,
		)

	if err != nil {
		return 0, err
	}

	if pid != spid {
		return 0, fmt.Errorf("failed to read pid %d stats", pid)
	}

	return stime + utime + cstime + cutime, nil
}
