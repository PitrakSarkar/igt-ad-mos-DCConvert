// dcconvert -- make a standard DICOM XA object DiskCopy-importable by adding
// the Philips private groups the DFI receiver requires.
//
// This is a thin command-line wrapper around the DfiToDiskCopy library
// (DfiToDiskCopy.h). All conversion logic lives in the library so it can be
// reused from other applications; this file only parses arguments, calls
// ConvertFile, and prints a summary.
//
// Usage:
//   dcconvert --version VER_5 <input.dcm> <output.dcm>
//            [--set-software-version] [--max-dim <N>] [--detector <name>]

#include <cstdio>
#include <string>
#include <vector>

#include "DfiToDiskCopy.h"
#include "Version.h"

using namespace dfitodiskcopy;

namespace
{
    void PrintUsage()
    {
        std::printf(
            "Usage: dcconvert --version <id> <input.dcm> <output.dcm>\n"
            "                 [--set-software-version] [--max-dim <N>]\n"
            "                 [--detector <name>]\n\n"
            "  --max-dim <N>  scale so the longer side is exactly N pixels (up or\n"
            "                 down), keeping the aspect ratio (0 = keep native).\n"
            "                 Default 1000, the genuine DiskCopy stored-image size.\n"
            "  --detector <name>  target detector. Sets (0018,1162) and the image-record\n"
            "                 iiFormat/iiRadius consistently. If omitted, the input's own\n"
            "                 (0018,1162) is adopted when recognised, else FD17 is used.\n"
            "                 One of: FD17, FD15, FD12, BV312, PX2020.\n\n"
            "The image is always (re)built: pixels normalised/resized (--max-dim), detector\n"
            "tags and records rebuilt. CBEI image-processing recipes are copied from the\n"
            "input's (3333,100A) group when present, otherwise filled with FD17 defaults.\n\n"
            "Supported --version values:\n%s",
            SupportedVersionsHelp().c_str());
    }

    const char* DetectorSourceSuffix(const std::string& src)
    {
        if (src == "input")    return " [from input]";
        if (src == "explicit") return " [--detector]";
        return " [default]";
    }
}

int main(int argc, char* argv[])
{
    std::string versionToken;
    ConvertOptions options;
    options.detector.clear();      // empty => auto (input's 0018,1162 or FD17)

    std::vector<std::string> positionals;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--version" && i + 1 < argc)
            versionToken = argv[++i];
        else if (arg == "--set-software-version")
            options.setSoftwareVersion = true;
        else if (arg == "--detector" && i + 1 < argc)
            options.detector = argv[++i];
        else if (arg == "--max-dim" && i + 1 < argc)
            options.maxDim = std::atoi(argv[++i]);
        else if (arg == "-h" || arg == "--help")
        {
            PrintUsage();
            return 0;
        }
        else
            positionals.push_back(arg);
    }

    if (versionToken.empty() || positionals.size() != 2)
    {
        PrintUsage();
        return 2;
    }
    const std::string inputPath  = positionals[0];
    const std::string outputPath = positionals[1];

    options.version = ParseVersion(versionToken);
    if (options.version == Version::Unknown)
    {
        std::printf("Error: unknown --version '%s'.\n\n", versionToken.c_str());
        PrintUsage();
        return 2;
    }

    ConvertReport report;
    const ConvertStatus status = ConvertFile(inputPath, outputPath, options, &report);
    if (status != ConvertStatus::Ok)
    {
        std::printf("Error: %s\n", report.message.empty()
                                       ? ToString(status)
                                       : report.message.c_str());
        // Usage errors (bad version/detector) get the help text, like before.
        if (status == ConvertStatus::UnsupportedVersion ||
            status == ConvertStatus::UnknownDetector)
        {
            std::printf("\n");
            PrintUsage();
            return 2;
        }
        return 1;
    }

    std::printf(
        "OK: %s -> %s\n"
        "    version   : %s (%s)\n"
        "    detector  : %s%s  (0018,1162)=%s iiFormat=%d iiRadius=%d\n"
        "    geometry  : %ux%u, %d frame(s)%s\n"
        "    pixels    : %s\n"
        "    cbei      : %s\n"
        "    run-info  : (3333,100A) OB  %zu bytes\n"
        "    image-info: (9999,100A) OB  %zu bytes\n",
        inputPath.c_str(), outputPath.c_str(),
        report.versionId.c_str(), report.versionFriendlyName.c_str(),
        report.detectorName.c_str(), DetectorSourceSuffix(report.detectorSource),
        report.intensifierSize.c_str(), report.iiFormat, report.iiRadius,
        report.columns, report.rows, report.frames,
        report.pixelsScaled ? " (resized)" : "",
        report.pixelsMappedTo14Bit
            ? (report.pixelsConverted ? "converted to MONOCHROME2, rescaled to native 14-bit"
                                      : "rescaled to native 14-bit (0..16383)")
            : (report.pixelsConverted ? "converted to MONOCHROME2 (native 14-bit)"
                                      : "already native 14-bit range (values unchanged)"),
        report.cbeiFromInput ? "copied from input (3333,100A)"
                             : "filled with FD17 defaults (input had none)",
        report.runInfoBytes, report.imageInfoBytes);
    return 0;
}
