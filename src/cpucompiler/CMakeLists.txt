set(SRCS
    main.cpp
)

add_executable(streamit-cpu-compiler ${SRCS})
target_include_directories(streamit-cpu-compiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(streamit-cpu-compiler common parser frontend streamgraph cputarget)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(llvm_libs support core executionengine mcjit native bitwriter)
    target_link_libraries(streamit-cpu-compiler ${llvm_libs})
endif()

# This is Linux-specific...
#target_link_libraries(streamit-cpu-compiler "-Wl,--whole-archive" cpuruntimelibrary "-Wl,--no-whole-archive")
target_sources(streamit-cpu-compiler PRIVATE $<TARGET_OBJECTS:cpuruntimelibrary>)

