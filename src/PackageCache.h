#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include "Git.h"
#include "PackageArguments.h"
#include "Submodules.h"

#include <string>
#include <vector>

namespace lockstep
{
enum class Network
{
    allowed,
    forbidden
};

enum class Verification
{
    trustStamp,
    rehash
};

struct PackageRequest
{
    std::string name;
    PackageKind kind = PackageKind::git;
    std::string url;
    std::string ref;
    eacp::Vector<eacp::FilePath> patches;
    SubmoduleSelection submoduleSelection;
    ArchiveHash archiveHash;
};

struct MaterializedPackage
{
    std::string commit;
    std::string hash;
    eacp::FilePath sourceDirectory;
    std::vector<LockedSubmodule> submodules;
    bool usesGitLfs = false;
};

// The shared cache: bare git mirrors under git/, downloaded archives under
// downloads/, and unpacked trees under src/<merkle hash>.
struct GitCheckout
{
    std::string resolvedCommit;
    std::vector<LockedSubmodule> submodules;
    bool usesLfs = false;
};

struct SubmoduleParent
{
    std::string packageName;
    std::string url;
    const GitTreeListing& listing;
    eacp::FilePath tree;
    std::string pathPrefix;
};

class PackageCache
{
public:
    PackageCache(eacp::FilePath rootToUse, eacp::FilePath cmakeToUse);

    static eacp::FilePath getDefaultDirectory();

    eacp::FilePath getSourceDirectory(const std::string& hash) const;

    // True when src/<hash> exists and still hashes to `hash`. With
    // trustStamp, a matching size/mtime stamp stands in for the hash.
    bool hasIntactEntry(const std::string& hash, Verification verification) const;

    void removeEntry(const std::string& hash) const;

    // Fetches `commit` (or, when it is empty, whatever `request.ref` names now),
    // unpacks it, applies the patches and stores the tree under its hash.
    MaterializedPackage materialize(const PackageRequest& request,
                                    const std::string& commit,
                                    Network network) const;

private:
    eacp::FilePath getMirrorDirectory(const std::string& url) const;
    eacp::FilePath getOrCreateMirror(const std::string& url) const;
    eacp::FilePath getDownloadPath(const std::string& url) const;
    eacp::TemporaryFile createStagingDirectory() const;

    GitCheckout fetchGitTree(const PackageRequest& request,
                             const std::string& commit,
                             Network network,
                             const eacp::FilePath& staging) const;
    eacp::FilePath fetchCommitIntoMirror(const std::string& name,
                                         const std::string& url,
                                         const std::string& commit,
                                         Network network) const;
    void checkOutSubmodules(const SubmoduleParent& parent,
                            const SubmoduleSelection& selection,
                            Network network,
                            GitCheckout& checkout) const;
    void fetchArchiveTree(const PackageRequest& request,
                          Network network,
                          const eacp::FilePath& staging) const;
    std::string computeArchiveHash(const eacp::FilePath& archive,
                                   const std::string& algorithm) const;
    void extractArchive(const eacp::FilePath& archive,
                        const eacp::FilePath& destination) const;
    MaterializedPackage storeTree(const eacp::FilePath& tree,
                                  const std::string& commit,
                                  std::vector<LockedSubmodule> submodules) const;

    eacp::FilePath root;
    eacp::FilePath cmake;
};
} // namespace lockstep
