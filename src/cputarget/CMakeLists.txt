set(SRCS
    channel_builder.cpp
    debug_print_builder.cpp
    filter_builder.cpp
    program_builder.cpp
)

add_library(cputarget
            ${SRCS}
            )

target_include_directories(cputarget PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(cputarget core frontend streamgraph parser)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(llvm_libs support core passes transformutils scalaropts target ipo)
    target_link_libraries(cputarget ${llvm_libs})
endif()

add_subdirectory(runtimelibrary)
