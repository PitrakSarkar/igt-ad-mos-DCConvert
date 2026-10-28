// Implementation of the DfiToDiskCopy conversion library. The conversion logic
// here was extracted verbatim (behaviour-preserving) from the former dcconvert
// main.cpp so that the CLI and library share one implementation.

#include "DfiToDiskCopy.h"

#include <cmath>
#include <cstdlib>

#include "dcmtk/config/osconfig.h"
#include "dcmtk/dcmdata/dctk.h"
#include "dcmtk/dcmdata/dcistrmb.h"
#include "dcmtk/dcmdata/dcostrmb.h"

#include "DiskCopySerializerV5.h"
#include "SerializerInput.h"

namespace dfitodiskcopy
{
    namespace
    {
        // Private creator "A"; low element byte 0x0A (DISKCOPY_ELEMENT_BYTE).
        // Block 0x10 -> attribute element 0x100A. The receiver matches on the
        // creator string + group + element byte, so the block choice is free.
        constexpr Uint16 kRunInfoGroup   = 0x3333;
        constexpr Uint16 kImageInfoGroup = 0x9999;
        constexpr Uint16 kCreatorElement = 0x0010;
        constexpr Uint16 kDataElement    = 0x100A;
        const char*      kCreator        = "A";

        // Detector table mirroring dcm_diskcopy.c readStandardAttributes: the
        // receiver maps (0018,1162) IntensifierSize -> examPtr->detectorType, and
        // the persisted image records must carry the matching iiFormat/iiRadius
        // (iiRadius is derived from iiFormat's radius entry, so it is the same for
        // a given iiFormat). FD17 values verified byte-for-byte against a genuine
        // run.img export.
        struct DetectorInfo
        {
            const char*  name;
            const char*  intensifierSize;  // (0018,1162) DS value
            std::int32_t iiFormat;         // II_FMT_ENUM ordinal in the image record
            std::int32_t iiRadius;         // image-record iiRadius (0 = unverified)
        };
        constexpr DetectorInfo kDetectors[] = {
            {"FD17",   "301",  8, 301},  // DT_FD17,   FD_10_INCH  (verified)
            {"FD15",   "265",  8, 301},  // DT_FD15,   FD_10_INCH  (same iiFormat as FD17)
            {"FD12",   "207", 11,   0},  // DT_FD12,   FD_8_INCH   (radius unverified)
            {"BV312",  "310",  5,   0},  // DT_BV312,  II_12_INCH  (radius unverified)
            {"PX2020", "200", 12,   0},  // DT_PX2020, FD_PX_20_OV (radius unverified)
        };

        const DetectorInfo* FindDetector(const std::string& name)
        {
            for (const DetectorInfo& d : kDetectors)
                if (name == d.name)
                    return &d;
            return nullptr;
        }

        // Reverse lookup: adopt the detector the input already declares in
        // (0018,1162). Compared numerically so trailing DS padding ("301 ")
        // still matches.
        const DetectorInfo* FindDetectorByIntensifierSize(const char* dsValue)
        {
            const int v = std::atoi(dsValue);
            for (const DetectorInfo& d : kDetectors)
                if (std::atoi(d.intensifierSize) == v)
                    return &d;
            return nullptr;
        }

        // Insert one private group (creator + OB data blob) into the dataset.
        OFCondition InsertPrivateGroup(DcmDataset& ds, Uint16 group,
                                       const std::vector<std::uint8_t>& blob)
        {
            OFCondition cond = ds.putAndInsertString(
                DcmTag(group, kCreatorElement, EVR_LO), kCreator);
            if (cond.bad())
                return cond;

            DcmTag dataTag(group, kDataElement, EVR_OB);
            return ds.putAndInsertUint8Array(
                dataTag, blob.data(), static_cast<unsigned long>(blob.size()));
        }

