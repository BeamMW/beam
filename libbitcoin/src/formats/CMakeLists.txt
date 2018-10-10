file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/formats/*.hpp")

add_library(libbitcoin_formats OBJECT ${SOURCES} ${HEADERS})