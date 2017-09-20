set(SRCS
    io.cpp
    debug.cpp
    println.cpp
)

add_library(cpuruntimelibrary_static ${SRCS})
add_library(cpuruntimelibrary OBJECT ${SRCS})

target_include_directories(cpuruntimelibrary PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../")


