#pragma once
#include <QString>

// Portable fallback for systems without a desktop secret service. The file is
// owner-only and updates are committed atomically; errors are never discarded.
namespace CredentialStore
{
QString read(const QString& path, const QString& key);
bool write(const QString& path, const QString& key, const QString& value);
}
