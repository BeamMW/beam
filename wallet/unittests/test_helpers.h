// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

#define WALLET_CHECK_NO_THROW(s) \
try { \
    s; \
} catch(...) { \
    PrintFailure(#s, __FILE__, __LINE__); \
} \

#define WALLET_CHECK_THROW(s) \
try { \
    s; \
    PrintFailure(#s, __FILE__, __LINE__); \
} catch(...) { } \

#define WALLET_CHECK_RESULT g_failureCount ? -1 : 0;