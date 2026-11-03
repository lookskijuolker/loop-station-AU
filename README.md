# Loop Station AU

A loop-station Audio Unit (AUv2) for **Logic Pro X 10.2.3** on an **Intel Mac running
macOS 10.12.6 Sierra**.

**Current stage: step 1, proof-of-concept.** This is not the looper yet. It is a stereo
(or mono) pass-through with one gain knob (-60…+12 dB, smoothed). It exists to prove the
whole path works: building on CI, downloading, installing, auval, and Logic.

To install it, see **[INSTALL.md](INSTALL.md)**.

| | |
|---|---|
| Format | AUv2 `.component`, type `aufx`, subtype `Lsg1`, manufacturer `Lsau` |
| Architecture | x86_64 only |
| Deployment target | macOS 10.12 |
| Framework | JUCE **7.0.12** (fetched by CMake) |
| Language | C++17 |
| Build | CMake ≥ 3.22, GitHub Actions on `macos-14` |

## Layout

```
CMakeLists.txt                   deployment target, arch, JUCE fetch, plugin definition
Source/PluginProcessor.{h,cpp}   audio: pass-through and smoothed gain
Source/PluginEditor.{h,cpp}      UI: one rotary knob
scripts/check_macos_compat.sh    static checks on the built binary (arch, min OS, bad symbols)
.github/workflows/build-au.yml   CI build and artifact upload
```

## Building locally (on any Mac with Xcode and CMake)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target LoopStationGain_AU
scripts/check_macos_compat.sh build/LoopStationGain_artefacts/Release/AU/LoopStationGain.component
```

To use a local JUCE checkout instead of downloading one, add
`-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

## Why these choices

- **JUCE 7.0.12 rather than JUCE 8.** JUCE 7.0.12 is the last JUCE 7 release. Its README
  lists macOS **10.9** as the minimum deployment target, so 10.12 is well inside what it
  supports. JUCE 8 changed a lot (a new text and graphics stack, and the AGPLv3 licence)
  and is not tested much on old macOS versions. A conservative, finished release is the
  safer base for a Sierra target.
- **C++17, not C++20.** JUCE 7 needs C++17. Most C++20 library features need a newer
  system libc++ than Sierra has (see below).
- **Deployment target and architecture are set before `project()`** as cache variables
  in `CMakeLists.txt`, and CI also passes them explicitly. JUCE's helper tool `juceaide`
  is configured separately and builds for the runner's own architecture, so
  cross-compiling for x86_64 on an Apple-silicon runner works.
- **Unix Makefiles generator, not Xcode.** This keeps the build simple and avoids Xcode
  project settings (code signing, "minimum supported deployment target" checks) that
  newer Xcode versions enforce.
- **No code signing.** Sierra and Logic 10.2.3 don't need signed AUs. Removing the
  quarantine flag (INSTALL.md) is enough.
- **Splash screen disabled** (`JUCE_DISPLAY_SPLASH_SCREEN=0`). JUCE allows this for GPL
  projects, and this repository is GPLv3.

## Sierra compatibility rules (for all future code)

The plugin uses the system's `/usr/lib/libc++.1.dylib`, and on 10.12 that is old. Code
that *compiles* with a modern SDK can still fail to *load* on Sierra with a dyld
"Symbol not found" error. Don't use:

| Avoid | Needs | Use instead |
|---|---|---|
| `std::filesystem` | 10.15 | `juce::File` |
| `std::optional::value()`, `std::get`/`std::visit` on `std::variant`, `std::any_cast` (they throw `bad_*` exceptions) | 10.13–10.14 | `has_value()` / `*opt`, `std::get_if`, or JUCE types |
| Heap allocation of over-aligned types (`alignas(32)` and `new`, aligned `operator new`) | 10.13 | `juce::HeapBlock`, or keep alignment ≤ 16 |
| `std::to_chars` / `std::from_chars` (floating-point) | 13.3+ | `juce::String` |
| `std::format` | 13.3 | `juce::String` |
| `std::atomic::wait/notify`, `std::latch`, `std::barrier`, `std::counting_semaphore`, `std::jthread` | 11.0 | `juce::WaitableEvent`, `juce::Thread`, plain atomics |
| `std::pmr::*` | 14.0 | JUCE containers, or preallocated buffers |
| Apple APIs newer than 10.12 without an `@available` guard | — | Guard them. `-Wunguarded-availability` warns about missing guards |

