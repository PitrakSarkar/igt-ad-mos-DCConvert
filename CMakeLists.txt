cmake_minimum_required(VERSION 3.20)
project(DfiToDiskCopy VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

# Path to the Philips DiskCopyCommon headers (self-contained DiskCopyData_Vx.h + DiskCopyDataGlob.h).
# Override with -DDISKCOPY_INC=<path> if the platform repo lives elsewhere.
set(DISKCOPY_INC "C:/Git/IGT-MOS-Platform/DFI/library/DiskCopyCommon/inc"
    CACHE PATH "Directory containing DiskCopyData_Vx.h and DiskCopyDataGlob.h")

if(NOT EXISTS "${DISKCOPY_INC}/DiskCopyDataGlob.h")
    message(FATAL_ERROR "DiskCopyDataGlob.h not found under DISKCOPY_INC='${DISKCOPY_INC}'. "
                        "Pass -DDISKCOPY_INC=<path-to-DiskCopyCommon/inc>.")
endif()

# ---------------------------------------------------------------------------
# Milestone 1: ABI size probe. No external dependencies (no DCMTK yet).
# Validates that the versioned structs compile and reports their byte sizes,
# which the receiver's raw memcpy parsing depends on.
# ---------------------------------------------------------------------------
add_executable(sizeprobe src/sizeprobe.cpp)
target_include_directories(sizeprobe PRIVATE "${DISKCOPY_INC}")

if(MSVC)
    target_compile_options(sizeprobe PRIVATE /W4 /permissive-)
endif()

# ---------------------------------------------------------------------------
# Core library: version model + per-version DiskCopy blob serializers.
# No DICOM dependency -- pure struct filling. Compileable/testable on its own.
# ---------------------------------------------------------------------------
add_library(dccore STATIC
    src/Version.cpp
    src/DiskCopySerializerV5.cpp)
target_include_directories(dccore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${DISKCOPY_INC}")
if(MSVC)
    target_compile_options(dccore PRIVATE /W4 /permissive-)
endif()

# Self-test: builds the run-info/image-info blobs and checks their sizes
# against the ABI probe values. No DCMTK required.
add_executable(coretest src/coretest.cpp)
target_link_libraries(coretest PRIVATE dccore)
target_include_directories(coretest PRIVATE "${DISKCOPY_INC}")

# ---------------------------------------------------------------------------
# Milestone 2: the conversion library + converter CLI. Requires DCMTK (vcpkg).
# dcconvertlib holds all conversion logic; dcconvert is a thin CLI wrapper.
# Only built when DCMTK is found so the project still configures without it.
# ---------------------------------------------------------------------------
find_package(DCMTK CONFIG QUIET)
if(DCMTK_FOUND)
    # -----------------------------------------------------------------------
    # dcconvertlib: the reusable conversion library. Link this into your own
    # application and call dfitodiskcopy::ConvertFile / ConvertBuffer
    # (see src/DfiToDiskCopy.h). Carries its DCMTK + dccore dependencies.
    # -----------------------------------------------------------------------
    add_library(dcconvertlib STATIC src/DfiToDiskCopy.cpp)
    target_include_directories(dcconvertlib
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE "${DISKCOPY_INC}")
    target_link_libraries(dcconvertlib PUBLIC dccore DCMTK::DCMTK)
    if(MSVC)
        target_compile_options(dcconvertlib PRIVATE /W4 /permissive-)
    endif()

    # The converter CLI is now a thin wrapper over dcconvertlib.
    add_executable(dcconvert src/main.cpp)
    target_link_libraries(dcconvert PRIVATE dcconvertlib)
    if(MSVC)
        target_compile_options(dcconvert PRIVATE /W4 /permissive-)
    endif()

    # Example app: demonstrates consuming dcconvertlib (ConvertFile +
    # ConvertBuffer) to perform the same job as the dcconvert CLI.
    add_executable(dcconvert_demo examples/dcconvert_demo.cpp)
    target_link_libraries(dcconvert_demo PRIVATE dcconvertlib)
    if(MSVC)
        target_compile_options(dcconvert_demo PRIVATE /W4 /permissive-)
    endif()
    message(STATUS "DCMTK found: building dcconvertlib library and dcconvert CLI.")

    # -----------------------------------------------------------------------
    # Install + export, so other projects can consume the library with
    #   find_package(DfiToDiskCopy CONFIG REQUIRED)
    #   target_link_libraries(myapp PRIVATE DfiToDiskCopy::dcconvertlib)
    # -----------------------------------------------------------------------
    include(CMakePackageConfigHelpers)

    install(TARGETS dcconvertlib dccore
        EXPORT DfiToDiskCopyTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # Public headers only (the DISKCOPY_INC headers stay private to the build).
    install(FILES
        src/DfiToDiskCopy.h
        src/Version.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT DfiToDiskCopyTargets
        FILE DfiToDiskCopyTargets.cmake
        NAMESPACE DfiToDiskCopy::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DfiToDiskCopy)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DfiToDiskCopyConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/DfiToDiskCopyConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DfiToDiskCopy)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/DfiToDiskCopyConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/DfiToDiskCopyConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/DfiToDiskCopyConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DfiToDiskCopy)
else()
    message(STATUS "DCMTK not found: skipping dcconvertlib/dcconvert. Install via vcpkg and re-run CMake.")
endif()
