#include <cstdio>
#include <cstring>

#include "DiskCopySerializerV5.h"
#include "Version.h"
#include "DiskCopyData_V3.h"
#include "DiskCopyData_V5.h"

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

    in.rows = in.columns = 1000;
    in.numberOfFrames = 10;
    const auto runV3 = BuildRunInfoBlobV3(in);
    const auto imagesV3 = BuildImageInfoBlobV3(in);
    if (runV3.size() != 4252 || imagesV3.size() != 22840)
    {
        std::printf("FAIL: VER_3 record sizes\n");
        ++failures;
    }
    for (int i = 0; i < in.numberOfFrames; ++i)
    {
        DiskCopyDataVer03::IMAGE_RECORD_STRUCT_V3 image;
        std::memcpy(&image, imagesV3.data() + i * sizeof(image), sizeof(image));
        if (image.imageNr != i + 1 || static_cast<int>(image.iiFormat) != 8 || image.iiRadius != 301)
        {
            std::printf("FAIL: native image record %d\n", i);
            ++failures;
        }
    }

    DiskCopyDataVer03::RUN_RECORD_STRUCT_V3 nativeRun;
    std::memcpy(&nativeRun, runV3.data() + 800, sizeof(nativeRun));
    if (nativeRun.nrImage != 10 || nativeRun.irisAperture != 141 ||
        nativeRun.cleanCircleRadius.radius != 500 ||
        std::strcmp(nativeRun.cbeiNormal.cttTasteFile, "CTT_FM_04.params.ini") != 0)
    {
        std::printf("FAIL: VER_3 display settings\n");
        ++failures;
    }

    const auto sourceV5 = BuildRunInfoBlobV5(in);
    in.sourceRunInfoBlob = sourceV5.data();
    in.sourceRunInfoBlobSize = sourceV5.size();
    const auto convertedV3 = BuildRunInfoBlobV3(in);
    std::memcpy(&nativeRun, convertedV3.data() + 800, sizeof(nativeRun));
    if (std::strcmp(nativeRun.cbeiNormal.cttTasteFile, "CTT_FM_04.params.ini") != 0 ||
        nativeRun.cleanCircleRadius.radius != 500)
    {
        std::printf("FAIL: VER_5 to VER_3 recipe conversion\n");
        ++failures;
    }

    if (failures == 0)
        std::printf("coretest: ALL PASS (VER_5 run=%zu; native run=%zu, images=%zu for %d frames)\n",
                    runBlob.size(), runV3.size(), imagesV3.size(), in.numberOfFrames);
    else
        std::printf("coretest: %d FAILURE(S)\n", failures);

    return failures == 0 ? 0 : 1;
}