        // Ensure the pixel module is single-channel 16-bit MONOCHROME2 with
        // PixelData == rows*cols*2*frames. Decisions are driven by the ACTUAL
        // bytes per pixel (PixelData / rows*cols*frames), not the header tags,
        // because the files we fix often carry a lying header (RGB pixels tagged
        // as 16-bit mono). 3 B/px -> RGB8 luminance; 1 B/px -> 8-bit mono; 2 B/px
        // -> 16-bit mono. Genuine, already-consistent 16-bit mono is left
        // untouched.
        bool NormalizePixelsToMono16(DcmDataset& ds, Uint16 rows, Uint16 columns,
                                     Sint32 frames, bool& converted, std::string& err)
        {
            converted = false;

            Uint16 samplesPerPixel = 0;
            ds.findAndGetUint16(DCM_SamplesPerPixel, samplesPerPixel);
            Uint16 bitsAllocated = 0;
            ds.findAndGetUint16(DCM_BitsAllocated, bitsAllocated);

            const size_t npixels =
                static_cast<size_t>(rows) * columns * static_cast<size_t>(frames);
            if (npixels == 0)
            {
                err = "zero pixel count.";
                return false;
            }

            const Uint8* px = nullptr;
            unsigned long count = 0;
            if (ds.findAndGetUint8Array(DCM_PixelData, px, &count).bad() || px == nullptr)
            {
                err = "cannot read PixelData (compressed input is not supported).";
                return false;
            }

            if (static_cast<size_t>(count) % npixels != 0)
            {
                char msg[160];
                std::snprintf(msg, sizeof(msg),
                              "PixelData %lu bytes is not a whole number of bytes "
                              "per pixel for %zu pixels (%ux%u x %d frame(s)).",
                              count, npixels, columns, rows, static_cast<int>(frames));
                err = msg;
                return false;
            }
            const size_t bpp = static_cast<size_t>(count) / npixels;

            // Genuine, consistent 16-bit mono -> keep pixel tags as they are.
            if (bpp == 2 && samplesPerPixel == 1 && bitsAllocated == 16)
                return true;

            std::vector<Uint16> gray(npixels, 0);

            if (bpp == 3)
            {
                // Interleaved RGB8 -> BT.601 luminance, stretch 8->16 bit (x257).
                for (size_t i = 0; i < npixels; ++i)
                {
                    const double r = px[i * 3 + 0];
                    const double g = px[i * 3 + 1];
                    const double b = px[i * 3 + 2];
                    double v = (0.299 * r + 0.587 * g + 0.114 * b) * 257.0;
                    if (v < 0.0) v = 0.0;
                    if (v > 65535.0) v = 65535.0;
                    gray[i] = static_cast<Uint16>(v + 0.5);
                }
            }
            else if (bpp == 1)
            {
                for (size_t i = 0; i < npixels; ++i)
                    gray[i] = static_cast<Uint16>(px[i] * 257);
            }
            else if (bpp == 2)
            {
                // 2 B/px but tags disagree -> reinterpret as little-endian 16-bit.
                for (size_t i = 0; i < npixels; ++i)
                    gray[i] = static_cast<Uint16>(px[i * 2] |
                                                  (static_cast<Uint16>(px[i * 2 + 1]) << 8));
            }
            else
            {
                char msg[160];
                std::snprintf(msg, sizeof(msg),
                              "unsupported pixel layout (%zu bytes/pixel). Only 1, 2, "
                              "or 3 bytes/pixel are handled.", bpp);
                err = msg;
                return false;
            }

            OFCondition c = ds.putAndInsertUint16Array(
                DCM_PixelData, gray.data(), static_cast<unsigned long>(gray.size()));
            if (c.bad())
            {
                err = std::string("cannot write converted PixelData: ") + c.text();
                return false;
            }

            ds.putAndInsertUint16(DCM_SamplesPerPixel, 1);
            ds.putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
            ds.putAndInsertUint16(DCM_BitsAllocated, 16);
            ds.putAndInsertUint16(DCM_BitsStored, 16);
            ds.putAndInsertUint16(DCM_HighBit, 15);
            ds.putAndInsertUint16(DCM_PixelRepresentation, 0);
            ds.findAndDeleteElement(DCM_PlanarConfiguration);

            converted = true;
            return true;
        }

