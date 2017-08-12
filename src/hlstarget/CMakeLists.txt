set(SRCS
    filter_builder.cpp
    program_builder.cpp
)

add_library(hlstarget
            ${SRCS}
            )

target_include_directories(hlstarget PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(hlstarget core frontend streamgraph parser)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(llvm_libs support core)
    target_link_libraries(hlstarget ${llvm_libs})
endif()

