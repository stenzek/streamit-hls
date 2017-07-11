set(SRCS
    main.cpp
)

add_executable(compiler ${SRCS})
target_include_directories(compiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(compiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/..")
target_link_libraries(compiler parser frontend)
