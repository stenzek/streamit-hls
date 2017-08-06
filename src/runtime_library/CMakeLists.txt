set(SRCS
    debug.cpp
    println.cpp
)

#add_library(runtime_library ${SRCS})
add_library(runtime_library OBJECT ${SRCS})

target_include_directories(runtime_library PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(runtime_library PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/..")

