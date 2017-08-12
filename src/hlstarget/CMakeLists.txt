set(SRCS
    CBackend.cpp
    filter_builder.cpp
    program_builder.cpp
)

add_library(hlstarget
            ${SRCS}
            )

target_include_directories(hlstarget PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(hlstarget core frontend streamgraph parser)

set_source_files_properties(CBackend.cpp PROPERTIES COMPILE_FLAGS -fno-rtti)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(llvm_libs support core mc analysis codegen passes transformutils scalaropts target)
    target_link_libraries(hlstarget ${llvm_libs})
endif()

