set(SRCS
    main.cpp
)

add_executable(streamit-hls-compiler ${SRCS})
target_include_directories(streamit-hls-compiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(streamit-hls-compiler common parser frontend streamgraph hlstarget)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(llvm_libs support core bitwriter)
    target_link_libraries(streamit-hls-compiler ${llvm_libs})
endif()
