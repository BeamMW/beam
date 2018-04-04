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
