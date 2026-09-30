#include "vfs/vfscatalog.h"
#include "vfs/vfsindex.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using SteadyClock = std::chrono::steady_clock;

class TemporaryDirectory
{
public:
  explicit TemporaryDirectory(const fs::path& parent)
  {
    std::error_code error;
    fs::create_directories(parent, error);
    if (error) {
      throw std::runtime_error("Unable to create scratch root: " +
                               error.message());
    }

    std::string pattern =
        (fs::absolute(parent) / "fluorine-vfs-cache-benchmark-XXXXXX")
            .string();
    pattern.push_back('\0');
    char* created = ::mkdtemp(pattern.data());
    if (created == nullptr) {
      throw std::runtime_error("mkdtemp failed under scratch root");
    }
    m_path = created;
  }

  ~TemporaryDirectory()
  {
    std::error_code ignored;
    fs::remove_all(m_path, ignored);
  }

  const fs::path& path() const { return m_path; }

private:
  fs::path m_path;
};

struct Options
{
  std::size_t mod_files = 128;
  std::size_t bytes_per_file = 64 * 1024;
  fs::path scratch_root = fs::current_path();
};

std::size_t parseSize(const std::string& value, const std::string& option)
{
  std::size_t consumed = 0;
  const unsigned long long parsed = std::stoull(value, &consumed);
  if (consumed != value.size() || parsed == 0 ||
      parsed > static_cast<unsigned long long>(SIZE_MAX)) {
    throw std::runtime_error("Invalid value for " + option + ": " + value);
  }
  return static_cast<std::size_t>(parsed);
}

Options parseOptions(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h") {
      std::cout << "Usage: benchmark_vfs_cache_reuse [--files N] "
                   "[--bytes-per-file N] [--scratch-root PATH]\n"
                   "Creates and removes an isolated synthetic fixture under "
                   "PATH. No game, user mod, or active VFS cache is read.\n";
      std::exit(0);
    }
    if (index + 1 >= argc) {
      throw std::runtime_error("Missing value after " + argument);
    }
    const std::string value = argv[++index];
    if (argument == "--files") {
      options.mod_files = parseSize(value, argument);
    } else if (argument == "--bytes-per-file") {
      options.bytes_per_file = parseSize(value, argument);
    } else if (argument == "--scratch-root") {
      options.scratch_root = value;
    } else {
      throw std::runtime_error("Unknown option: " + argument);
    }
  }
  if (options.bytes_per_file > static_cast<std::size_t>(1) << 30) {
    throw std::runtime_error("--bytes-per-file is limited to 1 GiB");
  }
  return options;
}

void setXdgDataHome(const fs::path& path)
{
  const QByteArray value = QByteArray::fromStdString(fs::absolute(path).string());
  if (!qputenv("XDG_DATA_HOME", value)) {
    throw std::runtime_error("Unable to set isolated XDG_DATA_HOME");
  }
}

bool pathIsWithin(const fs::path& child, const fs::path& parent)
{
  const fs::path relative = fs::absolute(child).lexically_normal().lexically_relative(
      fs::absolute(parent).lexically_normal());
  if (relative.empty()) return fs::absolute(child) == fs::absolute(parent);
  return *relative.begin() != ".." && !relative.is_absolute();
}

void requireIsolatedDatabase(const fs::path& database,
                             const fs::path& scratchRoot)
{
  if (!pathIsWithin(database, scratchRoot)) {
    throw std::runtime_error(
        "Production VFS cache path escaped the benchmark scratch root; "
        "refusing to write: " + database.string());
  }
}

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

void writePayload(const fs::path& path, std::size_t size, unsigned char seed)
{
  std::error_code error;
  fs::create_directories(path.parent_path(), error);
  if (error) {
    throw std::runtime_error("Unable to create fixture directory: " +
                             error.message());
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("Unable to create fixture file: " +
                             path.string());
  }

  constexpr std::size_t blockSize = 64 * 1024;
  std::vector<char> block(std::min(blockSize, std::max<std::size_t>(size, 1)));
  for (std::size_t index = 0; index < block.size(); ++index) {
    block[index] = static_cast<char>(
        (index * 131u + static_cast<std::size_t>(seed) * 17u) & 0xffu);
  }
  std::size_t written = 0;
  while (written < size) {
    const std::size_t amount = std::min(block.size(), size - written);
    output.write(block.data(), static_cast<std::streamsize>(amount));
    if (!output) {
      throw std::runtime_error("Unable to write fixture file: " +
                               path.string());
    }
    written += amount;
  }
}

