if(NOT LLVM_FOUND)
    message(WARNING "LLVM not found, compile will likely fail.")
endif()

set(SRCS
)

add_library(codegen
            ${SRCS}
            )

target_include_directories(codegen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(codegen PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/..")
target_link_libraries(codegen parser)