        // Scale 16-bit MONOCHROME2 pixels so the longer side is exactly maxDim,
        // preserving the aspect ratio (bilinear resampling, per frame). Scales up
        // or down. Updates the dataset's PixelData/Rows/Columns and the caller's
        // rows/columns. No-op when maxDim <= 0 or already the right size. Assumes
        // pixels are already 16-bit single channel.
        bool ResizeMono16(DcmDataset& ds, Uint16& rows, Uint16& columns,
                          Sint32 frames, int maxDim, bool& scaled, std::string& err)
        {
            scaled = false;
            if (maxDim <= 0)
                return true;

            const Uint16 longer = (rows > columns) ? rows : columns;
            if (longer == maxDim)
                return true;

            const double factor = static_cast<double>(maxDim) / longer;
            Uint16 dstRows = static_cast<Uint16>(rows * factor + 0.5);
            Uint16 dstCols = static_cast<Uint16>(columns * factor + 0.5);
            if (dstRows < 1) dstRows = 1;
            if (dstCols < 1) dstCols = 1;

            const Uint16* src = nullptr;
            unsigned long count = 0;
            if (ds.findAndGetUint16Array(DCM_PixelData, src, &count).bad() || src == nullptr)
            {
                err = "cannot read 16-bit PixelData for scaling.";
                return false;
            }
            const size_t srcFrame = static_cast<size_t>(rows) * columns;
            const size_t dstFrame = static_cast<size_t>(dstRows) * dstCols;
            std::vector<Uint16> dst(dstFrame * static_cast<size_t>(frames), 0);

            const double sx = static_cast<double>(columns) / dstCols;
            const double sy = static_cast<double>(rows) / dstRows;

            for (Sint32 f = 0; f < frames; ++f)
            {
                const Uint16* sp = src + static_cast<size_t>(f) * srcFrame;
                Uint16* dp = dst.data() + static_cast<size_t>(f) * dstFrame;
                for (Uint16 y = 0; y < dstRows; ++y)
                {
                    double fy = (y + 0.5) * sy - 0.5;
                    if (fy < 0.0) fy = 0.0;
                    int y0 = static_cast<int>(fy);
                    int y1 = (y0 + 1 < rows) ? y0 + 1 : rows - 1;
                    const double wy = fy - y0;
                    for (Uint16 x = 0; x < dstCols; ++x)
                    {
                        double fx = (x + 0.5) * sx - 0.5;
                        if (fx < 0.0) fx = 0.0;
                        int x0 = static_cast<int>(fx);
                        int x1 = (x0 + 1 < columns) ? x0 + 1 : columns - 1;
                        const double wx = fx - x0;
                        const double top = sp[y0 * columns + x0] * (1.0 - wx) +
                                           sp[y0 * columns + x1] * wx;
                        const double bot = sp[y1 * columns + x0] * (1.0 - wx) +
                                           sp[y1 * columns + x1] * wx;
                        double v = top * (1.0 - wy) + bot * wy;
                        if (v < 0.0) v = 0.0;
                        if (v > 65535.0) v = 65535.0;
                        dp[y * dstCols + x] = static_cast<Uint16>(v + 0.5);
                    }
                }
            }

            OFCondition c = ds.putAndInsertUint16Array(
                DCM_PixelData, dst.data(), static_cast<unsigned long>(dst.size()));
            if (c.bad())
            {
                err = std::string("cannot write scaled PixelData: ") + c.text();
                return false;
            }
            ds.putAndInsertUint16(DCM_Rows, dstRows);
            ds.putAndInsertUint16(DCM_Columns, dstCols);

            rows = dstRows;
            columns = dstCols;
            scaled = true;
            return true;
        }

