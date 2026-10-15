DfiToDiskCopy
=============

Makes a standard DICOM XA object DiskCopy-importable for the Philips DFI
receiver by normalising the pixels to 16-bit MONOCHROME2, optionally rescaling,
and inserting the required private run-info (3333,100A) and image-info
(9999,100A) record blobs.


Command-line tool (dcconvert.exe)
---------------------------------
dcconvert.exe --version VER_5 <input.dcm> <output.dcm> [--max-dim <N>]
              [--detector <name>] [--set-software-version]

Example:
build\Release> dcconvert.exe --version VER_5 "C:\...\IM_0017" "C:\...\out.dcm" --max-dim 1000


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
