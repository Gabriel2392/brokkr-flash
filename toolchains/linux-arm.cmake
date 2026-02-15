set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   zig cc)
set(CMAKE_CXX_COMPILER zig c++)

set(CMAKE_C_COMPILER_TARGET   arm-linux-gnueabihf)
set(CMAKE_CXX_COMPILER_TARGET arm-linux-gnueabihf)

set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex_a7")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex_a7")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
