set(SRCS
    CBackend.cpp
    component_generator.cpp
    component_test_bench_generator.cpp
    filter_builder.cpp
    project_generator.cpp
    test_bench_generator.cpp
    vhdl_helpers.cpp
)

add_library(hlstarget
            ${SRCS}
            )

target_include_directories(hlstarget PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(hlstarget frontend streamgraph parser)

set_source_files_properties(CBackend.cpp PROPERTIES COMPILE_FLAGS -fno-rtti)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(llvm_libs support core mc analysis codegen passes transformutils scalaropts target ipo)
    target_link_libraries(hlstarget ${llvm_libs})
endif()

