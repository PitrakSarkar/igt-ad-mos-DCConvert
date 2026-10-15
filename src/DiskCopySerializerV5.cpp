#include "DiskCopySerializerV5.h"

#include <cstring>

// The versioned DiskCopy record layout for Zenition 9.x (VER_5).
// Self-contained: only needs <Windows.h> (pulled in by DiskCopyDataGlob.h).
#include "DiskCopyData_V5.h"

namespace dfitodiskcopy
{
    namespace
    {
        using DiskCopyDataVer05::PATIENT_RECORD_STRUCT_DISKCOPY10_V5;
        using DiskCopyDataVer05::EXAM_RECORD_STRUCT_DISKCOPY10_V5;
        using DiskCopyDataVer05::RUN_RECORD_STRUCT_V5;
        using DiskCopyDataVer05::IMAGE_RECORD_STRUCT_V5;

        template <typename T>
        void AppendStruct(std::vector<std::uint8_t>& out, const T& s)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(&s);
            out.insert(out.end(), p, p + sizeof(T));
        }

        // Fill one IMAGE record. Standard tags carry patient/exam identity;
        // this only sets the internal fields StoreImage needs per image.
        IMAGE_RECORD_STRUCT_V5 MakeImageRecord(const SerializerInput& in, int imageNr)
        {
            IMAGE_RECORD_STRUCT_V5 img;
            std::memset(&img, 0, sizeof(img));
            img.imageNr  = imageNr;
            // Values verified against a genuine FD17 run.img export (kept=FALSE,
            // iiFormat=FD_10_INCH(8), iiRadius=301, width=height=1000). These are the
            // FINAL persisted records (dcm_diskcopy.c reorder loop writes our 9999-blob
            // array verbatim, forcing kept=FALSE anyway). (detectorType,iiFormat,iiRadius)
            // must form the supported FD17 triple or CoGetDetectorZoomFactor goes
            // degenerate -> black image + fault on re-view.
            img.kept     = FALSE;
            img.iiFormat = static_cast<DiskCopyDataVer05::II_FMT_ENUM_V5>(in.iiFormat);
            img.iiRadius = in.iiRadius;
            // width/height are the real pixel dimensions and, for a genuine DiskCopy
            // media object, equal the 1000x1000 stored-image size (resize input with
            // --max-dim 1000). The SCP receiver copies width*height*2 pixel bytes.
            img.width    = in.columns;
            img.height   = in.rows;
            img.snapshotImgWidth  = 0;
            img.snapshotImgHeight = 0;
            return img;
        }
    }

    std::vector<std::uint8_t> BuildRunInfoBlobV5(const SerializerInput& in)
    {
        PATIENT_RECORD_STRUCT_DISKCOPY10_V5 patient;
        EXAM_RECORD_STRUCT_DISKCOPY10_V5    exam;
        RUN_RECORD_STRUCT_V5                run;
        std::memset(&patient, 0, sizeof(patient));
        std::memset(&exam,    0, sizeof(exam));
        std::memset(&run,     0, sizeof(run));

        patient.nrExam = 1;

        exam.nrImage = in.numberOfFrames;
        exam.nrRun   = 1;

        run.nrImage    = in.numberOfFrames;
        run.runNr      = in.runNumber;
        run.iiFormat   = DiskCopyDataVer05::II_FMT_ENUM_V5::II_UNDEFINED;
        run.squareImage = (in.rows == in.columns) ? TRUE : FALSE;

        // Mirror the DFI receiver's fillStructsWithDefaults() RUN display defaults.
        // Our private (3333) blob is copied byte-for-byte over the receiver's runInfo,
        // so leaving these zero closes the iris (black image) and feeds invalid enums
        // to the viewer (crash on re-render). MAX_APERTURE_* come from swglobal/global.h.
        // Numeric/taste values below are matched to a genuine FD17 run.img export so the
        // DFI image processor (IMPR) has a valid processing chain -- empty CBEI taste
        // recipes leave no contrast-transfer/edge kernel and render an all-black image.
        const int kMaxApertureRound  = 100;
        const int kMaxApertureSquare = 141;

        // Copy a taste-recipe filename into a fixed 31-byte field (struct is zeroed).
        auto copyTaste = [](char* dst, std::size_t cap, const char* name)
        {
            const std::size_t n = std::strlen(name);
            const std::size_t k = (n < cap - 1) ? n : cap - 1;
            std::memcpy(dst, name, k);
            dst[k] = '\0';
        };

        run.cleanCircleRadius.centre.xPos = 960;
        run.cleanCircleRadius.centre.yPos = 960;
        run.cleanCircleRadius.radius      = 960;
        run.acqSpeed                      = 3000;

        run.cbeiNormal.contrast      = 0;
        run.cbeiNormal.brightness    = 5;
        run.cbeiNormal.invert        = FALSE;
        run.cbeiNormal.edge          = DiskCopyGlobal::EDGE_LEVEL_ENUM::LVL_2;
        run.cbeiNormal.harmonization = 0;
        copyTaste(run.cbeiNormal.snrTasteFile,  sizeof(run.cbeiNormal.snrTasteFile),  "SNRT_00.params.ini");
        copyTaste(run.cbeiNormal.tnrTasteFile,  sizeof(run.cbeiNormal.tnrTasteFile),  "TNRT_00.params.ini");
        copyTaste(run.cbeiNormal.cttTasteFile,  sizeof(run.cbeiNormal.cttTasteFile),  "CTT_FM_04.params.ini");
        copyTaste(run.cbeiNormal.edgkTasteFile, sizeof(run.cbeiNormal.edgkTasteFile), "EDGK_00.params.ini");

        run.cbeiSubtract.contrast      = 0;
        run.cbeiSubtract.brightness    = 0;
        run.cbeiSubtract.invert        = FALSE;
        run.cbeiSubtract.edge          = DiskCopyGlobal::EDGE_LEVEL_ENUM::LVL_0;
        run.cbeiSubtract.harmonization = 0;
        copyTaste(run.cbeiSubtract.snrTasteFile,  sizeof(run.cbeiSubtract.snrTasteFile),  "SNRT_00.params.ini");
        copyTaste(run.cbeiSubtract.tnrTasteFile,  sizeof(run.cbeiSubtract.tnrTasteFile),  "TNRT_00.params.ini");
        copyTaste(run.cbeiSubtract.cttTasteFile,  sizeof(run.cbeiSubtract.cttTasteFile),  "CTT_SM_01.params.ini");
        copyTaste(run.cbeiSubtract.edgkTasteFile, sizeof(run.cbeiSubtract.edgkTasteFile), "EDGK_00.params.ini");

        // Acquisition-time CBEI == current settings for a fresh run (genuine converter
        // carries both acqCbei* and cbei*; a real acquisition starts with them equal).
        run.acqCbeiNormal   = run.cbeiNormal;
        run.acqCbeiSubtract = run.cbeiSubtract;

        copyTaste(run.ContrastTransferLMTaste, sizeof(run.ContrastTransferLMTaste), "LMCTT_01.params.ini");
        copyTaste(run.apsTaste,                sizeof(run.apsTaste),                "APS_00.params.ini");

        // If the source object already carried a (3333,100A) run-info blob, adopt its
        // CBEI image-processing recipes verbatim (SNR/TNR/CTT/EDGK tastes + contrast/
        // brightness/edge and the LM/APS tastes). These are pixel-geometry independent,
        // so they stay valid after resize; everything else above (geometry, dims,
        // detector) is always freshly built so --max-dim/--detector still apply.
        if (in.sourceRunInfoBlob != nullptr &&
            in.sourceRunInfoBlobSize >= sizeof(patient) + sizeof(exam) + sizeof(run))
        {
            RUN_RECORD_STRUCT_V5 src;
            std::memcpy(&src, in.sourceRunInfoBlob + sizeof(patient) + sizeof(exam), sizeof(src));
            run.acqCbeiNormal   = src.acqCbeiNormal;
            run.cbeiNormal      = src.cbeiNormal;
            run.acqCbeiSubtract = src.acqCbeiSubtract;
            run.cbeiSubtract    = src.cbeiSubtract;
            std::memcpy(run.ContrastTransferLMTaste, src.ContrastTransferLMTaste,
                        sizeof(run.ContrastTransferLMTaste));
            std::memcpy(run.apsTaste, src.apsTaste, sizeof(run.apsTaste));
        }

        run.shuttered = FALSE;
        for (int s = 0; s < MAX_NR_OF_SHUTTERS; ++s)
        {
            run.shutter[s].aperture = kMaxApertureRound;
            run.shutter[s].angle    = 90;
            run.shutter[s].lineType = DiskCopyGlobal::LINE_TYPE_ENUM::LT_INVISIBLE;
        }
        run.irisAperture = run.squareImage ? kMaxApertureSquare : kMaxApertureRound;

        run.calibrated    = FALSE;
        run.subtracted    = FALSE;
        run.maskImageNr   = 1;
        run.catheterRun   = FALSE;
        run.landMarkLevel = DiskCopyGlobal::LEVEL_ENUM::LVL_OFF;
        run.magnified     = FALSE;
        run.panZoom       = FALSE;
        run.zoomFactor    = 100;   // genuine Version05FBConverter sets 100 (no zoom)
        run.run3D         = FALSE;
        run.imgFlipped    = FALSE;
        run.imgMirrored   = FALSE;

        const IMAGE_RECORD_STRUCT_V5 image = MakeImageRecord(in, in.imageNumberBase);

        std::vector<std::uint8_t> out;
        out.reserve(sizeof(patient) + sizeof(exam) + sizeof(run) + sizeof(image));
        AppendStruct(out, patient);
        AppendStruct(out, exam);
        AppendStruct(out, run);
        AppendStruct(out, image);
        return out;
    }

    std::vector<std::uint8_t> BuildImageInfoBlobV5(const SerializerInput& in)
    {
        std::vector<std::uint8_t> out;
        out.reserve(sizeof(IMAGE_RECORD_STRUCT_V5) * static_cast<std::size_t>(in.numberOfFrames));
        for (int i = 0; i < in.numberOfFrames; ++i)
        {
            const IMAGE_RECORD_STRUCT_V5 img = MakeImageRecord(in, in.imageNumberBase + i);
            AppendStruct(out, img);
        }
        return out;
    }
}
