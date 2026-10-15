#include "Version.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace dfitodiskcopy
{
    namespace
    {
        // One descriptor per real version. Friendly names match
        // DataConverterManager::SetTargetVersion in the platform source.
        const std::array<VersionInfo, 8> kVersions = {{
            { Version::V0,   "VER_0",   "DFI",                     "",      12 },
            { Version::V1,   "VER_1",   "Zenition 5.3.2 or older", "5.3.2", 14 },
            { Version::V1_1, "VER_1_1", "Zenition 5.3.3 or later", "5.3.3", 14 },
            { Version::V2,   "VER_2",   "Zenition 6.1.2 or older", "6.1.2", 14 },
            { Version::V2_1, "VER_2_1", "Zenition 6.1.3 or later", "6.1.3", 14 },
            { Version::V3,   "VER_3",   "Zenition 7.x.x",          "7.0.0", 14 },
            { Version::V4,   "VER_4",   "Zenition 8.x.x",          "8.0.0", 14 },
            { Version::V5,   "VER_5",   "Zenition 9.x.x",          "9.1.1", 14 },
        }};

        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // enum-name aliases: "V5", "V2_1", etc.
        const char* EnumName(Version v)
        {
            switch (v)
            {
            case Version::V0:   return "V0";
            case Version::V1:   return "V1";
            case Version::V1_1: return "V1_1";
            case Version::V2:   return "V2";
            case Version::V2_1: return "V2_1";
            case Version::V3:   return "V3";
            case Version::V4:   return "V4";
            case Version::V5:   return "V5";
            default:            return "";
            }
        }
    }

    const VersionInfo* GetVersionInfo(Version v)
    {
        for (const auto& info : kVersions)
        {
            if (info.version == v)
                return &info;
        }
        return nullptr;
    }

    Version ParseVersion(const std::string& token)
    {
        const std::string t = ToLower(token);
        for (const auto& info : kVersions)
        {
            if (t == ToLower(info.id) ||
                t == ToLower(EnumName(info.version)) ||
                t == ToLower(info.friendlyName))
            {
                return info.version;
            }
        }
        return Version::Unknown;
    }

    std::string SupportedVersionsHelp()
    {
        std::string out;
        for (const auto& info : kVersions)
        {
            out += "  ";
            out += info.id;
            out += "  (";
            out += info.friendlyName;
            out += ")\n";
        }
        return out;
    }
}
