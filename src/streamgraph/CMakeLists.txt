set(SRCS
    streamgraph.cpp
    streamgraph_builder.cpp
    streamgraph_dump.cpp
    streamgraph_function_builder.cpp
)

add_library(streamgraph
            ${SRCS}
            )

target_include_directories(streamgraph PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(streamgraph frontend parser)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(llvm_libs support core executionengine mcjit native)
    target_link_libraries(streamgraph ${llvm_libs})
endif()
