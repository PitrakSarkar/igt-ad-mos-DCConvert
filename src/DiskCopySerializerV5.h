#ifndef DFITODISKCOPY_DISKCOPY_SERIALIZER_V5_H
#define DFITODISKCOPY_DISKCOPY_SERIALIZER_V5_H

#include <cstdint>
#include <cstddef>
#include <vector>

#include "SerializerInput.h"

namespace dfitodiskcopy
{
    constexpr std::size_t kRunInfoV3Size = 4252;
    constexpr std::size_t kRunInfoV5Size = 4464;

    std::vector<std::uint8_t> BuildRunInfoBlobV3(const SerializerInput& in);
    std::vector<std::uint8_t> BuildImageInfoBlobV3(const SerializerInput& in);

    // Builds the run-info private blob for tag (3333,xx0A):
    //   PATIENT + EXAM + RUN + IMAGE records (concatenated, parsed sequentially
    //   by PrivateAttrRunCallBack on the receiver).
    std::vector<std::uint8_t> BuildRunInfoBlobV5(const SerializerInput& in);

    // Builds the image-info private blob for tag (9999,xx0A):
    //   numberOfFrames * IMAGE record. Delivered last; triggers StoreImage.
    std::vector<std::uint8_t> BuildImageInfoBlobV5(const SerializerInput& in);
}

#endif
