#ifndef DFITODISKCOPY_DFITODISKCOPY_H
#define DFITODISKCOPY_DFITODISKCOPY_H

// Public API for the DfiToDiskCopy conversion library.
//
// This is the same work the dcconvert.exe CLI performs, exposed as a linkable
// static library so it can be called directly from an application. Given a
// standard DICOM XA object it produces a DiskCopy-importable DICOM object by:
//   * normalising the pixels to single-channel 16-bit MONOCHROME2 (converting
//     8-bit mono / RGB when needed) so PixelData == Rows*Columns*2*Frames,
//   * optionally rescaling so the longer side is exactly ConvertOptions::maxDim,
//   * inserting the Philips private run-info (3333,100A) and image-info
//     (9999,100A) record blobs the DFI receiver requires.
//
// This header is intentionally free of any DCMTK dependency; callers only need
// the C++ standard library and Version.h. Link against the dcconvertlib target.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Version.h"

namespace dfitodiskcopy
{
    // Outcome of a conversion. Ok == 0; everything else is a failure whose
    // human-readable detail is in ConvertReport::message.
    enum class ConvertStatus
    {
        Ok = 0,
        InvalidArguments,     // null/zero buffer, odd-length buffer, etc.
        UnsupportedVersion,   // version is unknown or not yet implemented
        UnknownDetector,      // options.detector is not a recognised name
        CannotReadInput,      // input file/buffer could not be parsed as DICOM
        MissingGeometry,      // input has no Rows/Columns
        UnsupportedPixelData, // compressed or unhandled pixel layout
        SerializationFailed,  // building/inserting the private groups failed
        CannotWriteOutput     // output file/buffer could not be produced
    };

    // Short, static description for a status code (never null).
    const char* ToString(ConvertStatus status);

    // Knobs for a single conversion. Defaults reproduce the dcconvert CLI
    // defaults (VER_5, auto-detector, --max-dim 1000).
    struct ConvertOptions
    {
        // Target DiskCopy format. Only Version::V5 is currently implemented.
        Version version = Version::V5;

        // Target detector name: one of FD17, FD15, FD12, BV312, PX2020.
        // Empty string => auto: adopt the input's (0018,1162) when recognised,
        // otherwise fall back to FD17.
        std::string detector;

        // Scale so the longer side is exactly this many pixels (up or down),
        // preserving aspect ratio. 0 keeps the native size.
        int maxDim = 1000;

        // When true, overwrite (0018,1020) Software Versions with the value
        // associated with the target version.
        bool setSoftwareVersion = false;
    };

    // Informational result of a conversion (filled on success, and partially
    // filled on failure). Mirrors the summary the CLI prints.
    struct ConvertReport
    {
        std::string versionId;            // e.g. "VER_5"
        std::string versionFriendlyName;  // e.g. "Zenition 9.x.x"

        std::string detectorName;         // resolved detector, e.g. "FD17"
        std::string detectorSource;       // "input", "explicit", or "default"
        std::string intensifierSize;      // (0018,1162) value written
        int iiFormat = 0;                 // image-record iiFormat
        int iiRadius = 0;                 // image-record iiRadius

        unsigned int rows = 0;            // final Rows (after any resize)
        unsigned int columns = 0;         // final Columns (after any resize)
        int frames = 1;                   // Number of Frames

        bool pixelsConverted = false;     // pixels were normalised to mono16
        bool pixelsScaled = false;        // pixels were resized for --max-dim
        bool cbeiFromInput = false;       // CBEI recipes copied from input blob

        std::size_t runInfoBytes = 0;     // size of the (3333,100A) blob
        std::size_t imageInfoBytes = 0;   // size of the (9999,100A) blob

        std::string message;              // error detail on failure, else empty
    };

    // Convert a DICOM file on disk into a DiskCopy-importable DICOM file.
    // The output transfer syntax matches the input's (uncompressed only).
    // `report` is optional; when provided it is populated with details.
    ConvertStatus ConvertFile(const std::string& inputPath,
                              const std::string& outputPath,
                              const ConvertOptions& options,
                              ConvertReport* report = nullptr);

    // Convert an in-memory DICOM Part-10 object (with preamble + meta header)
    // into a new in-memory DICOM Part-10 object. `inputData`/`inputSize` is the
    // source; the converted bytes are written to `output` (cleared first).
    // `inputSize` must be a non-zero even number. `report` is optional.
    ConvertStatus ConvertBuffer(const std::uint8_t* inputData,
                                std::size_t inputSize,
                                std::vector<std::uint8_t>& output,
                                const ConvertOptions& options,
                                ConvertReport* report = nullptr);
}

#endif