void replaceByCopy(const fs::path& source, const fs::path& destination)
{
  fs::path temporary = destination;
  temporary += ".benchmark-replacement";
  std::error_code ignored;
  fs::remove(temporary, ignored);
  fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
  fs::permissions(temporary, fs::status(source).permissions(),
                  fs::perm_options::replace);
  fs::last_write_time(temporary, fs::last_write_time(source));
  fs::rename(temporary, destination);
}

struct FileIdentity
{
  dev_t device = 0;
  ino_t inode = 0;
  off_t size = 0;
  mode_t mode = 0;
};

FileIdentity fileIdentity(const fs::path& path)
{
  struct stat metadata {};
  if (::lstat(path.c_str(), &metadata) != 0) {
    throw std::runtime_error("Unable to stat fixture file: " + path.string());
  }
  return {metadata.st_dev, metadata.st_ino, metadata.st_size,
          static_cast<mode_t>(metadata.st_mode & 07777)};
}

bool contentsEqual(const fs::path& left, const fs::path& right)
{
  if (fs::file_size(left) != fs::file_size(right)) return false;
  std::ifstream leftInput(left, std::ios::binary);
  std::ifstream rightInput(right, std::ios::binary);
  if (!leftInput || !rightInput) return false;

  constexpr std::size_t blockSize = 64 * 1024;
  std::array<char, blockSize> leftBlock{};
  std::array<char, blockSize> rightBlock{};
  for (;;) {
    leftInput.read(leftBlock.data(), leftBlock.size());
    rightInput.read(rightBlock.data(), rightBlock.size());
    if (leftInput.gcount() != rightInput.gcount()) return false;
    if (!std::equal(leftBlock.begin(), leftBlock.begin() + leftInput.gcount(),
                    rightBlock.begin())) {
      return false;
    }
    if (leftInput.eof() || rightInput.eof()) {
      return leftInput.eof() && rightInput.eof();
    }
    if (!leftInput || !rightInput) return false;
  }
}

std::string digestHex(const VfsDigest& digest)
{
  constexpr char hex[] = "0123456789abcdef";
  std::string text(digest.size() * 2, '0');
  for (std::size_t index = 0; index < digest.size(); ++index) {
    text[index * 2] = hex[digest[index] >> 4];
    text[index * 2 + 1] = hex[digest[index] & 0x0f];
  }
  return text;
}

double elapsedMilliseconds(SteadyClock::time_point start)
{
  return std::chrono::duration<double, std::milli>(SteadyClock::now() - start)
      .count();
}

VfsDigest stableProfileDigest()
{
  VfsDigest digest{};
  for (std::size_t index = 0; index < digest.size(); ++index) {
    digest[index] = static_cast<unsigned char>(0x50u + index);
  }
  return digest;
}

void printProviderSummaries(const std::vector<VfsCatalogProviderSummary>& rows)
{
  if (rows.empty()) {
    std::cout << "provider_summaries=none";
    return;
  }
  std::cout << "provider_summaries=";
  bool first = true;
  for (const auto& row : rows) {
    if (!first) std::cout << '|';
    first = false;
    std::cout << row.origin << "{scanned=" << row.files_scanned
              << ",hashed=" << row.files_hashed
              << ",bytes=" << row.bytes_hashed
              << ",misses=" << row.fingerprint_misses
              << ",uncached=" << row.fingerprint_uncached
              << ",dev=" << row.fingerprint_device_mismatches
              << ",ino=" << row.fingerprint_inode_mismatches
              << ",size=" << row.fingerprint_size_mismatches
              << ",mode=" << row.fingerprint_mode_mismatches
              << ",mtime=" << row.fingerprint_mtime_mismatches
              << ",ctime=" << row.fingerprint_ctime_mismatches
              << ",no_digest=" << row.fingerprint_missing_digests
              << ",rows_loaded=" << row.catalog_rows_loaded
              << ",rows_written=" << row.catalog_rows_written
              << ",rows_deleted=" << row.catalog_rows_deleted << '}';
  }
}

