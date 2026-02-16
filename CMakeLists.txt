cmake_minimum_required(VERSION 3.22)
project(brokkr LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include spdlog as a dependency using FetchContent
include(FetchContent)
FetchContent_Declare(
   spdlog
   GIT_REPOSITORY https://github.com/gabime/spdlog.git
   GIT_TAG        v1.17.0
)

FetchContent_GetProperties(spdlog)
if (NOT spdlog_POPULATED)
    FetchContent_MakeAvailable(spdlog)
endif()

# Disable fmt tests to avoid pulling in extra dependencies
set(FMT_TEST OFF CACHE INTERNAL "disabling fmt tests")

# Include fmt as a dependency using FetchContent
FetchContent_Declare(
        fmt
        GIT_REPOSITORY  https://github.com/fmtlib/fmt.git
        GIT_TAG         12.1.0
        GIT_PROGRESS    TRUE
        USES_TERMINAL_DOWNLOAD TRUE
)

FetchContent_GetProperties(fmt)
if (NOT fmt_POPULATED)
    FetchContent_MakeAvailable(fmt)
endif()

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
endif()

target_include_directories(brokkr-platform INTERFACE src)
target_link_libraries(brokkr-platform INTERFACE spdlog::spdlog_header_only fmt::fmt-header-only)

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
    src/protocol/odin/pit_transfer.cpp)

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
target_link_libraries(brokkr PRIVATE Threads::Threads spdlog::spdlog_header_only fmt::fmt-header-only)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Core Gui Widgets)
if (Qt6_FOUND)
    message(STATUS "Qt6 found, enabling GUI target")
    add_executable(brokkr-gui WIN32 
        src-gui/main_gui.cpp 
        src-gui/brokkr_wrapper.cpp
    )
    target_link_libraries(brokkr-gui PRIVATE 
        Qt6::Core 
        Qt6::Gui 
        Qt6::Widgets
    )
    target_compile_options(brokkr-gui PRIVATE /Zc:__cplusplus)
    if(WIN32)
        # Find the windeployqt tool inside your Qt installation
        get_target_property(_qmake_exec Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake_exec}" DIRECTORY)
        find_program(WINDEPLOYQT_EXEC windeployqt HINTS "${_qt_bin_dir}")

        if(WINDEPLOYQT_EXEC)
            add_custom_command(TARGET brokkr-gui POST_BUILD
                COMMAND "${WINDEPLOYQT_EXEC}"
                # --compiler-runtime copies the C++ runtime DLLs too, making it fully portable!
                ARGS --no-translations --compiler-runtime "$<TARGET_FILE:brokkr-gui>"
                COMMENT "Running windeployqt to deploy Qt libraries..."
            )
        else()
            message(WARNING "windeployqt not found! You will need to copy DLLs manually.")
        endif()
    endif()
else()
    message(WARNING "Qt6 not found, GUI target will not be built")
endif()