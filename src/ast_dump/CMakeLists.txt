set(SRCS
    main.cpp
)

add_executable(ast_dump ${SRCS})
target_include_directories(ast_dump PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(ast_dump PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/..")
target_link_libraries(ast_dump parser)
