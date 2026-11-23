# Migrating consumers to mandatory schemas (0.2)

This is an intentional source API break from 0.1. The library provides no
no-schema overloads and never infers application policy from an uploaded archive.
The existing Smithproxy on-disk format remains `smithproxy-cftar`, version 1.

## Smithproxy, NetworkD and smithproxy-cftar

1. Update/pin the `libsxcftar` dependency to the 0.2 schema API commit.
   Link `sxcftar::sxcftar`; do not compile duplicate extracted sources or define
   another target named `sxcftar`.
2. Establish one explicit application profile at the composition boundary:

   ```cpp
   const auto schema = sx::cfg::smithproxy_schema();
   ```

   NetworkD reading the shared Smithproxy archive must use this same profile,
   not a new format identity. Root-level application `.cfg` files remain allowed.
3. Pass it as the **first argument** to all calls listed below. Include
   `service/cfgapi/cfg_schema.hpp` where only the schema is needed.
4. Keep passing the same schema through CLI create/validate/list/edit/save,
   initial load, privileged reload, import/restore, migration, resource edits,
   runtime trees and exposures. Archives and sessions retain a copy; their
   instance operations use it automatically. Their `schema()` accessor can
   supply the profile for a subsequent exposure or runtime tree.
5. Port any unmerged changes to extracted implementations into this repository
   deliberately. Do not discard local NetworkD fixes while removing duplicates.
6. Update tests and fixtures: runtime trees and exposures now validate the
   manifest and schema before touching disk. Fixtures need a valid manifest
   and required files, or their own explicit test schema.

## Changed entry points

```cpp
CfgManifest::canonical(schema);
CfgManifest::validate(schema, tar);
CfgManifest::is_resource_path(schema, logical_path);

CfgArchive::open(schema, paths);
CfgArchive::read_archive(schema, path, bytes);
CfgArchive::inspect_archive(schema, bytes);

CfgArchiveSession::open(schema, archive_path);
CfgArchiveSession::open(schema, archive_path, io);

CfgPack::build(schema, input);
CfgPack::migrate(schema, migration);
CfgPack::retire_smithproxy_legacy_tree(schema, root, active_archive, backup_name);

CfgRuntimeTree::create(schema, tar);
CfgRuntimeTree::create(schema, tar, io);
exposures.expose(schema, tar, logical_path);
```

`CfgPack::retire_legacy_tree` was renamed to
`retire_smithproxy_legacy_tree`: its filesystem ownership rules are explicitly
Smithproxy-only. Other applications must not use that helper for cleanup.
`CfgPack::retire_legacy_config`, path sibling helpers, and low-level TAR/directory
operations keep their signatures because they do not interpret a manifest.

`CfgManifest::format` and `CfgManifest::version` were removed: use
`schema.format` and `schema.version`. `CfgManifest::path` remains `manifest.json`.
`CfgManifest::max_config_size` remains the Smithproxy profile's text limit.

Instance methods such as `prepare_save`, `prepare_resource`, `accept_saved_main`,
`accept_saved_archive`, `put`, `remove`, `validate` and `save` keep their argument
lists and use the schema captured at open/read time. Failed validation leaves
cached/working content unchanged.

## RekoChat or another application

Define a schema in application code as shown in README. Choose a distinct format
identifier, required/optional files and recursive directory prefixes. Use that
schema consistently for all high-level calls. Do not change the Smithproxy
profile globally or inherit its libconfig parser unintentionally.

A main or startup role is optional. Without roles, pack everything through
`CfgPack::Input::files` and `directories`; required files are checked afterward:

```cpp
sx::cfg::CfgPack::Input input;
input.files.push_back({"rekochat.cfg", "/etc/rekochat/rekochat.cfg"});
input.directories.push_back({"certs", "/etc/rekochat/certs"});
auto tar = sx::cfg::CfgPack::build(schema, input);
```

Schema directory rules use `certs/` (trailing slash); pack directory destination
prefixes use `certs` (no trailing slash), matching the existing pack API.
`input.main_config` and `input.startup_config` remain optional physical-file
shortcuts only when corresponding schema roles have been set.

The library validates the structural schema and invokes supplied content hooks.
It does not automatically parse configuration payloads for unrelated formats.

## Verification in each consuming task

Build all consumers and tests after updating calls. Exercise creation, loading,
reload, resource editing, session save/reopen, import/restore, migration and
runtime/exposure flows used by that project. Confirm that missing required files,
unknown paths, wrong format/version and unsafe TAR entries are rejected.
