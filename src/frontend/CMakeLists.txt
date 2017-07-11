if(NOT LLVM_FOUND)
    message(WARNING "LLVM not found, compile will likely fail.")
endif()

set(SRCS
    filter_function_builder.cpp
    context.cpp
    expression_builder.cpp
    filter_builder.cpp
    statement_builder.cpp
)

add_library(frontend
            ${SRCS}
            )

target_include_directories(frontend PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(frontend PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/..")
target_link_libraries(frontend parser)

if(LLVM_FOUND)
    target_compile_definitions(frontend PRIVATE "${LLVM_DEFINITIONS}")
    target_include_directories(frontend PRIVATE "${LLVM_INCLUDE_DIRS}")
    llvm_map_components_to_libnames(llvm_libs support core irreader)
    target_link_libraries(frontend ${llvm_libs})
endif()
