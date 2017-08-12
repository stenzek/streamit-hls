set(SRCS
    type.cpp
    wrapped_llvm_context.cpp
)

add_library(core ${SRCS})

target_include_directories(core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(core common)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(llvm_libs support core)
    target_link_libraries(core ${llvm_libs})
endif()
