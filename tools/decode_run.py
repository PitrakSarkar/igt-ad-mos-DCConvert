import sys, struct
import pydicom

path = sys.argv[1]
ds = pydicom.dcmread(path, stop_before_pixels=True, force=True)
blob = bytes(ds[0x3333, 0x100A].value)
print("3333 blob len:", len(blob))

PAT = 336
EXAM = 464
RUN = 1352
run = blob[PAT+EXAM : PAT+EXAM+RUN]
print("run len:", len(run))

# Scan for cbeiSubtract signature: contrast=-16, brightness=-9 as consecutive int32 LE
sig = struct.pack('<ii', -16, -9)
idx = run.find(sig)
print("cbeiSubtract(-16,-9) found at run offset:", idx)

# Scan for irisAperture == 141 (square) or 100 (round) int32
for val in (141, 100):
    off = 0
    hits = []
    packed = struct.pack('<i', val)
    while True:
        j = run.find(packed, off)
        if j < 0:
            break
        hits.append(j)
        off = j + 1
    print(f"value {val} int32 at run offsets:", hits)

# maskImageNr == 1 occurrences
hits = []
off = 0
packed = struct.pack('<i', 1)
while True:
    j = run.find(packed, off)
    if j < 0:
        break
    hits.append(j)
    off = j + 1
print("value 1 int32 at run offsets:", hits)

# Show first 64 bytes hex and a summary of nonzero density
nz = sum(1 for b in run if b != 0)
print("run nonzero bytes:", nz, "/", len(run))
