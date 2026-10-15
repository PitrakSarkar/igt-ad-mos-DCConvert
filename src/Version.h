#ifndef DFITODISKCOPY_VERSION_H
#define DFITODISKCOPY_VERSION_H

#include <cstdint>
#include <string>

namespace dfitodiskcopy
{
    // Mirrors DiskCopyGlobal::VERSION_ENUM (DiskCopyDataGlob.h). Values must match.
    enum class Version : int
    {
        Unknown = -1,
        V0      = 0,   // legacy DFI (pre-Zenition)
        V1      = 1,   // Zenition <= 5.3.2
        V1_1    = 2,   // Zenition >= 5.3.3
        V2      = 3,   // Zenition <= 6.1.2
        V2_1    = 4,   // Zenition >= 6.1.3
        V3      = 5,   // Zenition 7.x.x
        V4      = 6,   // Zenition 8.x.x
        V5      = 7    // Zenition 9.x.x
    };

    struct VersionInfo
    {
        Version     version;
        const char* id;             // short CLI id, e.g. "VER_5"
        const char* friendlyName;   // matches DataConverterManager::SetTargetVersion
        const char* softwareVersion;// value written to (0018,1020) Software Versions
        int         bitsStored;     // value written to (0028,0101) Bits Stored (12 legacy, else 14)
    };

    // Returns the descriptor for a version, or nullptr if not a real version.
    const VersionInfo* GetVersionInfo(Version v);

    // Parses a CLI token: accepts short id ("VER_5"), enum name ("V5"),
    // or the friendly release name. Case-insensitive. Returns Version::Unknown on failure.
    Version ParseVersion(const std::string& token);

    // Human-readable list of accepted --version values (for help/usage text).
    std::string SupportedVersionsHelp();
}

#endif
