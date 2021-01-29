# Top Level CMakeLists.txt
if (LINUX)
    cmake_minimum_required(VERSION 3.10)
else ()
    cmake_minimum_required(VERSION 3.16)
endif ()

project(jak)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_CXX_STANDARD 17)

# Set default compile flags for GCC
# optimization level can be set here. Note that game/ overwrites this for building game C++ code.
if (LINUX)
    message(STATUS "GCC detected, adding compile flags")
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} \
        -Wall \
        -Winit-self \
        -ggdb \
        -Wextra \
        -Wno-cast-align \
        -Wcast-qual \
        -Wdisabled-optimization \
        -Wformat \
        -Wmissing-include-dirs \
        -Woverloaded-virtual \
        -Wredundant-decls \
        -Wshadow \
        -Wsign-promo")
elseif(UNIX)
    message(STATUS "MacOS detected, adding compile flags")

else ()
    set(CMAKE_CXX_FLAGS "/EHsc")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:10000000")
endif (LINUX)



IF (WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
ENDIF ()

IF (ASAN_BUILD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -O1")
    message(STATUS "Doing ASAN build")
ENDIF ()

option(CODE_COVERAGE "Enable Code Coverage Compiler Flags" OFF)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/third-party/cmake/modules/)

if(LINUX AND CODE_COVERAGE)
  include(CodeCoverage)
  append_coverage_compiler_flags()
  message("Code Coverage build is enabled!")
else()
  message("Code Coverage build is disabled!")
endif()

# includes relative to top level jak-project folder
include_directories(./)

include_directories(SYSTEM third-party/inja)

# build common library
add_subdirectory(common)

# build decompiler
add_subdirectory(decompiler)

# build the game code in C++
add_subdirectory(game)

# build the compiler
add_subdirectory(goalc)

# build standalone tools
add_subdirectory(tools)

# build the gtest libraries
add_subdirectory(third-party/googletest)

# build tests
add_subdirectory(test)

# build minilzo library
add_subdirectory(third-party/minilzo)

# build format library
add_subdirectory(third-party/fmt)

# build zydis third party library for disassembling x86
option(ZYDIS_BUILD_TOOLS "" OFF)
option(ZYDIS_BUILD_EXAMPLES "" OFF)
option(ZYDIS_BUILD_SHARED_LIB "" ON)
add_subdirectory("third-party/zydis")


# windows memory management lib
IF (WIN32)
    add_subdirectory(third-party/mman)
ENDIF ()
