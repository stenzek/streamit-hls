cmake_minimum_required(VERSION 2.6)
project(StreamItCompiler)

# We want to use the same compiler flags to compile the generated C++ code, as the
# compiler is built with. Otherwise, the C++11 ABI we disable causes problems.
set(TARGET_CXX_FLAGS "")

# Apparently GCC6+ appends ABI-specific function names and gdb doesn't like it.
# Not being able to set breakpoints is annoying, so let's just turn it off.
#if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 6.0)
#    add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)
#    set(TARGET_CXX_FLAGS "${TARGET_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0")
#endif()

# Detect C++ version support.
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    include(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
    CHECK_CXX_COMPILER_FLAG("-std=c++14" COMPILER_SUPPORTS_CXX14)
    if(COMPILER_SUPPORTS_CXX14)
        message("Enabling C++14 support.")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
        set(TARGET_CXX_FLAGS "${TARGET_CXX_FLAGS} -std=c++14")
    elseif(COMPILER_SUPPORTS_CXX14)
        message(WARNING "No C++14 support, enabling C++11 support.")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
        set(TARGET_CXX_FLAGS "${TARGET_CXX_FLAGS} -std=c++11")
    else()
        message(WARNING "No C++11 support. This may cause issues.")
    endif()
endif()

# Flex/Bison for building parser.
find_package(FLEX)
find_package(BISON)

# LLVM
find_package(LLVM)

enable_testing()
add_subdirectory(dep)
add_subdirectory(src)
