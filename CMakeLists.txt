cmake_minimum_required(VERSION 3.22)
project(brokkr LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Platform-specific source files and definitions
if (LINUX)
    add_compile_definitions(
        BROKKR_PLATFORM_LINUX
    )
    add_library(brokkr-platform INTERFACE)
    target_sources(brokkr-platform INTERFACE
        src/platform/linux/signal_shield.cpp
        src/platform/linux/single_instance.cpp
        src/platform/linux/sysfs_usb.cpp
        src/platform/linux/usbfs_device.cpp
        src/platform/linux/usbfs_conn.cpp
        src/platform/linux/tcp_transport.cpp
    )
    target_include_directories(brokkr-platform INTERFACE src)
endif()
if (WIN32)
    add_compile_definitions(
        BROKKR_PLATFORM_WINDOWS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    add_library(brokkr-platform INTERFACE)
    target_sources(brokkr-platform INTERFACE
        src/platform/windows/signal_shield.cpp
        src/platform/windows/single_instance.cpp
        src/platform/windows/sysfs_usb.cpp
        src/platform/windows/usbfs_device.cpp
        src/platform/windows/usbfs_conn.cpp
        src/platform/windows/tcp_transport.cpp
    )
    target_include_directories(brokkr-platform INTERFACE src)
endif()


add_executable(brokkr
    src/main.cpp

    src/app/cli.cpp
    src/app/run.cpp
    src/app/interface.cpp
    src/app/md5_verify.cpp

    src/core/thread_pool.cpp

    src/io/tar.cpp
    src/io/source.cpp
    src/io/lz4_frame.cpp

    src/crypto/md5.c
    src/lz4/lz4.c

    src/protocol/odin/odin_cmd.cpp
    src/protocol/odin/pit.cpp
    src/protocol/odin/flash.cpp
    src/protocol/odin/group_flasher.cpp
    src/protocol/odin/pit_transfer.cpp
)

target_include_directories(brokkr PRIVATE src src/lz4)

target_compile_definitions(brokkr PRIVATE
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
)

if (NOT MSVC)
target_compile_options(brokkr PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O3>

    $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>>:-march=native>
    $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>>:-mtune=native>
    $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-march=native>
    $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-mtune=native>

    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-ffunction-sections>
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fdata-sections>
)
endif()

target_link_libraries(brokkr PRIVATE brokkr-platform)

# Check LTO support and enable it if available
include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED)
if (LTO_SUPPORTED)
  message(STATUS "Enabling LTO (Link time optimization)")
  set_target_properties(brokkr PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

execute_process(
  COMMAND git rev-list --count HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE BROKKR_COMMIT_COUNT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT BROKKR_COMMIT_COUNT)
  set(BROKKR_COMMIT_COUNT "0")
endif()

target_compile_definitions(brokkr PRIVATE
  BROKKR_COMMIT_COUNT=\"${BROKKR_COMMIT_COUNT}\"
)

find_package(Threads REQUIRED)
target_link_libraries(brokkr PRIVATE Threads::Threads)
