DfiToDiskCopy
=============

Makes a standard DICOM XA object DiskCopy-importable for the Philips DFI
receiver by normalising the pixels to 16-bit MONOCHROME2, optionally rescaling,
and inserting the required private run-info (3333,100A) and image-info
(9999,100A) record blobs.

Pixel values are written in the DFI native range: 14-bit unsigned (0..16383),
BitsStored 14 / HighBit 13 / PixelRepresentation 0. Signed input is offset to
unsigned; input whose values exceed 16383 (e.g. full-range 16-bit or converted
8-bit/RGB) is rescaled linearly from its BitsStored range. Input already within
0..16383 (such as a DFI DiskCopy export) keeps its pixel values unchanged.
DFI stores pixels in a signed 16-bit buffer, so full-range values would
otherwise display wrong or wrap negative.


Command-line tool (dcconvert.exe)
---------------------------------
dcconvert.exe --version <VER_3|VER_5> <input.dcm> <output.dcm> [--max-dim <N>]
              [--detector <name>] [--set-software-version]

Example:
build\Release> dcconvert.exe --version VER_5 "C:\...\IM_0017" "C:\...\out.dcm" --max-dim 1000

Native Zen90 / K:\DFI compatibility
----------------------------------
Use --version VER_3 --max-dim 1000 for the native K:\DFI receiver:

    dcconvert.exe --version VER_3 input.dcm output_zen90.dcm --max-dim 1000

The reference header set's VER_3 schema matches this receiver's native ABI:
336-byte patient, 464-byte exam, 1168-byte run, and 2284-byte image records.
Its run-info blob is 4252 bytes; image-info is 2284 bytes per frame.
Do not infer compatibility from product release names: the reference's
historical VER_4 header differs from the native target's VER_4 header.
VER_5 remains the default library option for the reference platform.

VER_3 requires final geometry of 1000x1000 and does not support PX2020.
Recipes can be adopted from either a VER_3 or VER_5 source run-info blob;
all other source record sizes are rejected. Patient ID and standard
patient/study tags are preserved, not replaced with the selected patient's
identity. The receiver appends to an existing patient only when Patient ID
matches; a different ID still creates a different patient.

Reconvert older VER_5 outputs before sending them to K:\DFI. Native receivers
with private-record validation reject incompatible layouts rather than copying
them into the wrong structures (which corrupts run/image display settings).


Library (dcconvertlib)
----------------------
The same conversion is available as a static library so it can be called from
your own C++ application. Link against the dcconvertlib target and include
src/DfiToDiskCopy.h. The header has no DCMTK dependency.

    #include "DfiToDiskCopy.h"
    using namespace dfitodiskcopy;

    ConvertOptions opt;            // defaults: VER_5, auto detector, max-dim 1000
    opt.detector = "FD17";         // optional; empty => adopt input's, else FD17
    opt.maxDim   = 1000;           // 0 keeps native size

    // File to file:
    ConvertReport report;
    ConvertStatus st = ConvertFile("in.dcm", "out.dcm", opt, &report);
    if (st != ConvertStatus::Ok)
        fprintf(stderr, "convert failed: %s\n", report.message.c_str());

    // In-memory DICOM Part-10 bytes in, converted bytes out:
    std::vector<std::uint8_t> out;
    ConvertStatus st2 = ConvertBuffer(inData, inSize, out, opt, &report);

Both functions return a ConvertStatus (Ok == 0) and optionally fill a
ConvertReport describing the resolved detector, final geometry, whether pixels
were converted/resized, and the emitted blob sizes. On failure
ConvertReport::message holds the detail.


Building
--------
Requires DCMTK (install via vcpkg). Configure with the vcpkg toolchain, e.g.:

    cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
    cmake --build build --config Release

Targets:
  dccore          core version model + DiskCopy blob serializers (no DICOM dep)
  dcconvertlib    the reusable conversion library (ConvertFile / ConvertBuffer)
  dcconvert       thin CLI wrapper over dcconvertlib
  dcconvert_demo  example app consuming the library (examples/dcconvert_demo.cpp)
  coretest        self-test for the serializer blob sizes
  sizeprobe       ABI size probe for the versioned structs


Installing / consuming from another project
-------------------------------------------
    cmake --install build --config Release --prefix <prefix>

Then in your own CMakeLists.txt:

    find_package(DfiToDiskCopy CONFIG REQUIRED)
    target_link_libraries(myapp PRIVATE DfiToDiskCopy::dcconvertlib)

Point CMake at the install prefix (and the vcpkg toolchain for DCMTK) with
-DCMAKE_PREFIX_PATH=<prefix>. See examples/dcconvert_demo.cpp for a complete
application that uses both ConvertFile and ConvertBuffer.
