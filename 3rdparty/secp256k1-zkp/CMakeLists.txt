set(TARGET_NAME secp256k1)

add_library(${TARGET_NAME} STATIC src/secp256k1.c)

set(COMMON_COMPILE_FLAGS ENABLE_MODULE_RECOVERY ENABLE_MODULE_ECDH ENABLE_MODULE_RANGEPROOF ENABLE_MODULE_GENERATOR USE_FIELD_INV_BUILTIN USE_NUM_NONE USE_SCALAR_INV_BUILTIN)
if (MSVC)
	set(COMPILE_FLAGS USE_FIELD_10X26 USE_SCALAR_8X32)
	set(COMPILE_OPTIONS "")
else()
	set(COMPILE_FLAGS USE_FIELD_10X26 USE_SCALAR_8X32 HAVE_BUILTIN_EXPECT)
	set(COMPILE_OPTIONS -O3 -W -std=c89 -pedantic -Wall -Wextra -Wcast-align -Wnested-externs -Wshadow -Wstrict-prototypes -Wno-unused-function -Wno-long-long -Wno-overlength-strings -fvisibility=hidden)
endif()

target_compile_definitions(${TARGET_NAME} PRIVATE  ${COMMON_COMPILE_FLAGS} ${COMPILE_FLAGS})
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)