std::string runMeasurement(
    const std::string& scenario, VfsCatalog& catalog,
    const std::string& dataDirectory,
    const std::vector<std::pair<std::string, std::string>>& mods,
    const std::string& overwriteDirectory, const VfsDigest& profileDigest,
    const VfsIndexPublicationContext& publicationContext,
    const fs::path& catalogDatabase, const std::string& previousGeneration,
    const std::string& extra = {})
{
  VfsCatalogProgress finalProgress;
  const auto catalogStart = SteadyClock::now();
  VfsCatalogResult result = catalog.reconcileAndBuild(
      dataDirectory, mods, overwriteDirectory, true,
      [&](const VfsCatalogProgress& progress) { finalProgress = progress; });
  const double catalogMilliseconds = elapsedMilliseconds(catalogStart);

  VfsIndexPublisher publisher;
  const auto indexStart = SteadyClock::now();
  const VfsIndexPublicationResult publication = publisher.publish(
      result.tree, result.provider_roots, profileDigest, dataDirectory,
      publicationContext, result.archive_member_index);
  const double indexMilliseconds = elapsedMilliseconds(indexStart);
  require(publication.success,
          "Index publication failed for " + scenario + ": " +
              publication.error);

  const bool generationChanged = !previousGeneration.empty() &&
      publication.generation != previousGeneration;
  std::cout << std::fixed << std::setprecision(3)
            << "scenario=" << scenario
            << ";catalog_db=" << catalogDatabase.filename().string()
            << ";files_scanned=" << finalProgress.files_scanned
            << ";files_hashed=" << finalProgress.files_hashed
            << ";bytes_hashed=" << finalProgress.bytes_hashed
            << ";fingerprint_misses=" << finalProgress.fingerprint_misses
            << ";uncached=" << finalProgress.fingerprint_uncached
            << ";device_mismatch="
            << finalProgress.fingerprint_device_mismatches
            << ";inode_mismatch="
            << finalProgress.fingerprint_inode_mismatches
            << ";size_mismatch="
            << finalProgress.fingerprint_size_mismatches
            << ";mode_mismatch="
            << finalProgress.fingerprint_mode_mismatches
            << ";mtime_mismatch="
            << finalProgress.fingerprint_mtime_mismatches
            << ";ctime_mismatch="
            << finalProgress.fingerprint_ctime_mismatches
            << ";missing_digest="
            << finalProgress.fingerprint_missing_digests
            << ";provider_roots_changed="
            << finalProgress.provider_roots_changed
            << ";catalog_rows_loaded=" << finalProgress.catalog_rows_loaded
            << ";catalog_rows_written=" << finalProgress.catalog_rows_written
            << ";catalog_rows_deleted=" << finalProgress.catalog_rows_deleted
            << ";merkle_roots_reused=" << finalProgress.merkle_roots_reused
            << ";hash_workers=" << finalProgress.hash_workers
            << ";catalog_elapsed_ms=" << catalogMilliseconds
            << ";provider_reconcile_ms=" << finalProgress.provider_reconcile_ms
            << ";archive_reconcile_ms=" << finalProgress.archive_reconcile_ms
            << ";duplicate_scan_ms=" << finalProgress.duplicate_scan_ms
            << ";catalog_commit_ms=" << finalProgress.commit_ms
            << ";index_elapsed_ms=" << indexMilliseconds
            << ";index_files=" << publication.file_count
            << ";index_reused=" << (publication.reused_existing ? "true" : "false")
            << ";index_reason="
            << vfsIndexReuseReasonCode(publication.reuse_reason)
            << ";index_generation_changed="
            << (generationChanged ? "true" : "false")
            << ";profile_root=" << digestHex(result.profile_root) << ';';
  printProviderSummaries(result.provider_summaries);
  if (!extra.empty()) std::cout << ';' << extra;
  std::cout << '\n';
  return publication.generation;
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    const Options options = parseOptions(argc, argv);
    TemporaryDirectory temporary(options.scratch_root);
    const fs::path xdgDataOne = temporary.path() / "xdg-data-one";
    setXdgDataHome(xdgDataOne);
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("fluorine-vfs-cache-benchmark"));

    const std::size_t baseFiles = std::max<std::size_t>(4, options.mod_files / 16);
    const std::size_t expectedFiles = options.mod_files + baseFiles + 1;
    const fs::path data = temporary.path() / "game" / "Data";
    const fs::path mod = temporary.path() / "mods" / "SyntheticModlist";
    const fs::path overwrite = temporary.path() / "overwrite";
    const fs::path replacementSource =
        temporary.path() / "root-builder-source" / "managed.bin";
    const fs::path backup = temporary.path() / "root-builder-backup.bin";
    const fs::path managedDestination = data / "RootBuilder" / "managed.bin";
    const fs::path outputBase = temporary.path() / "instance";
    fs::create_directories(overwrite);
    fs::create_directories(outputBase);

    for (std::size_t index = 0; index < baseFiles; ++index) {
      writePayload(data / "Base" / ("base-" + std::to_string(index) + ".bin"),
                   options.bytes_per_file,
                   static_cast<unsigned char>(10 + index));
    }
    for (std::size_t index = 0; index < options.mod_files; ++index) {
      writePayload(mod / "Data" / "Meshes" /
                       ("asset-" + std::to_string(index) + ".bin"),
                   options.bytes_per_file,
                   static_cast<unsigned char>(80 + index % 120));
    }
    writePayload(managedDestination, options.bytes_per_file, 31);
    writePayload(replacementSource, options.bytes_per_file, 203);
    fs::copy_file(managedDestination, backup);

    const fs::path catalogDatabase =
        VfsCatalog::databasePath(data.string());
    requireIsolatedDatabase(catalogDatabase, temporary.path());
    const std::vector<std::pair<std::string, std::string>> mods = {
        {"SyntheticModlist", mod.string()}};
    const VfsDigest profileDigest = stableProfileDigest();
    VfsIndexPublicationContext context{
        .output_base=outputBase,
        .producer="Fluorine benchmark 0.3.4",
        .instance_name="Synthetic cache benchmark",
        .profile_name="Default",
        .consumer_path_style=VfsIndexConsumerPathStyle::Wine};

    std::cout << "fixture_files=" << expectedFiles
              << ";mod_files=" << options.mod_files
              << ";base_files_including_rootbuilder=" << baseFiles + 1
              << ";bytes_per_file=" << options.bytes_per_file
              << ";fixture_payload_bytes="
              << expectedFiles * options.bytes_per_file
              << ";scratch_root=" << temporary.path().parent_path().string()
              << ";fixture_root=isolated-temporary-directory"
              << ";catalog_path_source=VfsCatalog::databasePath"
              << ";cold_means=empty-synthetic-sqlite-catalog-and-index"
              << ";process_restarted=false"
              << ";version_change=simulated-producer-metadata-change"
              << '\n';

    VfsCatalog catalog(catalogDatabase);
    std::string generation = runMeasurement(
        "cold", catalog, data.string(), mods, overwrite.string(), profileDigest,
        context, catalogDatabase, {});
    require(fs::exists(catalogDatabase), "Cold pass did not create catalog DB");

    const auto warmIdentity = catalogDatabase;
    const std::string previousWarmGeneration = generation;
    generation = runMeasurement(
        "warm_same_object", catalog, data.string(), mods, overwrite.string(),
        profileDigest, context, catalogDatabase, previousWarmGeneration);

    context.producer = "Fluorine benchmark 0.3.5";
    VfsCatalog reopenedCatalog(catalogDatabase);
    const std::string previousVersionGeneration = generation;
    generation = runMeasurement(
        "reopened_catalog_simulated_version_change", reopenedCatalog,
        data.string(), mods, overwrite.string(), profileDigest, context,
        catalogDatabase, previousVersionGeneration,
        "same_database_path=true;new_catalog_object=true;new_os_process=false");
    require(catalogDatabase == warmIdentity,
            "Version-change pass did not use the same catalog path");

    const fs::path changedModFile =
        mod / "Data" / "Meshes" / "asset-0.bin";
    const fs::path changedSource = temporary.path() / "changed-asset.bin";
    writePayload(changedSource, options.bytes_per_file, 7);
    replaceByCopy(changedSource, changedModFile);
    generation = runMeasurement(
        "one_changed_file", reopenedCatalog, data.string(), mods,
        overwrite.string(), profileDigest, context, catalogDatabase,
        generation);

    const FileIdentity beforeDeploy = fileIdentity(managedDestination);
    replaceByCopy(replacementSource, managedDestination);
    const FileIdentity afterDeploy = fileIdentity(managedDestination);
    require(beforeDeploy.inode != afterDeploy.inode,
            "Synthetic Root Builder deployment did not replace the inode");
    generation = runMeasurement(
        "rootbuilder_like_deploy", reopenedCatalog, data.string(), mods,
        overwrite.string(), profileDigest, context, catalogDatabase,
        generation, "destination_inode_replaced=true;bytes_match_original=false");

    // Root Builder restores the old destination at shutdown. On the next
    // deployment it copies the same payload back, producing identical bytes
    // with a replacement inode. Do both operations without reconciling between
    // them so the next catalog run compares against the prior deployment.
    const FileIdentity previousDeployment = fileIdentity(managedDestination);
    replaceByCopy(backup, managedDestination);
    const bool restoredBytesMatch = contentsEqual(managedDestination, backup);
    require(restoredBytesMatch,
            "Synthetic Root Builder restore changed the original bytes");
    const FileIdentity restoredIdentity = fileIdentity(managedDestination);
    replaceByCopy(replacementSource, managedDestination);
    const FileIdentity redeployedIdentity = fileIdentity(managedDestination);
    const bool redeployedBytesMatch =
        contentsEqual(managedDestination, replacementSource);
    require(restoredIdentity.inode != redeployedIdentity.inode &&
                previousDeployment.inode != redeployedIdentity.inode,
            "Synthetic Root Builder restore/redeploy did not replace the inode");
    require(redeployedBytesMatch,
            "Synthetic Root Builder redeployment changed the payload bytes");
    generation = runMeasurement(
        "rootbuilder_like_redeploy_same_bytes", reopenedCatalog, data.string(), mods,
        overwrite.string(), profileDigest, context, catalogDatabase,
        generation,
        "destination_inode_replaced=true;bytes_match_previous_deploy=true;"
        "scan_between_restore_and_redeploy=false");

    const FileIdentity beforeFinalRestore = fileIdentity(managedDestination);
    replaceByCopy(backup, managedDestination);
    const FileIdentity afterFinalRestore = fileIdentity(managedDestination);
    require(beforeFinalRestore.inode != afterFinalRestore.inode,
            "Synthetic Root Builder final restore did not replace the inode");
    require(contentsEqual(managedDestination, backup),
            "Synthetic Root Builder final restore changed the original bytes");
    generation = runMeasurement(
        "rootbuilder_like_restore", reopenedCatalog, data.string(), mods,
        overwrite.string(), profileDigest, context, catalogDatabase,
        generation, "destination_inode_replaced=true;bytes_match_original=true");

    const fs::path relocatedData = temporary.path() / "relocated-game" / "Data";
    fs::create_directories(relocatedData.parent_path());
    fs::rename(data, relocatedData);
    const fs::path relocatedDatabase =
        VfsCatalog::databasePath(relocatedData.string());
    requireIsolatedDatabase(relocatedDatabase, temporary.path());
    require(relocatedDatabase != catalogDatabase,
            "Data-directory relocation did not select a new catalog path");
    VfsCatalog relocatedCatalog(relocatedDatabase);
    generation = runMeasurement(
        "data_root_relocation", relocatedCatalog, relocatedData.string(), mods,
        overwrite.string(), profileDigest, context, relocatedDatabase, {},
        "new_data_root_identity=true;new_catalog_database=true");

    const fs::path xdgDataTwo = temporary.path() / "xdg-data-two";
    setXdgDataHome(xdgDataTwo);
    const fs::path xdgRelocatedDatabase =
        VfsCatalog::databasePath(relocatedData.string());
    requireIsolatedDatabase(xdgRelocatedDatabase, temporary.path());
    require(xdgRelocatedDatabase != relocatedDatabase,
            "XDG_DATA_HOME relocation did not select a new catalog path");
    require(pathIsWithin(xdgRelocatedDatabase, xdgDataTwo),
            "VFS catalog path did not follow the isolated XDG_DATA_HOME");
    VfsCatalog xdgRelocatedCatalog(xdgRelocatedDatabase);
    generation = runMeasurement(
        "xdg_data_home_relocation", xdgRelocatedCatalog,
        relocatedData.string(), mods, overwrite.string(), profileDigest,
        context, xdgRelocatedDatabase, {},
        "same_data_root=true;new_xdg_data_home=true;new_catalog_database=true");
    (void)generation;

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "benchmark_vfs_cache_reuse: " << error.what() << '\n';
    return 1;
  }
}