        // The shared conversion pipeline, operating on an already-loaded dataset.
        // Does no file/stream I/O; both ConvertFile and ConvertBuffer call this.
        ConvertStatus ConvertDataset(DcmDataset& ds, const ConvertOptions& opt,
                                     ConvertReport& rep)
        {
            const VersionInfo* vinfo = GetVersionInfo(opt.version);
            if (opt.version == Version::Unknown || vinfo == nullptr)
            {
                rep.message = "unknown target version.";
                return ConvertStatus::UnsupportedVersion;
            }
            rep.versionId = vinfo->id;
            rep.versionFriendlyName = vinfo->friendlyName;

            if (opt.version != Version::V3 && opt.version != Version::V5)
            {
                rep.message = "only VER_3 (native Zen90 layout) and VER_5 (Zenition 9.x.x) are implemented.";
                return ConvertStatus::UnsupportedVersion;
            }

            const bool detectorExplicit = !opt.detector.empty();
            if (detectorExplicit && FindDetector(opt.detector) == nullptr)
            {
                rep.message = "unknown detector '" + opt.detector +
                              "' (use FD17, FD15, FD12, BV312, PX2020).";
                return ConvertStatus::UnknownDetector;
            }

            // Adopt CBEI recipes from the input's (3333,100A) blob when present.
            std::vector<std::uint8_t> sourceRunInfo;
            {
                const Uint8* blob = nullptr;
                unsigned long blobLen = 0;
                if (ds.findAndGetUint8Array(DcmTagKey(kRunInfoGroup, kDataElement),
                                            blob, &blobLen).good() && blob != nullptr)
                    sourceRunInfo.assign(blob, blob + blobLen);
            }
            const bool cbeiFromInput = !sourceRunInfo.empty();
            if (cbeiFromInput && sourceRunInfo.size() != kRunInfoV3Size && sourceRunInfo.size() != kRunInfoV5Size)
            {
                rep.message = "input private run-info is not a supported VER_3 or VER_5 record layout.";
                return ConvertStatus::SerializationFailed;
            }

            // Resolve detector: explicit --detector > input's (0018,1162) > FD17.
            const DetectorInfo* detector = nullptr;
            bool detectorFromInput = false;
            if (detectorExplicit)
            {
                detector = FindDetector(opt.detector);
            }
            else
            {
                OFString existingSize;
                if (ds.findAndGetOFString(DCM_IntensifierSize, existingSize).good() &&
                    existingSize.size() > 0)
                {
                    detector = FindDetectorByIntensifierSize(existingSize.c_str());
                    detectorFromInput = (detector != nullptr);
                }
                if (detector == nullptr)
                    detector = FindDetector("FD17");  // default
            }

            rep.detectorName = detector->name;
            rep.detectorSource = detectorFromInput ? "input"
                               : detectorExplicit  ? "explicit"
                                                   : "default";
            rep.intensifierSize = detector->intensifierSize;
            rep.iiFormat = detector->iiFormat;
            rep.iiRadius = detector->iiRadius;
            if (opt.version == Version::V3 && std::string(detector->name) == "PX2020")
            {
                rep.message = "PX2020 is not supported by the native Zen90 receiver.";
                return ConvertStatus::UnknownDetector;
            }

            // Extract geometry.
            Uint16 rows = 0, columns = 0;
            ds.findAndGetUint16(DCM_Rows, rows);
            ds.findAndGetUint16(DCM_Columns, columns);

            Sint32 frames = 1;
            if (ds.findAndGetSint32(DCM_NumberOfFrames, frames).bad() || frames < 1)
                frames = 1;

            if (rows == 0 || columns == 0)
            {
                rep.message = "input has no Rows/Columns.";
                return ConvertStatus::MissingGeometry;
            }

            // Normalize pixels to 16-bit MONOCHROME2 (converts RGB/8-bit).
            bool pixelsConverted = false;
            if (!NormalizePixelsToMono16(ds, rows, columns, frames, pixelsConverted, rep.message))
                return ConvertStatus::UnsupportedPixelData;

            // Optional aspect-preserving resize.
            bool pixelsScaled = false;
            if (!ResizeMono16(ds, rows, columns, frames, opt.maxDim, pixelsScaled, rep.message))
                return ConvertStatus::UnsupportedPixelData;

            if (opt.version == Version::V3 && (rows != 1000 || columns != 1000))
            {
                rep.message = "VER_3 requires the target's native 1000x1000 image geometry.";
                return ConvertStatus::UnsupportedPixelData;
            }

            // Keep NumberOfFrames explicit so per-frame buffering matches.
            char framesText[16];
            std::snprintf(framesText, sizeof(framesText), "%d", static_cast<int>(frames));
            ds.putAndInsertString(DCM_NumberOfFrames, framesText);

            // The receiver derives the exam detectorType from (0018,1162); the
            // persisted image records carry the matching iiFormat/iiRadius.
            ds.putAndInsertString(DCM_IntensifierSize, detector->intensifierSize);

            SerializerInput in;
            in.rows           = rows;
            in.columns        = columns;
            in.numberOfFrames = frames;
            in.iiFormat       = detector->iiFormat;
            in.iiRadius       = detector->iiRadius;
            in.sourceRunInfoBlob     = cbeiFromInput ? sourceRunInfo.data() : nullptr;
            in.sourceRunInfoBlobSize = sourceRunInfo.size();

            const std::vector<std::uint8_t> runInfo = opt.version == Version::V3
                ? BuildRunInfoBlobV3(in) : BuildRunInfoBlobV5(in);
            const std::vector<std::uint8_t> imageInfo = opt.version == Version::V3
                ? BuildImageInfoBlobV3(in) : BuildImageInfoBlobV5(in);

            if (InsertPrivateGroup(ds, kRunInfoGroup, runInfo).bad() ||
                InsertPrivateGroup(ds, kImageInfoGroup, imageInfo).bad())
            {
                rep.message = "failed to insert private groups.";
                return ConvertStatus::SerializationFailed;
            }

            if (opt.setSoftwareVersion && vinfo->softwareVersion[0] != '\0')
                ds.putAndInsertString(DCM_SoftwareVersions, vinfo->softwareVersion);

            rep.rows = rows;
            rep.columns = columns;
            rep.frames = static_cast<int>(frames);
            rep.pixelsConverted = pixelsConverted;
            rep.pixelsScaled = pixelsScaled;
            rep.cbeiFromInput = cbeiFromInput;
            rep.runInfoBytes = runInfo.size();
            rep.imageInfoBytes = imageInfo.size();
            return ConvertStatus::Ok;
        }

