# baa_save 🐑

Ultra-fast MATLAB writer for NumPy `.npy` files on Windows. Faster than `save`.

## Installation (Windows)

Latest release toolbox package:

- `https://github.com/keshuaixu/baa_save/releases/latest/download/baa_save_win64.mltbx`

1. Download `baa_save_win64.mltbx` from the link above.
2. Double-click the `.mltbx` file, then click **Install** in MATLAB.

Verify installation:

```matlab
which baa_save
```

## Benchmark (1 GiB `int16`, 2D)

Benchmark script: `benchmark_save.m`

Run from MATLAB:

```matlab
benchmark_save
```

Array used:

- Shape: `16384 x 32768` (2D)
- Type: `int16`
- Payload size: `1,073,741,824` bytes (`1.00 GiB`)

Measured on a shitty machine with SATA SSD (single run, MATLAB `R2025b Update 3`, with `pause(10)` between writes):

| Method | Time (s) | Write speed (MB/s) |
|---|---:|---:|
| `save(file, "A")` | 13.8440 | 77.654 |
| `save(file, "A", "-v7.3")` | 20.1230 | 53.418 |
| `baa_save(A, file)` | 3.4034 | 315.49 |


## API

```matlab
baa_save(a, filename)
```

- `a`: real, non-sparse numeric or logical MATLAB array.
- `filename`: MATLAB `char` vector (UTF-16 path).
- No return value.

The implementation writes MATLAB memory directly and sets `fortran_order: True`, so no transpose/copy is needed.

## Features

- Direct raw payload write from `mxGetData(a)`.
- NumPy header with:
  - `descr` mapped from MATLAB type.
  - `fortran_order: True`.
  - `shape` from MATLAB dimensions.
- Automatic parent directory creation (recursive, Windows).
- Uses NPY v1.0 when possible, falls back to NPY v2.0 for large headers.
- Chunked `WriteFile` writes (64 MiB chunks) with Windows file APIs.

## Supported MATLAB classes

| MATLAB class | NumPy `descr` |
|---|---|
| `double` | `'<f8'` |
| `single` | `'<f4'` |
| `int8` | `'|i1'` |
| `uint8` | `'|u1'` |
| `int16` | `'<i2'` |
| `uint16` | `'<u2'` |
| `int32` | `'<i4'` |
| `uint32` | `'<u4'` |
| `int64` | `'<i8'` |
| `uint64` | `'<u8'` |
| `logical` | `'|b1'` |

## Not supported

- Complex arrays (`mxIsComplex(a)`).
- Sparse arrays.
- Non-numeric/non-logical arrays.
- MATLAB `string` input for filename (only `char` vector is accepted).

## Build (MATLAB, Windows)

From the repository root in MATLAB:

```matlab
mex -O CXXFLAGS="$CXXFLAGS /std:c++17" baa_save.cpp
```

This generates `baa_save.mexw64`.

## Package release

Create an end-user MATLAB toolbox package (`.mltbx`).

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\package_release.ps1
```

Default output:

- `release\baa_save_win64.mltbx`

Optional flags:

- `-PackageName <name>`
- `-OutputRoot <folder>`
- `-ToolboxVersion <version>`
- `-ToolboxIdentifier <identifier>` (alphanumeric with optional `-`)
- `-NoSource` (package only `baa_save.mexw64` + `README.md`)

The package script includes only release files (`baa_save.mexw64`, `README.md`, and optionally `baa_save.cpp`). Test and benchmark scripts are not packaged.

## Quick test

In MATLAB:

```matlab
A = rand(2000, 3000, 'single');
baa_save(A, 'out\A.npy');
```

In Python:

```python
import numpy as np
A = np.load(r"out\A.npy")
print(A.shape, A.dtype, A.flags["F_CONTIGUOUS"])
```

## Round-trip test

Test script: `test_roundtrip.m`

Run from MATLAB:

```matlab
test_roundtrip
```

This writes `.npy` files and validates readback with `numpy.load` for:

- 1D vector case (`1xN` in MATLAB)
- 2D array
- 3D array
- `int16` array
- `double` array


## Error IDs

- `baa_save:usage` for bad call signature.
- `baa_save:type` for unsupported input type/format.
- `baa_save:io` for path and file I/O failures.
