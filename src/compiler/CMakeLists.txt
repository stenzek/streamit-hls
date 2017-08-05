set(SRCS
    main.cpp
)

add_executable(compiler ${SRCS})
target_include_directories(compiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../dep/spdlog/include")
target_include_directories(compiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(compiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/..")
target_link_libraries(compiler parser frontend)

# This is Linux-specific...
#target_link_libraries("-Wl,--whole-archive" runtime_library "-Wl,--no-whole-archive")
target_sources(compiler PRIVATE $<TARGET_OBJECTS:runtime_library>)