        // Serialise a (converted) fileformat to a byte vector using DCMTK's
        // memory stream, draining the fixed buffer in a loop so arbitrarily large
        // pixel data is handled. Uncompressed transfer syntaxes only.
        bool WriteFileFormatToBuffer(DcmFileFormat& ff, E_TransferSyntax xfer,
                                     std::vector<std::uint8_t>& out, std::string& err)
        {
            out.clear();

            std::vector<Uint8> chunk(64 * 1024);
            DcmOutputBufferStream stream(chunk.data(),
                                         static_cast<offile_off_t>(chunk.size()));

            ff.transferInit();
            OFCondition cond;
            do
            {
                cond = ff.write(stream, xfer, EET_ExplicitLength, nullptr);
                void* flat = nullptr;
                offile_off_t len = 0;
                stream.flushBuffer(flat, len);
                if (len > 0 && flat != nullptr)
                {
                    const Uint8* p = static_cast<const Uint8*>(flat);
                    out.insert(out.end(), p, p + static_cast<size_t>(len));
                }
            } while (cond == EC_StreamNotifyClient);
            ff.transferEnd();

            if (cond.bad())
            {
                err = std::string("cannot serialise output: ") + cond.text();
                out.clear();
                return false;
            }
            return true;
        }
    }

    const char* ToString(ConvertStatus status)
    {
        switch (status)
        {
        case ConvertStatus::Ok:                   return "OK";
        case ConvertStatus::InvalidArguments:     return "invalid arguments";
        case ConvertStatus::UnsupportedVersion:   return "unsupported version";
        case ConvertStatus::UnknownDetector:      return "unknown detector";
        case ConvertStatus::CannotReadInput:      return "cannot read input";
        case ConvertStatus::MissingGeometry:      return "missing geometry";
        case ConvertStatus::UnsupportedPixelData: return "unsupported pixel data";
        case ConvertStatus::SerializationFailed:  return "serialization failed";
        case ConvertStatus::CannotWriteOutput:    return "cannot write output";
        }
        return "unknown status";
    }

