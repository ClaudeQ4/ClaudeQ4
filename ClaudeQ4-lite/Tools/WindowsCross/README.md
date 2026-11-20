# How the ready-made Windows binaries are built

The `bin` folder of the Claude-Q 4 package is compiled on Linux, with the same compiler, runtime
and SDK a Windows developer would use with Visual Studio + Clang:

- compiler / linker: **clang-cl + lld-link 22.1.8** (official LLVM release)
- runtime and headers: **Microsoft C/C++ runtime and STL 14.52** and **Windows SDK 10.0.28000**
  (the x64 headers and libraries, in the layout of
  [windows-msvc-sysroot](https://github.com/trcrsired/windows-msvc-sysroot))
- **unmodified JUCE 9**, static runtime (`/MT`): the plug-in needs no Visual C++
  redistributable, only the system DLLs of Windows 10 / 11
- `moduleinfo.json` is produced by JUCE's own VST3 manifest helper

Checks run on the Windows binaries (under Wine 11):

- Steinberg **VST3 validator** (SDK 3.8.1): 47 tests passed, 0 failed
- the Claude-Q 4 test suite (`Tests/SnapshotMain.cpp`): processor, latency, state, presets,
  MIDI learn, mouse / keyboard editing, GUI rendering, and the VST3 loaded through a host
  (a +12 dB bell measures +12.00 dB)
- the DSP unit tests (`Tests/*Test.cpp`)

## Reproducing the build

```
# 1. LLVM 22 or newer (clang-cl, lld-link, llvm-rc, llvm-lib, llvm-mt), e.g. in /opt/llvm
# 2. the MSVC / Windows SDK sysroot, e.g. in /opt/windows-msvc-sysroot
# 3. symlinks for the mixed-case header / library names used by JUCE:
python3 make_case_overlay.py /opt/windows-msvc-sysroot /opt/windows-msvc-sysroot-case \
        ../JUCE/modules ../Source ../Tests ../ThirdParty
# 4. configure and build
cmake -S .. -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=clangcl-toolchain.cmake
cmake --build build-win --target ClaudeQ4_VST3 ClaudeQ4_Standalone
```

The paths can also be given as `CQ_LLVM_ROOT`, `CQ_MSVC_SYSROOT` and `CQ_CASE_OVERLAY`
(CMake variables or environment variables). On Windows itself none of this is needed:
`source\BUILD_FROM_SOURCE.bat` builds with Visual Studio.
