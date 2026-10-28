#include "DiskCopySerializerV5.h"

#include <cstring>

// Versioned private-record layouts; VER_3 matches the native Zen90 receiver.
// Self-contained: only needs <Windows.h> (pulled in by DiskCopyDataGlob.h).
#include "DiskCopyData_V5.h"
#include "DiskCopyData_V3.h"

namespace dfitodiskcopy
{
    namespace
    {
        using DiskCopyDataVer05::PATIENT_RECORD_STRUCT_DISKCOPY10_V5;
        using DiskCopyDataVer05::EXAM_RECORD_STRUCT_DISKCOPY10_V5;
        using DiskCopyDataVer05::RUN_RECORD_STRUCT_V5;
        using DiskCopyDataVer05::IMAGE_RECORD_STRUCT_V5;

        static_assert(sizeof(DiskCopyDataVer03::PATIENT_RECORD_STRUCT_DISKCOPY10_V3) +
                      sizeof(DiskCopyDataVer03::EXAM_RECORD_STRUCT_DISKCOPY10_V3) +
                      sizeof(DiskCopyDataVer03::RUN_RECORD_STRUCT_V3) +
                      sizeof(DiskCopyDataVer03::IMAGE_RECORD_STRUCT_V3) == kRunInfoV3Size,
                      "Native Zen90 private-record ABI changed");
        static_assert(sizeof(PATIENT_RECORD_STRUCT_DISKCOPY10_V5) + sizeof(EXAM_RECORD_STRUCT_DISKCOPY10_V5) +
                      sizeof(RUN_RECORD_STRUCT_V5) + sizeof(IMAGE_RECORD_STRUCT_V5) == kRunInfoV5Size,
                      "VER_5 private-record ABI changed");

        void SetImageDimensions(DiskCopyDataVer03::IMAGE_RECORD_STRUCT_V3&, const SerializerInput&)
        {
        }

        void SetImageDimensions(IMAGE_RECORD_STRUCT_V5& image, const SerializerInput& in)
        {
            image.width = in.columns;
            image.height = in.rows;
        }

        void SetRunExtras(DiskCopyDataVer03::RUN_RECORD_STRUCT_V3& run, const SerializerInput&)
        {
            run.cleanCircleRadius.centre.xPos = 500;
            run.cleanCircleRadius.centre.yPos = 500;
            run.cleanCircleRadius.radius = 500;
        }

        void SetRunExtras(RUN_RECORD_STRUCT_V5& run, const SerializerInput&)
        {
            run.zoomFactor = 100;
        }

        template <typename TargetRun, typename SourceRun>
        void CopyRecipes(TargetRun& target, const SourceRun& source)
        {
            static_assert(sizeof(target.cbeiNormal) == sizeof(source.cbeiNormal), "CBEI layout mismatch");
            std::memcpy(&target.acqCbeiNormal, &source.acqCbeiNormal, sizeof(target.acqCbeiNormal));
            std::memcpy(&target.cbeiNormal, &source.cbeiNormal, sizeof(target.cbeiNormal));
            std::memcpy(&target.acqCbeiSubtract, &source.acqCbeiSubtract, sizeof(target.acqCbeiSubtract));
            std::memcpy(&target.cbeiSubtract, &source.cbeiSubtract, sizeof(target.cbeiSubtract));
            std::memcpy(target.ContrastTransferLMTaste, source.ContrastTransferLMTaste,
                        sizeof(target.ContrastTransferLMTaste));
            std::memcpy(target.apsTaste, source.apsTaste, sizeof(target.apsTaste));
        }

        template <typename T>
        void AppendStruct(std::vector<std::uint8_t>& out, const T& s)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(&s);
            out.insert(out.end(), p, p + sizeof(T));
        }

