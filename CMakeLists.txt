cmake_minimum_required(VERSION 3.22)
project(brokkr LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(brokkr
    src/main.cpp

    src/app/cli.cpp
    src/app/run.cpp
    src/app/interface.cpp
    src/app/md5_verify.cpp

    src/core/thread_pool.cpp
    src/core/signal_shield.cpp

    src/platform/linux/single_instance.cpp
    src/platform/linux/sysfs_usb.cpp
    src/platform/linux/usbfs_device.cpp
    src/platform/linux/usbfs_conn.cpp
    src/platform/linux/tcp_transport.cpp

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

target_compile_options(brokkr PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O3>

    $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>>:-march=native>
    $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>>:-mtune=native>
    $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-march=native>
    $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-mtune=native>

    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:Clang>>:-flto=full>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:Clang>>:-flto=full>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-flto>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:GNU>>:-flto>

    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-ffunction-sections>
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fdata-sections>
)

target_link_options(brokkr PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:Clang>>:-flto=full>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:Clang>>:-flto=full>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-flto>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:GNU>>:-flto>

    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections>
    $<$<CONFIG:Release>:-s>
)

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
