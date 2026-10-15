// dcconvert_demo -- example application that consumes the DfiToDiskCopy
// library (dcconvertlib) and performs the same job as dcconvert.exe.
//
// It demonstrates BOTH public entry points:
//   1. ConvertFile   -- straight file-to-file conversion.
//   2. ConvertBuffer -- load the input into memory, convert in-memory, and
//                       write the resulting bytes out, then confirm the two
//                       outputs are byte-identical.
//
// Usage:
//   dcconvert_demo <input.dcm> <output.dcm>
//                  [--version VER_5] [--max-dim N] [--detector NAME]
//
// Build it as part of this project, or link it against an installed package:
//   find_package(DfiToDiskCopy CONFIG REQUIRED)
//   target_link_libraries(app PRIVATE DfiToDiskCopy::dcconvertlib)

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "DfiToDiskCopy.h"
#include "Version.h"

using namespace dfitodiskcopy;

namespace
{
    std::vector<std::uint8_t> ReadFile(const std::string& path, bool& ok)
    {
        std::ifstream f(path, std::ios::binary);
        ok = f.good();
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>());
    }

    bool WriteFile(const std::string& path, const std::vector<std::uint8_t>& data)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f.good())
            return false;
        if (!data.empty())
            f.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
        return f.good();
    }

    void PrintReport(const char* how, const ConvertReport& r)
    {
        std::printf(
            "  [%s]\n"
            "    version   : %s (%s)\n"
            "    detector  : %s [%s]  (0018,1162)=%s iiFormat=%d iiRadius=%d\n"
            "    geometry  : %ux%u, %d frame(s)%s\n"
            "    pixels    : %s\n"
            "    cbei      : %s\n"
            "    run-info  : %zu bytes   image-info: %zu bytes\n",
            how,
            r.versionId.c_str(), r.versionFriendlyName.c_str(),
            r.detectorName.c_str(), r.detectorSource.c_str(),
            r.intensifierSize.c_str(), r.iiFormat, r.iiRadius,
            r.columns, r.rows, r.frames,
            r.pixelsScaled ? " (resized)" : "",
            r.pixelsConverted ? "converted to 16-bit MONOCHROME2"
                              : "already 16-bit MONOCHROME2 (unchanged)",
            r.cbeiFromInput ? "copied from input (3333,100A)"
                            : "filled with defaults (input had none)",
            r.runInfoBytes, r.imageInfoBytes);
    }

    void PrintUsage()
    {
        std::printf(
            "Usage: dcconvert_demo <input.dcm> <output.dcm>\n"
            "                      [--version VER_5] [--max-dim N] [--detector NAME]\n\n"
            "Demonstrates the DfiToDiskCopy library (ConvertFile + ConvertBuffer).\n");
    }
}

int main(int argc, char* argv[])
{
    std::string inputPath;
    std::string outputPath;
    std::string versionToken = "VER_5";
    ConvertOptions options;           // defaults: VER_5, auto detector, max-dim 1000

    std::vector<std::string> positionals;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--version" && i + 1 < argc)
            versionToken = argv[++i];
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

    if (positionals.size() != 2)
    {
        PrintUsage();
        return 2;
    }
    inputPath  = positionals[0];
    outputPath = positionals[1];

    options.version = ParseVersion(versionToken);
    if (options.version == Version::Unknown)
    {
        std::printf("Error: unknown --version '%s'.\n", versionToken.c_str());
        return 2;
    }

    std::printf("DfiToDiskCopy library demo\n%s -> %s\n\n",
                inputPath.c_str(), outputPath.c_str());

    // --- 1. File-to-file API -------------------------------------------------
    ConvertReport fileReport;
    ConvertStatus fileStatus = ConvertFile(inputPath, outputPath, options, &fileReport);
    if (fileStatus != ConvertStatus::Ok)
    {
        std::printf("ConvertFile failed: %s\n",
                    fileReport.message.empty() ? ToString(fileStatus)
                                               : fileReport.message.c_str());
        return 1;
    }
    std::printf("ConvertFile: OK -> %s\n", outputPath.c_str());
    PrintReport("file API", fileReport);

    // --- 2. In-memory buffer-to-buffer API -----------------------------------
    bool readOk = false;
    std::vector<std::uint8_t> inputBytes = ReadFile(inputPath, readOk);
    if (!readOk)
    {
        std::printf("\nSkipping buffer demo: could not re-open '%s'.\n", inputPath.c_str());
        return 0;
    }

    std::vector<std::uint8_t> outputBytes;
    ConvertReport bufReport;
    ConvertStatus bufStatus =
        ConvertBuffer(inputBytes.data(), inputBytes.size(), outputBytes, options, &bufReport);
    if (bufStatus != ConvertStatus::Ok)
    {
        std::printf("\nConvertBuffer failed: %s\n",
                    bufReport.message.empty() ? ToString(bufStatus)
                                              : bufReport.message.c_str());
        return 1;
    }

    const std::string bufferOutPath = outputPath + ".frombuffer.dcm";
    WriteFile(bufferOutPath, outputBytes);
    std::printf("\nConvertBuffer: OK -> %s (%zu bytes in memory)\n",
                bufferOutPath.c_str(), outputBytes.size());
    PrintReport("buffer API", bufReport);

    // --- Cross-check the two paths agree -------------------------------------
    bool fileOk = false;
    std::vector<std::uint8_t> fileOutBytes = ReadFile(outputPath, fileOk);
    const bool identical = fileOk && (fileOutBytes == outputBytes);
    std::printf("\nfile vs buffer output: %s\n",
                identical ? "IDENTICAL" : "DIFFERENT");

    return identical ? 0 : 1;
}
