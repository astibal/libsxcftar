#include <gtest/gtest.h>
#include <service/cfgapi/cfg_archive.hpp>
#include <service/cfgapi/cfg_archive_session.hpp>
#include <service/cfgapi/cfg_exposure.hpp>
#include <service/cfgapi/cfg_pack.hpp>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <unistd.h>

using namespace sx::cfg;
namespace {

static_assert(!std::is_invocable_v<decltype(&CfgManifest::canonical)>);
static_assert(!std::is_invocable_v<decltype(&CfgManifest::validate), const CfgTar&>);
static_assert(!std::is_invocable_v<decltype(&CfgArchive::open), CfgArchive::Paths>);
static_assert(!std::is_invocable_v<decltype(&CfgPack::build), const CfgPack::Input&>);

ArchiveSchema chat_schema() {
    ArchiveSchema schema;
    schema.format = "rekochat-config";
    schema.version = 7;
    schema.required_files = {"rekochat.cfg"};
    schema.optional_files = {"logging.cfg"};
    schema.allowed_directories = {"certs/", "templates/"};
    schema.main_config = "rekochat.cfg";
    return schema;
}

CfgTar chat_archive() {
    CfgTar archive;
    archive.add("manifest.json", CfgManifest::canonical(chat_schema()));
    // This is deliberately not libconfig syntax: the application owns parsing.
    archive.add("rekochat.cfg", "chat configuration");
    archive.add("certs/key.bin", std::string("\0\xff", 2));
    return archive;
}

class SchemaTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string pattern = "/tmp/cftar-schema-XXXXXX";
        const auto* created = ::mkdtemp(pattern.data());
        ASSERT_NE(created, nullptr);
        root = created;
        file = root + "/chat.cftar";
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    std::string root, file;
};

TEST(ArchiveSchema, RequiredOptionalAndRecursiveDirectories) {
    auto schema = chat_schema();
    auto archive = chat_archive();
    EXPECT_NO_THROW(CfgManifest::validate(schema, archive));
    archive.add("logging.cfg", "logging");
    archive.add("templates/nested/page.txt", "hello");
    EXPECT_NO_THROW(CfgManifest::validate(schema, archive));
    archive.add("certs-evil/key", "no");
    EXPECT_THROW(CfgManifest::validate(schema, archive), CfgTarError);
    archive.remove("certs-evil/key");
    archive.remove("rekochat.cfg");
    EXPECT_THROW(CfgManifest::validate(schema, archive), CfgTarError);
}

TEST(ArchiveSchema, RejectsWrongIdentityAndOversizedIntegerVersion) {
    auto archive = chat_archive();
    EXPECT_THROW(CfgManifest::validate(smithproxy_schema(), archive), CfgTarError);
    for (const auto* manifest : {
        R"({"format":"other","version":7})",
        R"({"format":"rekochat-config","version":8})",
        R"({"format":"rekochat-config","version":4294967303})",
        R"({"format":"rekochat-config","version":-1})",
        R"({"format":"rekochat-config","version":7,"extra":true})"}) {
        archive.replace("manifest.json", manifest);
        EXPECT_THROW(CfgManifest::validate(chat_schema(), archive), CfgTarError);
    }
}

TEST(ArchiveSchema, InvalidDefinitionsAndSafetyCannotBeOverridden) {
    EXPECT_THROW(CfgManifest::canonical(ArchiveSchema{}), CfgTarError);
    auto schema = chat_schema();
    schema.version = 0;
    EXPECT_THROW(schema.validate(), CfgTarError);
    schema = chat_schema();
    schema.main_config = "missing-role.cfg";
    EXPECT_THROW(schema.validate(), CfgTarError);
    for (const auto* path : {"../escape", "/absolute", "a//b", "a/./b", "a\\b", "manifest.json"}) {
        schema = chat_schema();
        schema.optional_files = {path};
        EXPECT_THROW(schema.validate(), CfgTarError) << path;
    }
    for (const auto* directory : {"", "/", "../", "certs", "certs//", "manifest.json/"}) {
        schema = chat_schema();
        schema.allowed_directories = {directory};
        EXPECT_THROW(schema.validate(), CfgTarError) << directory;
    }
    schema = chat_schema();
    schema.allowed_root_extensions = {".cfg/../bad"};
    EXPECT_THROW(schema.validate(), CfgTarError);
    schema = chat_schema();
    schema.directory_descriptions = {{"unknown/", "description"}};
    EXPECT_THROW(schema.validate(), CfgTarError);
    auto archive = chat_archive();
    EXPECT_THROW(archive.add("certs/../escape", "bad"), CfgTarError);
    CfgTar::Limits limits;
    limits.max_file_size = 2;
    CfgTar bounded(limits);
    EXPECT_THROW(bounded.add("rekochat.cfg", "oversize"), CfgTarError);
}

TEST(ArchiveSchema, ContentValidatorRunsAfterStructuralValidation) {
    auto schema = chat_schema();
    unsigned calls = 0;
    schema.validate_content = [&](const CfgTar& archive) {
        ++calls;
        if (archive.at("rekochat.cfg").data != "approved") throw CfgTarError("invalid chat config");
    };
    auto archive = chat_archive();
    EXPECT_THROW(CfgManifest::validate(schema, archive), CfgTarError);
    EXPECT_EQ(calls, 1u);
    archive.replace("rekochat.cfg", "approved");
    EXPECT_NO_THROW(CfgManifest::validate(schema, archive));
    archive.add("forbidden", "data");
    EXPECT_THROW(CfgManifest::validate(schema, archive), CfgTarError);
    EXPECT_EQ(calls, 2u);
}

