#include <cstdio>

#include "DiskCopySerializerV5.h"
#include "Version.h"

using namespace dfitodiskcopy;

int main()
{
    int failures = 0;

    // --- Version parsing -----------------------------------------------------
    if (ParseVersion("VER_5") != Version::V5)          { std::printf("FAIL: parse VER_5\n");          ++failures; }
    if (ParseVersion("v5") != Version::V5)             { std::printf("FAIL: parse v5\n");             ++failures; }
    if (ParseVersion("Zenition 9.x.x") != Version::V5) { std::printf("FAIL: parse friendly name\n");  ++failures; }
    if (ParseVersion("nonsense") != Version::Unknown)  { std::printf("FAIL: parse nonsense\n");        ++failures; }

    const VersionInfo* v5 = GetVersionInfo(Version::V5);
    if (!v5 || v5->bitsStored != 14)                   { std::printf("FAIL: V5 info/bitsStored\n");    ++failures; }

    // --- Blob sizes must match the ABI probe (VER_5) -------------------------
    // run-info P+E+R+I = 336+464+1352+2312 = 4464 ; image = 2312 bytes.
    constexpr std::size_t kRunInfoV5 = 4464;
    constexpr std::size_t kImageV5   = 2312;

    SerializerInput in;
    in.rows           = 1920;
    in.columns        = 1920;
    in.numberOfFrames = 48;

    const auto runBlob = BuildRunInfoBlobV5(in);
    if (runBlob.size() != kRunInfoV5)
    {
        std::printf("FAIL: run-info blob = %zu, expected %zu\n", runBlob.size(), kRunInfoV5);
        ++failures;
    }

    const auto imgBlob = BuildImageInfoBlobV5(in);
    if (imgBlob.size() != kImageV5 * static_cast<std::size_t>(in.numberOfFrames))
    {
        std::printf("FAIL: image-info blob = %zu, expected %zu\n",
                    imgBlob.size(), kImageV5 * static_cast<std::size_t>(in.numberOfFrames));
        ++failures;
    }

    if (failures == 0)
        std::printf("coretest: ALL PASS (run-info=%zu, image-info=%zu for %d frames)\n",
                    runBlob.size(), imgBlob.size(), in.numberOfFrames);
    else
        std::printf("coretest: %d FAILURE(S)\n", failures);

    return failures == 0 ? 0 : 1;
}
