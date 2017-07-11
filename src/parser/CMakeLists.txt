if(FLEX_FOUND AND BISON_FOUND)
    FLEX_TARGET(scanner scanner.l ${CMAKE_CURRENT_BINARY_DIR}/scanner.cpp DEFINES_FILE "${CMAKE_CURRENT_BINARY_DIR}/scanner.h")
    BISON_TARGET(parsery parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp DEFINES_FILE "${CMAKE_CURRENT_BINARY_DIR}/parser_tokens.h")
    ADD_FLEX_BISON_DEPENDENCY(scanner parsery)
else()
    message(WARNING "Bison/flex not found, scanner/parser will not be built.")
endif()

set(SRCS
    ast.cpp
    ast_dump.cpp
    ast_printer.cpp
    ast_visitor.cpp
    helpers.cpp
    parser_state.cpp
    scanner_helpers.cpp
    semantic_analysis.cpp
    type.cpp
)

add_library(parser
            ${SRCS}
            ${FLEX_scanner_OUTPUTS}
            ${BISON_parsery_OUTPUTS}
            )

target_include_directories(parser PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(parser PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/..")

