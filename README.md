# libsxcftar

C++17 static library for configuration TAR archives: bounded parsing and writing,
application-defined schemas, validated archive sessions, file locking, atomic
packing and temporary runtime trees. No dependency on Socle or the proxy runtime.

**Version 0.2 requires an explicit `ArchiveSchema` for every high-level archive
operation.** There is no implicit Smithproxy default or global schema override.
See [MIGRATION.md](MIGRATION.md) for consumer migration instructions.

## Build

Linux, CMake >= 3.16 and a C++17 compiler are required. Dependencies: libarchive,
OpenSSL Crypto, nlohmann_json, libconfig++ (the explicit Smithproxy session
validator), and pthreads. GoogleTest is needed only for tests.

```sh
sudo apt-get install cmake g++ pkg-config libarchive-dev libssl-dev \
    libconfig++-dev nlohmann-json3-dev libgtest-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/install"
```

The output is `libsxcftar.a`; dependencies may remain dynamically linked.
`SXCFTAR_BUILD_TESTS` defaults to ON standalone and OFF when embedded.

## Integrate

```sh
git submodule add https://github.com/astibal/libsxcftar.git external/libsxcftar
```

```cmake
add_subdirectory(external/libsxcftar)
target_link_libraries(your_target PRIVATE sxcftar::sxcftar)
```

Alternatively, use `find_package(sxcftar 0.2 CONFIG REQUIRED)` with
`CMAKE_PREFIX_PATH` pointing to the installation. `Smithproxy::CfTar` is a
compatibility target. Existing `sx::cfg` names and `service/cfgapi/*.hpp` header
paths are retained, but high-level calls now require the schema as first argument.

## Define an application schema

```cpp
#include <service/cfgapi/cfg_archive.hpp>

sx::cfg::ArchiveSchema schema;
schema.format = "rekochat-config";
schema.version = 1;
schema.required_files = {"rekochat.cfg"};
schema.optional_files = {"logging.cfg"};
schema.allowed_directories = {"certs/", "templates/"};
schema.main_config = "rekochat.cfg"; // optional convenience role

sx::cfg::CfgTar tar;
tar.add("manifest.json", sx::cfg::CfgManifest::canonical(schema));
tar.add("rekochat.cfg", "enabled = true;\n");
sx::cfg::CfgManifest::validate(schema, tar);
auto source = sx::cfg::CfgArchive::read_archive(schema, "chat.cftar", tar.write());
```

The manifest has exactly `format` and `version` fields. It identifies the
application format; it does not supply trusted validation rules. Required files
must exist; optional files may exist; directory prefixes permit recursive content.
Unknown paths are rejected. Prefixes end with `/` and do not match sibling names.
`manifest.json` is library-owned and must not be listed in the schema's file lists.

Paths, links, special files and TAR size limits remain protected independently
of the schema. Low-level `CfgTar` operations and `CfgPack::read_directory` remain
schema-free building blocks; they do not claim application-level validation.

Optional schema features:

- `main_config` and `startup_config`: map convenience APIs and pack shortcuts to
  required files. Empty means no such role; role-dependent accessors fail clearly.
- `allowed_root_extensions`: explicitly permit root files such as `.cfg`; these
  are treated as control files rather than removable resources.
- `validate_content`: application callback, run after namespace validation on all
  high-level validation paths. Throw `CfgTarError` to reject content.
- `validate_session`: additional checks for offline editing on open, edits and
  save. Smithproxy uses this for libconfig syntax, matching its previous behavior.
- `directory_descriptions`: optional directory-to-description map. Packing emits
  `resource.json` only for populated directories and preserves existing content.

Archives and sessions own a copy of their schema; `schema()` returns a const
reference. Callbacks must be deterministic and own their captured state. Use
separate schemas for unrelated applications. No generic schema implicitly
requires UTF-8 or libconfig; supply the appropriate content validator.

`smithproxy_schema()` explicitly preserves the previous manifest, required
files, root `.cfg` UTF-8 checks, resource namespaces and session parsing. Existing
Smithproxy archives retain their on-disk format and need no conversion.

## Provenance and license

Originally extracted from Smithproxy master
`ae0415f69624618d7c611ff7a336280352a50f5b`. Version 0.2 adds mandatory explicit
schemas and regression tests for unrelated application archives. The original
GPLv3 [LICENSE](LICENSE) is preserved.
