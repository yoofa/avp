# Repository Guidelines

## Project Structure & Module Organization

AVP is a C++20 audio/video player inspired by Android NuPlayer. Public interfaces live in `api/`; playback orchestration, decoders, renderers, and synchronization live in `core/`. Source implementations are under `content_source/` and `demuxer/`, while reusable audio, video, codec, and foundation components are in `media/`. Platform-facing SDK code is in `sdk/`, runnable programs are in `examples/`, and shared test support is in `test/`. GN definitions are colocated with their modules in `BUILD.gn` files. Treat `out/` as generated build output.

## Build, Test, and Development Commands

- `gn gen out/Default --export-compile-commands` generates Ninja files and a compilation database. Run it after changing any `BUILD.gn`, `.gni`, or build argument.
- `ninja -C out/Default` builds the default library and examples.
- `ninja -C out/Default player_unittests aac_offload_unittests` builds the core GoogleTest executables.
- `./out/Default/player_unittests` runs renderer, synchronization, and player tests; `./out/Default/aac_offload_unittests` runs compressed-audio tests.
- `./out/Default/avplay <media-file>` runs the local player example after a successful build.

Build only the smallest relevant target during iteration, then run the default build before submitting.

## Coding Style & Naming Conventions

Use C++20 and the repository's `.clang-tidy` configuration; warnings are treated as errors. Format C/C++ with clang-format 18, matching CI. Use two-space indentation and follow existing Google-style naming: `PascalCase` for classes and methods, `snake_case` for variables and files, and trailing underscores for data members. Prefer fixed-width numeric types such as `uint32_t` and `int64_t` over `int` or `signed` where size matters. Keep headers self-contained and use include guards.

## Testing Guidelines

Tests use GoogleTest and normally live beside the implementation as `*_unittest.cc`. Add regression coverage for decoder state changes, timestamp handling, buffering, and error fallback paths. Name tests by behavior, for example `OpenCompressedAudioSinkFallsBackOnFailure`. Run the directly affected suite and any aggregate executable that includes it.

## Commit & Pull Request Guidelines

Use concise, imperative commit subjects. Existing history accepts plain subjects (`Fix TS playback freeze`) and scoped prefixes (`audio: use GetTimestamp`, `fix: eliminate crash`). Keep each commit focused. Pull requests should explain the observed problem, implementation approach, affected formats/platforms, and exact build/test commands run. Link relevant issues and include logs or screenshots when playback behavior or UI output changes.
