// Milestone 1: ABI size probe.
// The DiskCopy import path memcpy's the private-tag blob directly into these
// structs, so their exact byte sizes (per software version) are the contract.
// This probe confirms the versioned headers compile standalone and prints the
// run-info blob size (patient+exam+run+image) and per-frame image-record size.

#include <cstddef>
#include <cstdio>
#include <ctime>   // some versions (e.g. V0) declare 'struct tm' fields; must be visible first

#include "DiskCopyData_V0.h"
#include "DiskCopyData_V1.h"
#include "DiskCopyData_V1_1.h"
#include "DiskCopyData_V2.h"
#include "DiskCopyData_V2_1.h"
#include "DiskCopyData_V3.h"
#include "DiskCopyData_V4.h"
#include "DiskCopyData_V5.h"

#define PROBE(LABEL, NS, SFX)                                                          \
    do {                                                                              \
        const size_t p = sizeof(NS::PATIENT_RECORD_STRUCT_DISKCOPY10##SFX);           \
        const size_t e = sizeof(NS::EXAM_RECORD_STRUCT_DISKCOPY10##SFX);              \
        const size_t r = sizeof(NS::RUN_RECORD_STRUCT##SFX);                          \
        const size_t i = sizeof(NS::IMAGE_RECORD_STRUCT##SFX);                        \
        std::printf("%-26s  P=%6zu  E=%6zu  R=%6zu  I=%6zu  | run-info=%7zu\n",       \
                    LABEL, p, e, r, i, p + e + r + i);                                \
    } while (0)

int main()
{
    std::printf("DiskCopy struct sizes (bytes) per software version\n");
    std::printf("run-info blob (3333,xx0A) = P+E+R+I ; image-info blob (9999,xx0A) = nFrames * I\n\n");

    PROBE("VER_0   DFI legacy",        DiskCopyDataVer0,   _V0);
    PROBE("VER_1   Zenition <=5.3.2",  DiskCopyDataVer01,  _V1);
    PROBE("VER_1_1 Zenition >=5.3.3",  DiskCopyDataVer1_1, _V1_1);
    PROBE("VER_2   Zenition <=6.1.2",  DiskCopyDataVer02,  _V2);
    PROBE("VER_2_1 Zenition >=6.1.3",  DiskCopyDataVer2_1, _V2_1);
    PROBE("VER_3   Zenition 7.x.x",    DiskCopyDataVer03,  _V3);
    PROBE("VER_4   Zenition 8.x.x",    DiskCopyDataVer04,  _V4);
    PROBE("VER_5   Zenition 9.x.x",    DiskCopyDataVer05,  _V5);

    return 0;
}
