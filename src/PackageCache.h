#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

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

struct PackageRequest
{
    std::string name;
    std::string kind;
    std::string url;
    std::string ref;
    eacp::Vector<eacp::FilePath> patches;
    SubmoduleSelection submoduleSelection;
    std::string archiveSha256;
};

struct MaterializedPackage
{
    std::string commit;
    std::string hash;
    eacp::FilePath sourceDirectory;
    std::vector<LockedSubmodule> submodules;
};

// The shared cache: bare git mirrors under git/, downloaded archives under
// downloads/, and unpacked trees under src/<merkle hash>.
class PackageCache
{
public:
    PackageCache(eacp::FilePath rootToUse, eacp::FilePath cmakeToUse);

    static eacp::FilePath getDefaultDirectory();

    eacp::FilePath getSourceDirectory(const std::string& hash) const;

    // True when src/<hash> exists and still hashes to `hash`. Without
    // `alwaysRecompute`, a matching size/mtime stamp stands in for the hash.
    bool hasIntactEntry(const std::string& hash, bool alwaysRecompute) const;

    void removeEntry(const std::string& hash) const;

    // Fetches `commit` (or, when it is empty, whatever `request.ref` names now),
    // unpacks it, applies the patches and stores the tree under its hash.
    MaterializedPackage materialize(const PackageRequest& request,
                                    const std::string& commit,
                                    Network network) const;

private:
    eacp::FilePath getMirrorDirectory(const std::string& url) const;
    eacp::FilePath getDownloadPath(const std::string& url) const;
    eacp::FilePath createStagingDirectory() const;

    std::string fetchGitTree(const PackageRequest& request,
                             const std::string& commit,
                             Network network,
                             const eacp::FilePath& staging,
                             std::vector<LockedSubmodule>& submodules) const;
    eacp::FilePath fetchCommitIntoMirror(const std::string& name,
                                         const std::string& url,
                                         const std::string& commit,
                                         Network network) const;
    void checkOutSubmodules(const std::string& name,
                            const std::string& parentUrl,
                            const eacp::FilePath& parentMirror,
                            const std::string& parentCommit,
                            const eacp::FilePath& tree,
                            const std::string& pathPrefix,
                            const SubmoduleSelection& selection,
                            Network network,
                            std::vector<LockedSubmodule>& submodules) const;
    void fetchArchiveTree(const PackageRequest& request,
                          Network network,
                          const eacp::FilePath& staging) const;
    void extractArchive(const eacp::FilePath& archive,
                        const eacp::FilePath& destination) const;
    MaterializedPackage storeTree(const eacp::FilePath& tree,
                                  const std::string& commit,
                                  std::vector<LockedSubmodule> submodules) const;

    eacp::FilePath root;
    eacp::FilePath cmake;
};
} // namespace lockstep
