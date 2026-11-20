# CMake toolchain: Windows x64 build on Linux with clang-cl + lld-link and the real
# Microsoft C/C++ runtime, STL and Windows SDK (same ABI and libraries as Visual Studio).
#
#   CQ_LLVM_ROOT     LLVM release with clang-cl / lld-link / llvm-rc / llvm-lib / llvm-mt (22 or newer)
#   CQ_MSVC_SYSROOT  headers + x64 import/static libraries of the MSVC toolset and Windows SDK
#                    (layout of github.com/trcrsired/windows-msvc-sysroot)
#   CQ_CASE_OVERLAY  symlinks for mixed-case header / library names (make_case_overlay.py)

set (CMAKE_SYSTEM_NAME Windows)
set (CMAKE_SYSTEM_VERSION 10.0)
set (CMAKE_SYSTEM_PROCESSOR AMD64)

foreach (v CQ_LLVM_ROOT CQ_MSVC_SYSROOT CQ_CASE_OVERLAY)
    if (NOT DEFINED ${v} AND DEFINED ENV{${v}})
        set (${v} "$ENV{${v}}")
    endif()
endforeach()
if (NOT CQ_LLVM_ROOT)
    set (CQ_LLVM_ROOT "/opt/llvm")
endif()
if (NOT CQ_MSVC_SYSROOT)
    set (CQ_MSVC_SYSROOT "/opt/windows-msvc-sysroot")
endif()
if (NOT CQ_CASE_OVERLAY)
    set (CQ_CASE_OVERLAY "/opt/windows-msvc-sysroot-case")
endif()
# try_compile projects re-read this file: pass the paths through
list (APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CQ_LLVM_ROOT CQ_MSVC_SYSROOT CQ_CASE_OVERLAY)

set (CMAKE_C_COMPILER   "${CQ_LLVM_ROOT}/bin/clang-cl")
set (CMAKE_CXX_COMPILER "${CQ_LLVM_ROOT}/bin/clang-cl")
set (CMAKE_LINKER       "${CQ_LLVM_ROOT}/bin/lld-link" CACHE FILEPATH "" FORCE)
set (CMAKE_AR           "${CQ_LLVM_ROOT}/bin/llvm-lib" CACHE FILEPATH "" FORCE)
set (CMAKE_RC_COMPILER  "${CQ_LLVM_ROOT}/bin/llvm-rc"  CACHE FILEPATH "" FORCE)
set (CMAKE_MT           "${CQ_LLVM_ROOT}/bin/llvm-mt"  CACHE FILEPATH "" FORCE)

set (CMAKE_C_COMPILER_TARGET   x86_64-pc-windows-msvc)
set (CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

set (_cq_inc "/imsvc ${CQ_MSVC_SYSROOT}/include/c++/msvcstl /imsvc ${CQ_MSVC_SYSROOT}/include /imsvc ${CQ_CASE_OVERLAY}/include")
# /Brepro: reproducible objects (no timestamps)
set (CMAKE_C_FLAGS_INIT   "${_cq_inc} /Brepro")
set (CMAKE_CXX_FLAGS_INIT "${_cq_inc} /Brepro")
set (CMAKE_RC_FLAGS_INIT  "-I ${CQ_MSVC_SYSROOT}/include -I ${CQ_CASE_OVERLAY}/include")

set (_cq_lib "/libpath:${CQ_MSVC_SYSROOT}/lib/x86_64-unknown-windows-msvc /libpath:${CQ_CASE_OVERLAY}/lib /Brepro")
set (CMAKE_EXE_LINKER_FLAGS_INIT    "${_cq_lib}")
set (CMAKE_SHARED_LINKER_FLAGS_INIT "${_cq_lib}")
set (CMAKE_MODULE_LINKER_FLAGS_INIT "${_cq_lib}")

set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