    ConvertStatus ConvertFile(const std::string& inputPath,
                              const std::string& outputPath,
                              const ConvertOptions& options,
                              ConvertReport* report)
    {
        ConvertReport local;
        ConvertReport& rep = report ? *report : local;
        rep = ConvertReport{};

        if (inputPath.empty() || outputPath.empty())
        {
            rep.message = "input and output paths are required.";
            return ConvertStatus::InvalidArguments;
        }

        DcmFileFormat fileformat;
        OFCondition cond = fileformat.loadFile(inputPath.c_str());
        if (cond.bad())
        {
            rep.message = std::string("cannot read '") + inputPath + "': " + cond.text();
            return ConvertStatus::CannotReadInput;
        }

        DcmDataset* ds = fileformat.getDataset();
        const ConvertStatus st = ConvertDataset(*ds, options, rep);
        if (st != ConvertStatus::Ok)
            return st;

        const E_TransferSyntax xfer = ds->getOriginalXfer();
        cond = fileformat.saveFile(outputPath.c_str(), xfer);
        if (cond.bad())
        {
            rep.message = std::string("cannot write '") + outputPath + "': " + cond.text();
            return ConvertStatus::CannotWriteOutput;
        }
        return ConvertStatus::Ok;
    }

    ConvertStatus ConvertBuffer(const std::uint8_t* inputData,
                                std::size_t inputSize,
                                std::vector<std::uint8_t>& output,
                                const ConvertOptions& options,
                                ConvertReport* report)
    {
        ConvertReport local;
        ConvertReport& rep = report ? *report : local;
        rep = ConvertReport{};
        output.clear();

        if (inputData == nullptr || inputSize == 0)
        {
            rep.message = "input buffer is null or empty.";
            return ConvertStatus::InvalidArguments;
        }
        if ((inputSize & 1u) != 0)
        {
            rep.message = "input buffer length must be an even number of bytes.";
            return ConvertStatus::InvalidArguments;
        }

        DcmInputBufferStream inStream;
        inStream.setBuffer(inputData, static_cast<offile_off_t>(inputSize));
        inStream.setEos();

        DcmFileFormat fileformat;
        fileformat.transferInit();
        OFCondition cond = fileformat.read(inStream);
        fileformat.transferEnd();
        if (cond.bad())
        {
            rep.message = std::string("cannot parse input buffer as DICOM: ") + cond.text();
            return ConvertStatus::CannotReadInput;
        }

        DcmDataset* ds = fileformat.getDataset();
        const ConvertStatus st = ConvertDataset(*ds, options, rep);
        if (st != ConvertStatus::Ok)
            return st;

        const E_TransferSyntax xfer = ds->getOriginalXfer();
        if (!WriteFileFormatToBuffer(fileformat, xfer, output, rep.message))
            return ConvertStatus::CannotWriteOutput;
        return ConvertStatus::Ok;
    }
}
