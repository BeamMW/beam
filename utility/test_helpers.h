#pragma once
#include <chrono>
#include <stdint.h>
#include <stdio.h>

#ifndef WIN32
#	include <unistd.h>
#endif // WIN32

namespace beam { namespace helpers {

/// Simple thing for benchmarking pieces of tests
class StopWatch {
public:
    void start() {
        elapsed = 0;
        started = std::chrono::high_resolution_clock::now();
    }

    void stop() {
        auto diff = std::chrono::high_resolution_clock::now() - started;
        elapsed = std::chrono::duration_cast<std::chrono::microseconds>(diff).count();
    }

    uint64_t microseconds() const {
        return elapsed;
    }

    uint64_t milliseconds() const {
        return elapsed / 1000;
    }

    double seconds() const {
        return elapsed / 1000000.0;
    }

private:
    std::chrono::high_resolution_clock::time_point started;
    uint64_t elapsed;
};

// Used in tests, to prevent parallel execution of overlapping tests
inline bool ProcessWideLock(const char* szFilePath)
{
	printf("Acquiring test lock (%s)...\n", szFilePath);
	fflush(stdout);

#ifdef WIN32
	HANDLE hMutex = CreateMutexA(NULL, FALSE, szFilePath); // unix-style file path is ok, windows object namespace permits everything except backslashes
	if (!hMutex)
		return false;

	DWORD dw = WaitForSingleObject(hMutex, INFINITE);
	return (WAIT_OBJECT_0 == dw) || (WAIT_ABANDONED_0 == dw);

#else // WIN32

	int hFile = open(szFilePath, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
	if (-1 == hFile)
		return false;

	return !lockf(hFile, F_LOCK, 0);
#endif // WIN32
}


}} //namespaces