TEST_F(SchemaTest, ArchiveOperationsUseAnOwnedSchemaAndKeepFailedAdoptionAtomic) {
    auto schema = chat_schema();
    schema.validate_content = [](const CfgTar& archive) {
        if (archive.at("rekochat.cfg").data == "invalid") throw CfgTarError("invalid content");
    };
    auto source = CfgArchive::read_archive(schema, file, chat_archive().write());
    schema.required_files.push_back("external-change");
    EXPECT_EQ(source.info().format_version, 7u);
    EXPECT_EQ(source.main_config(), "chat configuration");
    EXPECT_THROW(source.startup_config(), CfgTarError);
    EXPECT_EQ(source.resolve("certs/key.bin", "").content(), std::string("\0\xff", 2));
    EXPECT_EQ(source.resources("certs/"), std::vector<std::string>{"certs/key.bin"});
    EXPECT_THROW(source.accept_saved_main("invalid"), CfgTarError);
    EXPECT_EQ(source.main_config(), "chat configuration");
    auto saved = source.prepare_save("updated");
    source.accept_saved_archive(saved.replacement);
    EXPECT_EQ(source.main_config(), "updated");
    saved = source.prepare_resource("logging.cfg", "debug");
    source.accept_saved_archive(saved.replacement);
    saved = source.prepare_resource("logging.cfg", std::nullopt);
    source.accept_saved_archive(saved.replacement);
    EXPECT_FALSE(source.archive().contains("logging.cfg"));
    EXPECT_THROW(source.prepare_resource("outside/file", "bad"), CfgTarError);
    EXPECT_EQ(CfgArchive::inspect_archive(chat_schema(), source.archive_payload()).format_version, 7u);
    source.archive().write_file(file);
    EXPECT_EQ(CfgArchive::open(chat_schema(), {file, root + "/absent.cfg"}).main_config(), "updated");
}

TEST_F(SchemaTest, SessionSaveReloadAndExposureUseTheSelectedSchema) {
    chat_archive().write_file(file);
    auto schema = chat_schema();
    auto session = CfgArchiveSession::open(schema, file);
    schema.allowed_directories.clear();
    session.put("certs/second.bin", "second");
    session.put("logging.cfg", "debug");
    session.remove("logging.cfg");
    EXPECT_THROW(session.remove("rekochat.cfg"), CfgTarError);
    EXPECT_THROW(session.put("smithproxy.cfg", "unexpected"), CfgTarError);
    EXPECT_THROW(session.put("manifest.json", "{}"), CfgTarError);
    session.save();
    EXPECT_FALSE(session.dirty());
    auto reloaded = CfgArchiveSession::open(chat_schema(), file);
    EXPECT_EQ(reloaded.entry("certs/second.bin").data, "second");
    EXPECT_TRUE(std::filesystem::exists(file + ".previous.bak.cftar"));
    CfgExposureRegistry exposures;
    const auto exposed = exposures.expose(chat_schema(), reloaded.archive(), "certs");
    EXPECT_TRUE(std::filesystem::exists(exposed.filesystem_path + "/second.bin"));
    EXPECT_THROW(CfgRuntimeTree::create(smithproxy_schema(), reloaded.archive()), CfgTarError);
}

TEST_F(SchemaTest, SessionValidatorCannotContaminateWorkingCopy) {
    chat_archive().write_file(file);
    auto schema = chat_schema();
    schema.validate_session = [](const CfgTar& archive) {
        if (archive.at("rekochat.cfg").data == "invalid") throw CfgTarError("invalid edit");
    };
    auto session = CfgArchiveSession::open(schema, file);
    EXPECT_THROW(session.put("rekochat.cfg", "invalid"), CfgTarError);
    EXPECT_FALSE(session.dirty());
    EXPECT_EQ(session.entry("rekochat.cfg").data, "chat configuration");
}

TEST_F(SchemaTest, PackMigrateAndRoleFreeArchivesAreGeneric) {
    std::ofstream(root + "/input") << "chat configuration";
    std::filesystem::create_directory(root + "/certs");
    std::ofstream(root + "/certs/key") << "key";
    CfgPack::Input input;
    input.files.push_back({"rekochat.cfg", root + "/input"});
    input.directories.push_back({"certs", root + "/certs"});
    auto schema = chat_schema();
    schema.main_config.clear(); // No implicit main/startup filenames needed.
    auto archive = CfgPack::build(schema, input);
    EXPECT_FALSE(archive.contains("smithproxy.cfg"));
    EXPECT_FALSE(archive.contains("certs/resource.json"));
    EXPECT_NO_THROW(CfgManifest::validate(schema, archive));
    EXPECT_TRUE(CfgPack::migrate(schema, {input, file}));
    EXPECT_FALSE(CfgPack::migrate(schema, {input, file}));
    EXPECT_THROW(CfgPack::migrate(smithproxy_schema(), {input, file}), CfgTarError);
    EXPECT_THROW(CfgPack::retire_smithproxy_legacy_tree(schema, root, "chat.cftar", "backup.tar"), CfgTarError);
    EXPECT_TRUE(std::filesystem::exists(root + "/input"));
    input.main_config = root + "/input";
    EXPECT_THROW(CfgPack::build(schema, input), CfgTarError);
    input.files.clear();
    schema.main_config = "rekochat.cfg";
    schema.directory_descriptions = {{"certs/", "quoted \"description\"\nline"}};
    archive = CfgPack::build(schema, input);
    EXPECT_TRUE(archive.contains("certs/resource.json"));
    EXPECT_EQ(archive.at("rekochat.cfg").data, "chat configuration");
}

} // namespace
