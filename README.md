# libsxcftar

Small C++17 static library for CFtar configuration archives, extracted from
[Smithproxy](https://github.com/astibal/smithproxy) master at
`ae0415f69624618d7c611ff7a336280352a50f5b`.

It provides bounded TAR parsing and writing, manifest validation, archive
sessions, file locking, atomic configuration packing and runtime trees.
The existing `sx::cfg` API and `service/cfgapi/*.hpp` include paths are retained.
The output is `libsxcftar.a`; there is no dependency on Socle or the proxy runtime.

The archive format remains `smithproxy-cftar` version 1, including its required
`smithproxy.cfg` and `smithproxy.startup.cfg` entries and resource namespaces.
This extraction does not change format policy. Other root-level `.cfg` files
are supported. `CfgTar` can also be used directly without manifest validation.
The Smithproxy CLI and its default templates remain in Smithproxy.

## Build and test

Linux, CMake >= 3.16 and a C++17 compiler are required. Dependencies are
libarchive, OpenSSL Crypto, libconfig++, nlohmann_json and pthreads.
GoogleTest is required only when tests are enabled.

```sh
# Debian / Ubuntu
sudo apt-get install cmake g++ pkg-config libarchive-dev libssl-dev \
    libconfig++-dev nlohmann-json3-dev libgtest-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/install"
```

Only this library is forced static; system dependencies may be dynamically
linked. CMake propagates the required link dependencies to consumers.

## Use from another project

Use a Git submodule pinned to a reviewed commit:

```sh
git submodule add https://github.com/astibal/libsxcftar.git external/libsxcftar
```

```cmake
add_subdirectory(external/libsxcftar)
target_link_libraries(your_target PRIVATE sxcftar::sxcftar)
```

Tests default to OFF when embedded and ON for standalone builds. Override with
`-DSXCFTAR_BUILD_TESTS=ON/OFF`.

Alternatively, use an installed package, setting `CMAKE_PREFIX_PATH` to the
installation prefix when needed:

```cmake
find_package(sxcftar CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE sxcftar::sxcftar)
```

`Smithproxy::CfTar` is also available as a compatibility target. The build-tree
underlying target remains `sxcftar`.

```cpp
#include <service/cfgapi/cfg_tar.hpp>

sx::cfg::CfgTar archive;
archive.add("example.cfg", "enabled = true;\n");
auto restored = sx::cfg::CfgTar::read(archive.write());
```

## Migrating Smithproxy / networkd

Replace the in-tree `sxcftar` target definition with `add_subdirectory` above,
and link its consumers to `sxcftar::sxcftar`. Remove the eight extracted
`cfg_*.cpp` implementations from consumer source lists and use this repository's
headers instead of duplicate in-tree copies. Do not define a second `sxcftar`
target. Keep runtime/presentation code, CLI, default templates, archive monitor,
configuration transactions and networkd-specific logic in their consumers.

The internal SHA-256 implementation now calls OpenSSL directly in a private
`sx::cftar::crypto` namespace; the consumer's existing `src/utils/crypto.cpp`
can remain without duplicate symbols. The extraction contains master code,
not unmerged networkd changes: port any networkd changes to the extracted
files here before deleting those local changes.

## Provenance and license

Source and six existing test files were extracted from the Smithproxy commit
above. The only changes to the extracted configuration implementation are the
private crypto include and namespace. SHA-256 was decoupled from Socle;
standalone CMake packaging and a digest regression test were added.
The original GPLv3 license is preserved in [LICENSE](LICENSE).
