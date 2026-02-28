cmake_minimum_required(VERSION 3.28)
project(FTPServer C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

# Option to enable code coverage (gcc/clang). When ON, builds with coverage flags.
option(ENABLE_COVERAGE "Enable coverage build (gcc/clang)" OFF)
if(ENABLE_COVERAGE)
    message(STATUS "Building with coverage flags")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-g -O0 -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    else()
        message(WARNING "Coverage enabled but compiler is not GCC/Clang; coverage flags not set")
    endif()
endif()

# Option to enable OpenSSL for TCP/TLS & FTPs
option(ENABLE_OPENSSL "Enable TLS Support (using OpenSSL) over TCP." OFF)
if (ENABLE_OPENSSL)
    message(STATUS "Building with OpenSSL flags")
    find_package(OpenSSL REQUIRED)
endif()

# Include header files.
include_directories(${PROJECT_SOURCE_DIR}/include)

set(SOURCES
    src/utils.c
    src/logger.c
    src/filesys.c
    src/filelock.c
    src/network.c
    src/protocol.c
    src/command.c
    src/session.c
    src/transfer.c
    src/server.c
    src/auth.c
    src/handler.c
)

# Create library: use shared library when coverage enabled to ensure coverage data is emitted
if(ENABLE_COVERAGE)
    add_library(ftpserver SHARED ${SOURCES})
else()
    add_library(ftpserver STATIC ${SOURCES})
endif()

# Link OpenSSL libraries if enabled
if(ENABLE_OPENSSL)
    target_compile_definitions(ftpserver PUBLIC ENABLE_OPENSSL)
    target_link_libraries(ftpserver PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

if(WIN32)
    target_link_libraries(ftpserver PUBLIC ws2_32)
endif()

# Main program
add_executable(server src/main.c)
target_link_libraries(server ftpserver)

enable_testing()

# Add test subdirectory.
add_subdirectory(test)