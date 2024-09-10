cmake_minimum_required(VERSION 3.10)
project(StaticIOPlugin)

# Find the LLVM and Clang packages
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# Set the correct flags and include directories
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Specify the source files
set(SOURCES
    StaticIOPlugin.cpp
)

# Create the shared library for the Clang plugin
add_library(StaticIOPlugin SHARED ${SOURCES})

# Set the properties for the plugin
set_target_properties(StaticIOPlugin PROPERTIES
  COMPILE_FLAGS "${LLVM_CXXFLAGS}"
  LINK_FLAGS "${LLVM_LDFLAGS}"
)

# Link the Clang libraries
target_link_libraries(StaticIOPlugin
    clangTooling
    clangBasic
    clangASTMatchers
)

# Optionally, print out LLVM and Clang version information
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Found Clang ${CLANG_PACKAGE_VERSION}")
message(STATUS "Using ClangConfig.cmake in: ${CLANG_DIR}")