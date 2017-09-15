set(SRCS
    constant_expression_builder.cpp
    expression_builder.cpp
    function_builder.cpp
    state_variables_builder.cpp
    statement_builder.cpp
    wrapped_llvm_context.cpp
)

add_library(frontend
            ${SRCS}
            )

target_include_directories(frontend PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(frontend parser)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(llvm_libs support core)
    target_link_libraries(frontend ${llvm_libs})
endif()
