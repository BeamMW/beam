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

template<typename A, typename... Args>
void ampersand_folded(A& a, Args&... args) {
    (a & ... & args);
}

#define SERIALIZE_ARGS(...) ampersand_folded(ar__, __VA_ARGS__);

#define SERIALIZE(...) \
    template<typename Ar__> void serialize(Ar__ &ar__) const { \
        SERIALIZE_ARGS(__VA_ARGS__); \
    } \
    template<typename Ar__> void serialize(Ar__ &ar__) { \
        SERIALIZE_ARGS(__VA_ARGS__); \
    }

#define SERIALIZE_EXTERNAL(TYPE, ARG, ...) \
    template<typename Ar__> void serialize(Ar__ &ar__, const TYPE& ARG) const { \
        SERIALIZE_ARGS(__VA_ARGS__); \
    } \
    template<typename Ar__> void serialize(Ar__ &ar__, TYPE& ARG) { \
        SERIALIZE_ARGS(__VA_ARGS__); \
    }
