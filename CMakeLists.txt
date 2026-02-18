cmake_minimum_required(VERSION 3.22)
project(Brokkr LANGUAGES C CXX VERSION 1.1.0)

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

# Check modern C++ features
include(CheckCXXSourceCompiles)
# Check __cpp_lib_byteswap
set(BYTESWAP_SRC
"#include <version>
#if defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 201907L
#include <bit>
int main() {
    std::byteswap(0x12345678);
    return 0;
}
#else
#error \"__cpp_lib_byteswap not supported\"
#endif")
check_cxx_source_compiles("${BYTESWAP_SRC}" HAS_BYTESWAP)
if (NOT HAS_BYTESWAP)
    message(FATAL_ERROR "Compiler does not support __cpp_lib_byteswap, which is required")
endif()

# Platform-specific source files and definitions
if (LINUX)
    add_compile_definitions(
        BROKKR_PLATFORM_LINUX
    )
    add_library(brokkr-platform INTERFACE)
    target_sources(brokkr-platform INTERFACE
        src/platform/posix-common/signal_shield.cpp
        src/platform/posix-common/single_instance.cpp
        src/platform/posix-common/tcp_transport.cpp
        src/platform/linux/sysfs_usb.cpp
        src/platform/linux/usbfs_device.cpp
        src/platform/linux/usbfs_conn.cpp
    )
elseif (WIN32)
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
elseif (APPLE)
    add_compile_definitions(
        BROKKR_PLATFORM_MACOS
    )
    add_library(brokkr-platform INTERFACE)
    target_sources(brokkr-platform INTERFACE
        src/platform/posix-common/signal_shield.cpp
        src/platform/posix-common/single_instance.cpp
        src/platform/posix-common/tcp_transport.cpp      
        src/platform/macos/sysfs_usb.cpp
        src/platform/macos/usbfs_device.cpp
        src/platform/macos/usbfs_conn.cpp
        
    )
    target_link_libraries(brokkr-platform INTERFACE
        "-framework IOKit"
        "-framework CoreFoundation"
    )
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

target_include_directories(brokkr-platform INTERFACE src)
target_link_libraries(brokkr-platform INTERFACE spdlog::spdlog_header_only fmt::fmt-header-only)

add_library(brokkr-lib INTERFACE)
target_sources(brokkr-lib INTERFACE
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
    
    # Moved from executable to lib as per new file
    src/app/md5_verify.cpp
)

# Qt6 Detection (Old style: Optional, prevents configuration error if missing)
find_package(Qt6 6.3 COMPONENTS Core Gui Widgets)

if (Qt6_FOUND)
    message(STATUS "Qt6 found, enabling GUI target 'brokkr'")
    
    # New Target Files: GUI Sources defined as 'brokkr'
    if (WIN32)
        add_executable(brokkr WIN32 
            src-gui/main_gui.cpp 
            src-gui/brokkr_wrapper.hpp
            src-gui/brokkr_wrapper.cpp
            assets/icon.rc
        )
    else()
        add_executable(brokkr 
            src-gui/main_gui.cpp
            src-gui/brokkr_wrapper.hpp
            src-gui/brokkr_wrapper.cpp
        )
    endif()

    find_package(Threads REQUIRED)
    target_include_directories(brokkr PRIVATE src src-gui src/lz4)
    target_link_libraries(brokkr PRIVATE 
        Threads::Threads 
        spdlog::spdlog_header_only 
        fmt::fmt-header-only 
        brokkr-platform 
        brokkr-lib
        Qt6::Core 
        Qt6::Gui 
        Qt6::Widgets
    )

    # Enable Qt MOC (Meta-Object Compiler) for Q_OBJECT macros
    set_target_properties(brokkr PROPERTIES
        AUTOMOC ON
    )

    if (MSVC)
        target_compile_options(brokkr PRIVATE /Zc:__cplusplus)
    endif()

    if(WIN32)
        get_target_property(_qmake_exec Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake_exec}" DIRECTORY)
        find_program(WINDEPLOYQT_EXEC windeployqt HINTS "${_qt_bin_dir}")

        if(WINDEPLOYQT_EXEC)
            add_custom_command(TARGET brokkr POST_BUILD
                COMMAND "${WINDEPLOYQT_EXEC}"
                ARGS --no-translations --compiler-runtime "$<TARGET_FILE:brokkr>"
                COMMENT "Running windeployqt to deploy Qt libraries..."
            )
        else()
            message(WARNING "windeployqt not found! You will need to copy DLLs manually.")
        endif()
    endif()

    # Optimization block from Old file, applied to brokkr
    if (NOT WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_BUILD_TYPE AND CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
        target_compile_definitions(brokkr PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
        if (NOT CMAKE_CROSSCOMPILING)
             target_compile_options(brokkr PRIVATE -march=native -mtune=native)
        endif()
        target_compile_options(brokkr PRIVATE -O3 -ffunction-sections -fdata-sections)
    endif()

    # LTO block
    if (CMAKE_BUILD_TYPE AND CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
        include(CheckIPOSupported)
        check_ipo_supported(RESULT LTO_SUPPORTED)
        if (LTO_SUPPORTED)
            message(STATUS "Enabling LTO (Link time optimization)")
            set_target_properties(brokkr PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
            set_target_properties(brokkr-platform PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
            set_target_properties(brokkr-lib PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()
    endif()

    # Versioning
    set(BROKKR_BUILD_TYPE)
    set(BROKKR_BUILD_TYPE_dbg "dbg")
    set(BROKKR_BUILD_TYPE_rel "rel")

    if (CMAKE_BUILD_TYPE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(BROKKR_BUILD_TYPE ${BROKKR_BUILD_TYPE_dbg})
    else()
      set(BROKKR_BUILD_TYPE ${BROKKR_BUILD_TYPE_rel})
    endif()

    execute_process(
      COMMAND git rev-list --count ${PROJECT_VERSION}..HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE BROKKR_COMMIT_COUNT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(NOT BROKKR_COMMIT_COUNT)
      set(BROKKR_COMMIT_COUNT "0")
    endif()

    message(STATUS "Version: v${PROJECT_VERSION}+${BROKKR_COMMIT_COUNT}-${BROKKR_BUILD_TYPE}")
    target_compile_definitions(brokkr PRIVATE
      BROKKR_COMMIT_COUNT=\"${BROKKR_COMMIT_COUNT}\"
      BROKKR_BUILD_TYPE=\"${BROKKR_BUILD_TYPE}\"
      BROKKR_VERSION=\"${PROJECT_VERSION}\"
    )

    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)

    # Install rules
    
    # Generate deploy script for Qt applications
    qt_generate_deploy_app_script(
        TARGET brokkr
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )

    install(SCRIPT ${deploy_script})
    install(TARGETS brokkr RUNTIME DESTINATION bin)

else()
    message(WARNING "Qt6 NOT FOUND. The 'brokkr' target (now GUI-based) cannot be built.")
    message(NOTICE "To fix this, ensure Qt6 is installed and CMAKE_PREFIX_PATH is set correctly.")
    message(FATAL_ERROR "Qt6 is required for the GUI target. Please install Qt6 and try again.")
endif()


# Packaging with CPack (Old Config)
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-Source Implementation of Samsung Odin3")
set(CPACK_PACKAGE_CONTACT "roynatech@gmail.com")
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}-v${CPACK_PACKAGE_VERSION}")
set(CPACK_PACKAGE_ICON ${CMAKE_SOURCE_DIR}/assets/brokkr.ico)
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
set(CPACK_RESOURCE_FILE_README ${CMAKE_SOURCE_DIR}/assets/INSTALLER_README.txt)
set(CPACK_RESOURCE_FILE_WELCOME ${CMAKE_SOURCE_DIR}/assets/INSTALLER_WELCOME.txt)

if(WIN32)
  # NSIS
  list(APPEND CPACK_GENERATOR NSIS)
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MUI_ICON ${CMAKE_SOURCE_DIR}/assets/brokkr.ico)
  set(CPACK_NSIS_MUI_UNIICON ${CMAKE_SOURCE_DIR}/assets/uninstall.ico)
  set(CPACK_NSIS_DISPLAY_NAME "The ${PROJECT_NAME} Project")
  set(CPACK_NSIS_WELCOME_TITLE "Welcome to The ${PROJECT_NAME} Project Installer")
  set(CPACK_NSIS_BRANDING_TEXT "Open source Samsung Odin, in your hands")
  set(CPACK_NSIS_MANIFEST_DPI_AWARE TRUE)

  # Start Menu shortcut + Add/Remove Programs icon
  set(CPACK_PACKAGE_EXECUTABLES "brokkr" "Brokkr")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "bin/brokkr.exe")

  # Bundle and silently install VC++ Redistributable if found
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_vcredist_name "vc_redist.x64.exe")
  else()
    set(_vcredist_name "vc_redist.x86.exe")
  endif()

  if(DEFINED ENV{VCToolsRedistDir})
    file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" _vcredist_dir)
    set(_vcredist_path "${_vcredist_dir}/${_vcredist_name}")
    if(EXISTS "${_vcredist_path}")
      message(STATUS "Found vcredist: ${_vcredist_path}")
      install(FILES "${_vcredist_path}" DESTINATION redist)
      set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
          ExecWait '\\\"$INSTDIR\\\\redist\\\\${_vcredist_name}\\\" /install /quiet /norestart'
      ")
    else()
      message(WARNING "VCToolsRedistDir set but ${_vcredist_name} not found at: ${_vcredist_path}")
    endif()
  endif()
endif()

if (LINUX)
  # DEB
  list(APPEND CPACK_GENERATOR DEB)
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Soo Hwan Na <roynatech@gmail.com>")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
endif()

set(CPACK_PACKAGE_FILE_NAME 
    "${PROJECT_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)

# Include CPack
include(CPack)
