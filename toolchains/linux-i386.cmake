set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_AR     llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)

set(CMAKE_C_FLAGS_INIT           "-m32")
set(CMAKE_CXX_FLAGS_INIT         "-m32")
set(CMAKE_EXE_LINKER_FLAGS_INIT  "-m32 -fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32 -fuse-ld=lld")
