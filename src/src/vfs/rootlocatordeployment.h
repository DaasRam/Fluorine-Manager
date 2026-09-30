#ifndef VFS_ROOTLOCATORDEPLOYMENT_H
#define VFS_ROOTLOCATORDEPLOYMENT_H

#include "vfsindex.h"

#include <filesystem>

namespace VfsRootLocatorDeployment {
// Publish a temporary locator beside the game executable and record enough
// state to restore a pre-existing regular file during clear(). Symlinks and
// special files at the target are deliberately left untouched.
bool deploy(const std::filesystem::path &outputBase,
            const std::filesystem::path &gameDirectory,
            VfsIndexPublicationResult &publication);

// Restore the prior regular file, if any. Returns false when recovery data is
// malformed or the target has been replaced by a symlink/special file; in
// either case no game-root target is removed.
bool clear(const std::filesystem::path &outputBase,
           const std::filesystem::path &gameDirectory);
} // namespace VfsRootLocatorDeployment

#endif
