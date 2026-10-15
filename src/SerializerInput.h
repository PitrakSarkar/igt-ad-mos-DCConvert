#ifndef DFITODISKCOPY_SERIALIZER_INPUT_H
#define DFITODISKCOPY_SERIALIZER_INPUT_H

#include <cstddef>
#include <cstdint>

namespace dfitodiskcopy
{
    // Plain data extracted from the input DICOM object, used to fill the
    // internal DiskCopy record structs. Fields that the DFI receiver reads
    // from *standard* DICOM tags (patient name, IDs, dates, etc.) are NOT
    // duplicated here -- readStandardAttributes() supplies those. This holds
    // only what the private blob must carry for StoreImage to succeed.
    struct SerializerInput
    {
        std::uint32_t rows            = 0;  // (0028,0010) Rows
        std::uint32_t columns         = 0;  // (0028,0011) Columns
        std::int32_t  numberOfFrames  = 1;  // (0028,0008) Number of Frames
        std::int32_t  runNumber       = 1;
        std::int32_t  imageNumberBase = 1;  // first image number in the run
        // II_FMT_ENUM ordinal for the image record; MUST match the detector the
        // receiver derives from (0018,1162). FD17 -> FD_10_INCH (8). See main.cpp.
        std::int32_t  iiFormat        = 8;
        // Image-intensifier / detector radius for the image record. Verified 301
        // for FD17 against a genuine run.img export. Kept in lockstep with iiFormat.
        std::int32_t  iiRadius        = 301;

        // When the source object already carries a DiskCopy (3333,100A) run-info
        // blob, point these at its raw bytes. The serializer then copies the CBEI
        // image-processing recipes out of it (geometry/dims/detector are always
        // rebuilt so --max-dim/--detector still apply). Null/0 -> use defaults.
        const std::uint8_t* sourceRunInfoBlob     = nullptr;
        std::size_t         sourceRunInfoBlobSize = 0;
    };
}

#endif
