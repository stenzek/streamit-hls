set(SRCS
    log.cpp
    string_helpers.cpp
)

add_library(common ${SRCS})

target_include_directories(common PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
