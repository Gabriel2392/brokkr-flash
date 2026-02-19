cmake_minimum_required(VERSION 3.22)
project(Brokkr LANGUAGES C CXX VERSION 1.3.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------
# Dependencies (FetchContent)
# -----------------------------
include(FetchContent)

set(FMT_TEST OFF CACHE BOOL "disabling fmt tests" FORCE)

FetchContent_Declare(
   spdlog
   GIT_REPOSITORY https://github.com/gabime/spdlog.git
   GIT_TAG        v1.17.0
   GIT_PROGRESS    TRUE
   USES_TERMINAL_DOWNLOAD TRUE
)
FetchContent_GetProperties(spdlog)
if (NOT spdlog_POPULATED)
    FetchContent_MakeAvailable(spdlog)
endif()

# Important: Match the fmt version with spdlog's bundled version to avoid ODR issues.
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

# -----------------------------------------------
# Enable Hot Reload for MSVC compilers if supported
# -----------------------------------------------
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,\
$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,\
$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# ---------------------------
# Check modern C++ features
# ---------------------------
include(CheckCXXSourceCompiles)

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

# -------------------------------------------------------------------
# std::move_only_function compatibility
# -------------------------------------------------------------------
set(MOF_SRC
"#include <functional>
int main() { std::move_only_function<void()> f; return 0; }")
check_cxx_source_compiles("${MOF_SRC}" HAS_STD_MOVE_ONLY_FUNCTION)

if (NOT HAS_STD_MOVE_ONLY_FUNCTION)
    message(STATUS "std::move_only_function not available; enabling polyfill via function2")

    FetchContent_Declare(
        function2
        GIT_REPOSITORY https://github.com/Naios/function2.git
        GIT_TAG        4.2.2
    )
    FetchContent_GetProperties(function2)
    if (NOT function2_POPULATED)
        FetchContent_MakeAvailable(function2)
    endif()

    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(BROKKR_MOF_SHIM "${CMAKE_CURRENT_BINARY_DIR}/generated/brokkr_move_only_function_shim.hpp")

    file(WRITE "${BROKKR_MOF_SHIM}" [==[
#pragma once
#include <functional>
#include <function2/function2.hpp>

namespace std {
template <class Sig>
using move_only_function = fu2::unique_function<Sig>;
} // namespace std
]==])
endif()

# -----------------------------------------
# Platform-specific source files and definitions
# -----------------------------------------
if (LINUX OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(brokkr-platform INTERFACE)
    target_compile_definitions(brokkr-platform INTERFACE BROKKR_PLATFORM_LINUX)
    target_sources(brokkr-platform INTERFACE
        src/platform/posix-common/signal_shield.cpp
        src/platform/posix-common/single_instance.cpp
        src/platform/posix-common/tcp_transport.cpp
        src/platform/linux/sysfs_usb.cpp
        src/platform/linux/usbfs_device.cpp
        src/platform/linux/usbfs_conn.cpp
    )
elseif (WIN32)
    add_library(brokkr-platform INTERFACE)
    target_compile_definitions(brokkr-platform INTERFACE
        BROKKR_PLATFORM_WINDOWS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_sources(brokkr-platform INTERFACE
        src/platform/windows/signal_shield.cpp
        src/platform/windows/single_instance.cpp
        src/platform/windows/sysfs_usb.cpp
        src/platform/windows/usbfs_device.cpp
        src/platform/windows/usbfs_conn.cpp
        src/platform/windows/tcp_transport.cpp
    )
elseif (APPLE)
    add_library(brokkr-platform INTERFACE)
    target_compile_definitions(brokkr-platform INTERFACE BROKKR_PLATFORM_MACOS)
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
    src/third_party/md5/md5.c
    src/third_party/lz4/lz4.c
    src/protocol/odin/odin_cmd.cpp
    src/protocol/odin/pit.cpp
    src/protocol/odin/flash.cpp
    src/protocol/odin/group_flasher.cpp
    src/protocol/odin/pit_transfer.cpp
    src/app/md5_verify.cpp
)

include(TestBigEndian)
test_big_endian(BROKKR_IS_BIG_ENDIAN)
if (BROKKR_IS_BIG_ENDIAN)
    target_compile_definitions(brokkr-lib INTERFACE BROKKR_BIG_ENDIAN)
endif()

# ---------------------------
# Qt6 Detection
# ---------------------------
find_package(Qt6 6.3 COMPONENTS Core Gui Widgets)

if (NOT Qt6_FOUND)
    message(FATAL_ERROR "Qt6 is required for the GUI target. Please install Qt6 and try again.")
endif()
message(STATUS "Qt6 found, enabling GUI target 'brokkr'")

# ── macOS: strip frameworks the SDK no longer ships ───────────────
# macOS 14+/15+ SDK removed AGL.framework.  Qt's imported targets
# and .prl files may still reference it.  Walk EVERY imported
# target and surgically remove entries that mention a missing
# framework.  Uses string(REPLACE) to catch both single-element
# ("-framework AGL") and two-element ("-framework" ; "AGL") forms.
# ------------------------------------------------------------------
if (APPLE)
    set(_BROKKR_MISSING_FW "")
    foreach(_fw IN ITEMS AGL)
        find_library(_brokkr_probe_${_fw} ${_fw})
        if (NOT _brokkr_probe_${_fw})
            list(APPEND _BROKKR_MISSING_FW ${_fw})
            message(STATUS "${_fw}.framework not in SDK – stripping from all imported targets")
        endif()
        unset(_brokkr_probe_${_fw} CACHE)
    endforeach()

    if (_BROKKR_MISSING_FW)
        function(_brokkr_strip_fw tgt)
            if (NOT TARGET ${tgt})
                return()
            endif()
            foreach(prop IN ITEMS INTERFACE_LINK_LIBRARIES INTERFACE_LINK_OPTIONS)
                get_target_property(_val ${tgt} ${prop})
                if (NOT _val)
                    continue()
                endif()
                set(_orig "${_val}")
                foreach(_fw IN LISTS _BROKKR_MISSING_FW)
                    # Single-element: "-framework AGL"
                    string(REPLACE ";-framework ${_fw}" "" _val "${_val}")
                    string(REPLACE "-framework ${_fw};" "" _val "${_val}")
                    string(REPLACE "-framework ${_fw}"  "" _val "${_val}")
                    # Two-element: "-framework" ; "AGL"  (semicolon-separated)
                    string(REPLACE ";-framework;${_fw}" "" _val "${_val}")
                    string(REPLACE "-framework;${_fw};" "" _val "${_val}")
                    string(REPLACE "-framework;${_fw}"  "" _val "${_val}")
                endforeach()
                if (NOT "${_val}" STREQUAL "${_orig}")
                    message(STATUS "  stripped missing framework(s) from ${tgt}::${prop}")
                endif()
                set_target_properties(${tgt} PROPERTIES ${prop} "${_val}")
            endforeach()
        endfunction()

        # Walk every imported target (catches Qt6::GuiPrivate, Qt6::Platform, etc.)
        get_property(_all_imported DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY IMPORTED_TARGETS)
        foreach(_tgt IN LISTS _all_imported)
            _brokkr_strip_fw(${_tgt})
        endforeach()
    endif()
endif()
# ── end macOS framework fixup ─────────────────────────────────────

if (WIN32)
    add_executable(brokkr WIN32
        src-gui/main_gui.cpp
        src-gui/brokkr_wrapper.hpp
        src-gui/brokkr_wrapper.cpp
        assets/icon.rc
        assets/brokkr.qrc
    )
elseif (APPLE)
    add_executable(brokkr MACOSX_BUNDLE
        src-gui/main_gui.cpp
        src-gui/brokkr_wrapper.hpp
        src-gui/brokkr_wrapper.cpp
        assets/brokkr.qrc
    )
else()
    add_executable(brokkr
        src-gui/main_gui.cpp
        src-gui/brokkr_wrapper.hpp
        src-gui/brokkr_wrapper.cpp
        assets/brokkr.qrc
    )
endif()

find_package(Threads REQUIRED)
target_include_directories(brokkr PRIVATE src src-gui)

if (NOT HAS_STD_MOVE_ONLY_FUNCTION)
    target_include_directories(brokkr PRIVATE "${function2_SOURCE_DIR}/include")
    if (MSVC)
        target_compile_options(brokkr PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/FI${BROKKR_MOF_SHIM}>
        )
    else()
        target_compile_options(brokkr PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-include>
            $<$<COMPILE_LANGUAGE:CXX>:${BROKKR_MOF_SHIM}>
        )
    endif()
endif()

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

set_target_properties(brokkr PROPERTIES AUTOMOC ON AUTORCC ON)

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

if (NOT WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(brokkr PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
    target_compile_options(brokkr PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo>:-O3 -ffunction-sections -fdata-sections>
    )
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED)
if (LTO_SUPPORTED)
    message(STATUS "Enabling LTO (Link time optimization)")
    set_target_properties(brokkr PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Versioning
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

if (CMAKE_BUILD_TYPE)
    message(STATUS "Version: v${PROJECT_VERSION}+${BROKKR_COMMIT_COUNT}-${CMAKE_BUILD_TYPE}")
else()
    message(STATUS "Version: v${PROJECT_VERSION}+${BROKKR_COMMIT_COUNT}")
endif()

target_compile_definitions(brokkr PRIVATE
    BROKKR_COMMIT_COUNT=\"${BROKKR_COMMIT_COUNT}\"
    BROKKR_BUILD_TYPE=\"$<IF:$<CONFIG:Debug>,dbg,rel>\"
    BROKKR_VERSION=\"${PROJECT_VERSION}\"
)

qt_generate_deploy_app_script(
    TARGET brokkr
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
install(TARGETS brokkr
    RUNTIME DESTINATION bin
    BUNDLE  DESTINATION .
)

# ── Tests ─────────────────────────────────────────────────────────────
enable_testing()

add_executable(test_md5
    tests/test_md5.cpp
    src/third_party/md5/md5.c
)
target_include_directories(test_md5 PRIVATE src)
add_test(NAME md5 COMMAND test_md5)

add_executable(test_endian tests/test_endian.cpp)
target_include_directories(test_endian PRIVATE src)
add_test(NAME endian COMMAND test_endian)

# ── CPack ─────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-Source Implementation of Samsung Odin3")
set(CPACK_PACKAGE_CONTACT "roynatech@gmail.com")
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}-v${CPACK_PACKAGE_VERSION}")

# Resource files – guarded so missing files don't break CPack
if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/INSTALLER_README.txt")
  set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/assets/INSTALLER_README.txt")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/INSTALLER_WELCOME.txt")
  set(CPACK_RESOURCE_FILE_WELCOME "${CMAKE_SOURCE_DIR}/assets/INSTALLER_WELCOME.txt")
endif()

if(WIN32)
  list(APPEND CPACK_GENERATOR NSIS)
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

  # Installer / uninstaller icons
  if(EXISTS "${CMAKE_SOURCE_DIR}/assets/brokkr.ico")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/assets/brokkr.ico")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/assets/uninstall.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/assets/uninstall.ico")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/assets/brokkr.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/assets/brokkr.ico")
  endif()

  set(CPACK_NSIS_DISPLAY_NAME "The ${PROJECT_NAME} Project")
  set(CPACK_NSIS_WELCOME_TITLE "Welcome to The ${PROJECT_NAME} Project Installer")
  set(CPACK_NSIS_BRANDING_TEXT "Open source Samsung Odin, in your hands")
  set(CPACK_NSIS_MANIFEST_DPI_AWARE TRUE)
  set(CPACK_PACKAGE_EXECUTABLES "brokkr" "Brokkr")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "bin/brokkr.exe")

  # Bundle VC++ Redistributable
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

if (LINUX OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND CPACK_GENERATOR DEB)
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Soo Hwan Na <roynatech@gmail.com>")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
endif()

set(CPACK_PACKAGE_FILE_NAME
    "${PROJECT_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)

include(CPack)