        // Fill one IMAGE record. Standard tags carry patient/exam identity;
        // this only sets the internal fields StoreImage needs per image.
        template <typename Image>
        Image MakeImageRecord(const SerializerInput& in, int imageNr)
        {
            Image img;
            std::memset(&img, 0, sizeof(img));
            img.imageNr  = imageNr;
            // Values verified against a genuine FD17 run.img export (kept=FALSE,
            // iiFormat=FD_10_INCH(8), iiRadius=301, width=height=1000). These are the
            // FINAL persisted records (dcm_diskcopy.c reorder loop writes our 9999-blob
            // array verbatim, forcing kept=FALSE anyway). (detectorType,iiFormat,iiRadius)
            // must form the supported FD17 triple or CoGetDetectorZoomFactor goes
            // degenerate -> black image + fault on re-view.
            img.kept     = FALSE;
            img.iiFormat = static_cast<decltype(img.iiFormat)>(in.iiFormat);
            img.iiRadius = in.iiRadius;
            // width/height are the real pixel dimensions and, for a genuine DiskCopy
            // media object, equal the 1000x1000 stored-image size (resize input with
            // --max-dim 1000). The SCP receiver copies width*height*2 pixel bytes.
            SetImageDimensions(img, in);
            return img;
        }
    }

    template <typename Patient, typename Exam, typename Run, typename Image>
    std::vector<std::uint8_t> BuildRunInfoBlob(const SerializerInput& in)
    {
        Patient patient;
        Exam exam;
        Run run;
        std::memset(&patient, 0, sizeof(patient));
        std::memset(&exam,    0, sizeof(exam));
        std::memset(&run,     0, sizeof(run));

        patient.nrExam = 1;

        exam.nrImage = in.numberOfFrames;
        exam.nrRun   = 1;

        run.nrImage    = in.numberOfFrames;
        run.runNr      = in.runNumber;
        run.iiFormat   = static_cast<decltype(run.iiFormat)>(0);
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
        copyTaste(run.cbeiNormal.cttTasteFile,  sizeof(run.cbeiNormal.cttTasteFile),  "CTT_FM_00.params.ini");
        copyTaste(run.cbeiNormal.edgkTasteFile, sizeof(run.cbeiNormal.edgkTasteFile), "EDGK_00.params.ini");

        run.cbeiSubtract.contrast      = 0;
        run.cbeiSubtract.brightness    = 0;
        run.cbeiSubtract.invert        = FALSE;
        run.cbeiSubtract.edge          = DiskCopyGlobal::EDGE_LEVEL_ENUM::LVL_0;
        run.cbeiSubtract.harmonization = 0;
        copyTaste(run.cbeiSubtract.snrTasteFile,  sizeof(run.cbeiSubtract.snrTasteFile),  "SNRT_00.params.ini");
        copyTaste(run.cbeiSubtract.tnrTasteFile,  sizeof(run.cbeiSubtract.tnrTasteFile),  "TNRT_00.params.ini");
        copyTaste(run.cbeiSubtract.cttTasteFile,  sizeof(run.cbeiSubtract.cttTasteFile),  "CTT_SM_00.params.ini");
        copyTaste(run.cbeiSubtract.edgkTasteFile, sizeof(run.cbeiSubtract.edgkTasteFile), "EDGK_00.params.ini");

        // Acquisition-time CBEI == current settings for a fresh run (genuine converter
        // carries both acqCbei* and cbei*; a real acquisition starts with them equal).
        run.acqCbeiNormal   = run.cbeiNormal;
        run.acqCbeiSubtract = run.cbeiSubtract;

        copyTaste(run.ContrastTransferLMTaste, sizeof(run.ContrastTransferLMTaste), "LMCTT_00.params.ini");
        copyTaste(run.apsTaste,                sizeof(run.apsTaste),                "APS_00.params.ini");

        // If the source object already carried a (3333,100A) run-info blob, adopt its
        // CBEI image-processing recipes verbatim (SNR/TNR/CTT/EDGK tastes + contrast/
        // brightness/edge and the LM/APS tastes). These are pixel-geometry independent,
        // so they stay valid after resize; everything else above (geometry, dims,
        // detector) is always freshly built so --max-dim/--detector still apply.
        if (in.sourceRunInfoBlob != nullptr && in.sourceRunInfoBlobSize == kRunInfoV5Size)
        {
            RUN_RECORD_STRUCT_V5 src;
            std::memcpy(&src, in.sourceRunInfoBlob + sizeof(patient) + sizeof(exam), sizeof(src));
            CopyRecipes(run, src);
        }
        else if (in.sourceRunInfoBlob != nullptr && in.sourceRunInfoBlobSize == kRunInfoV3Size)
        {
            DiskCopyDataVer03::RUN_RECORD_STRUCT_V3 src;
            std::memcpy(&src, in.sourceRunInfoBlob + sizeof(patient) + sizeof(exam), sizeof(src));
            CopyRecipes(run, src);
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
        SetRunExtras(run, in);

        const Image image = MakeImageRecord<Image>(in, in.imageNumberBase);

        std::vector<std::uint8_t> out;
        out.reserve(sizeof(patient) + sizeof(exam) + sizeof(run) + sizeof(image));
        AppendStruct(out, patient);
        AppendStruct(out, exam);
        AppendStruct(out, run);
        AppendStruct(out, image);
        return out;
    }

    template <typename Image>
    std::vector<std::uint8_t> BuildImageInfoBlob(const SerializerInput& in)
    {
        std::vector<std::uint8_t> out;
        out.reserve(sizeof(Image) * static_cast<std::size_t>(in.numberOfFrames));
        for (int i = 0; i < in.numberOfFrames; ++i)
        {
            const Image img = MakeImageRecord<Image>(in, in.imageNumberBase + i);
            AppendStruct(out, img);
        }
        return out;
    }

    std::vector<std::uint8_t> BuildRunInfoBlobV3(const SerializerInput& in)
    {
        return BuildRunInfoBlob<DiskCopyDataVer03::PATIENT_RECORD_STRUCT_DISKCOPY10_V3,
                                DiskCopyDataVer03::EXAM_RECORD_STRUCT_DISKCOPY10_V3,
                                DiskCopyDataVer03::RUN_RECORD_STRUCT_V3,
                                DiskCopyDataVer03::IMAGE_RECORD_STRUCT_V3>(in);
    }

    std::vector<std::uint8_t> BuildImageInfoBlobV3(const SerializerInput& in)
    {
        return BuildImageInfoBlob<DiskCopyDataVer03::IMAGE_RECORD_STRUCT_V3>(in);
    }

    std::vector<std::uint8_t> BuildRunInfoBlobV5(const SerializerInput& in)
    {
        return BuildRunInfoBlob<PATIENT_RECORD_STRUCT_DISKCOPY10_V5,
                                EXAM_RECORD_STRUCT_DISKCOPY10_V5,
                                RUN_RECORD_STRUCT_V5, IMAGE_RECORD_STRUCT_V5>(in);
    }

    std::vector<std::uint8_t> BuildImageInfoBlobV5(const SerializerInput& in)
    {
        return BuildImageInfoBlob<IMAGE_RECORD_STRUCT_V5>(in);
    }
}
