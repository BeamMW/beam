#pragma once

#include <iostream>

#define WALLET_TEST_INIT \
namespace {\
int g_failureCount = 0;\
void PrintFailure(const char* expression, const char* file, int line){\
    std::cout << "\"" << expression << "\"" << " assertion failed. File: " << file << " at line: " << line << "\n";\
    ++g_failureCount;\
}}\

#define WALLET_CHECK(s) \
do {\
    if (!(s)) {\
        PrintFailure(#s, __FILE__, __LINE__);\
    }\
} while(false)\

#define WALLET_CHECK_RESULT g_failureCount ? -1 : 0;