`scripts/check_macos_compat.sh` runs on every CI build. It fails the build if the binary
references any of the runtime symbols above, if the architecture is not x86_64 only, or
if the minimum OS recorded in the binary is not 10.12.

## Keep the framework list short

Every system framework the plugin links gets loaded with it, along with everything
*that* framework depends on. On the target Sierra Mac, **CoreAudioKit can't load**: it
pulls in Apple's private `MobileDevice.framework`, and that copy has been replaced by a
newer one that needs a function Sierra doesn't have (`_xpc_file_transfer_create_with_fd`).
`scripts/sierra_dlopen_test.py` showed this. Any AU that links CoreAudioKit fails to open
on that machine, and most JUCE AUs do. The rules that follow from this:

- **Never link CoreAudioKit.** The AUv2 wrapper only needs it when hosting other AUs
  (`JUCE_PLUGINHOST_AU`, which is off). `CMakeLists.txt` removes it from
  `juce_audio_plugin_client_AU`'s link list. This rules out hosting AUs inside the looper,
  and Apple's generic AU view, unless the system is repaired.
- WebKit is removed from `juce_gui_extra`'s link list. That module always links it, even
  with the web browser turned off, and `-dead_strip_dylibs` doesn't drop it.
- Link `juce::juce_audio_processors`, not `juce_audio_utils`. `juce_audio_utils` brings
  in `DiscRecording` and other modules we don't need.
- Keep `-Wl,-dead_strip_dylibs` on the AU target, for anything else that's unused.
- `check_macos_compat.sh` fails the build if CoreAudioKit, WebKit, DiscRecording,
  MobileDevice or UniformTypeIdentifiers is linked.
- Use `scripts/sierra_dlopen_test.py` on the target Mac to find which framework fails
  to load. It ships in every artifact.

## Risks and how they're handled

1. **No GitHub runner has an Xcode that officially supports 10.12.** Xcode 14 and later
   officially support deployment targets from macOS 10.13. The last Xcode that supported
   10.12 was 13.x, and the runners that had it are gone. The workflow picks the
   **oldest Xcode from 15.1 up on `macos-14`** (15.0.x is skipped: its linker has known
   bugs, and JUCE 7.0.12 refuses to build with it). Its clang and SDK still build correctly for
   `-mmacosx-version-min=10.12`, and the SDK still marks which libc++ features need a
   newer OS. That is why compile errors like "'value' is unavailable: introduced in
   macOS 10.13" show up, which is what we want. This works in practice, but Apple doesn't
   test it.
   *Handled by:* the static checks in CI, and the real test, `auval` plus Logic on the
   Sierra machine. *Fallback if a future build breaks:* build on a Mac with Xcode 13.2.1
   (runs on macOS 11/12), or set up a self-hosted runner.
2. **Runner images change.** GitHub will retire `macos-14` at some point. If that happens,
   `macos-15` (Xcode 16) or `macos-15-intel` (native x86_64, so auval runs without
   Rosetta) should still work. They are just further from the target, so keep an eye on
   `compat-check.txt`.
3. **CI can't run Sierra or Logic.** The CI `auval` step runs on a modern macOS under
   Rosetta and is marked *best effort*. It only proves the bundle is basically valid.
   It does not prove the plugin loads on 10.12, so always do the auval and Logic check
   in INSTALL.md on the real machine.
4. **Some problems only show at load time.** A missing libc++ symbol or an unguarded new
   API only appears when Sierra loads the plugin. The symbol denylist catches the known
   cases, but it can't be complete.
5. **The plugin identity must never change.** `Lsau`/`Lsg1`, the type `aufx` and the
   bundle ID are how Logic identifies the plugin. Changing any of them makes it a
   *different* plugin, and saved projects lose it. Bump `VERSION` in `CMakeLists.txt`
   for each release so Logic rescans.
6. **The plugin type for the real looper.** `aufx` effects don't receive MIDI in Logic.
   If the looper needs MIDI or footswitch control, it will probably need to be a
   *MIDI-controlled effect* (`aumf`, `kAudioUnitType_MusicEffect`). Decide this before
   anyone uses the looper in real projects, because of risk 5. The real looper will
   probably get a new plugin code anyway.
7. **JUCE 7 gets no more updates.** This is fine for a fixed, old target. We would only
   upgrade if we hit a bug.
8. **Downloading on Sierra.** Artifacts need a GitHub login and are kept for 90 days.
   Old Safari on 10.12 may not handle GitHub well. INSTALL.md describes copying the zip
   from another computer instead.
