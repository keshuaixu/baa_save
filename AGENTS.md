# AGENTS.md — `baa_save` (MATLAB MEX) ultra-fast `.npy` writer (Windows)

This project implements a **single MATLAB MEX function** named:

```matlab
baa_save(a, filename)
```

It writes a MATLAB array `a` to disk in **NumPy `.npy`** format **as fast as possible** with **minimal processing**.

Key performance idea: **dump MATLAB’s memory buffer directly** and write a `.npy` header with:

- `fortran_order: True` (because MATLAB is column-major)
- `shape: ...` matching MATLAB dimensions
- `descr: ...` matching the MATLAB element type

No complex support is required.

---

## Goals

1. **Maximum write throughput** on Windows (disk-bound).
2. **Minimal processing**:
   - No per-element loops for real arrays
   - No transpose / reshaping / conversion
   - Header creation only
3. **Compatibility**: `numpy.load("file.npy")` must load correctly.

---

## Non-goals

- Complex arrays: **not supported** (error out).
- Sparse arrays: **not supported** (error out).
- Non-numeric MATLAB types (cell/struct/char/string/table/etc.): **not supported**.
- Filename validation / extension enforcement: **not required**.
- MATLAB wrapper `.m` file: **not required** (MEX name is `baa_save`).

---

## Platform

- Windows (64-bit)
- MATLAB MEX (C++)

---

## Public API

### MATLAB

```matlab
baa_save(a, filename)
```

- `a`: MATLAB array (numeric or logical), **real**, non-sparse.
- `filename`: MATLAB `char` vector (UTF-16). (Support for `string` is optional; simplest is char only.)
- Side effects:
  - Creates parent directory if missing.
  - Creates/overwrites the output file.
- Returns: nothing.

---

## File format details (`.npy`)

Write standard NumPy `.npy`:

### Header dict fields (minimum)

- `descr`: NumPy dtype string, little-endian where applicable
- `fortran_order`: `True` (so NumPy interprets the raw bytes as column-major)
- `shape`: tuple of dimensions

Example:

```python
{'descr': '<f8', 'fortran_order': True, 'shape': (2000, 3000), }
```

### Version selection

- Prefer **NPY v1.0** (2-byte header length) if header length ≤ 65535.
- Otherwise use **NPY v2.0** (4-byte header length).

### Alignment

The header (including magic/version/length) must end such that the *start of data payload* is aligned to a 16-byte boundary. Achieve this by padding the header string with spaces and ending with `\n`.

---

## Data layout strategy (critical)

MATLAB arrays are **column-major**, and NumPy arrays are typically **row-major** unless told otherwise. We avoid any transpose/copy by:

- Writing the raw MATLAB buffer **as-is**.
- Setting `fortran_order: True` in the `.npy` header.

Result:
- Python `np.load` gets the correct values and shape.
- The resulting NumPy array will be Fortran-contiguous (often `A.flags['F_CONTIGUOUS'] == True`).

---

## Supported MATLAB types → NumPy `descr`

All writes are little-endian (Windows is little-endian).

| MATLAB class | NumPy dtype (`descr`) | bytes/elem |
|-------------|------------------------|-----------:|
| `double`    | `'<f8'`                | 8 |
| `single`    | `'<f4'`                | 4 |
| `int8`      | `'|i1'`                | 1 |
| `uint8`     | `'|u1'`                | 1 |
| `int16`     | `'<i2'`                | 2 |
| `uint16`    | `'<u2'`                | 2 |
| `int32`     | `'<i4'`                | 4 |
| `uint32`    | `'<u4'`                | 4 |
| `int64`     | `'<i8'`                | 8 |
| `uint64`    | `'<u8'`                | 8 |
| `logical`   | `'|b1'`                | 1 |

### Complex arrays
If `mxIsComplex(a)` is true, the function **must throw an error** (no support).

---

## Directory creation requirement

If `filename` contains a parent directory that does not exist, create it (recursively), e.g.:

- `C:\tmp\out\data.npy` should create `C:\tmp\out\` if missing.
- `.\out\data.npy` should create `.\out\`.

Implementation guidance (Windows):
- Convert MATLAB UTF-16 char filename to `std::wstring`.
- Extract parent directory (everything before the last `\` or `/`).
- Recursively create directories using `CreateDirectoryW`.
- Treat “already exists” as success.
- You may ignore edge cases like UNC and device paths unless you want to be robust.

---

## Implementation plan (C++ MEX)

### 1) MEX entry point

File: `baa_save.cpp` (compiled into `baa_save.mexw64`)

Expected call signature:
- `nrhs == 2`
- `prhs[0]` is the array
- `prhs[1]` is filename (char)

Minimal checks:
- Ensure `a` is numeric or logical
- Ensure not sparse
- Ensure not complex
- Ensure filename is a MATLAB char array

(“No need to check filename” means no extension enforcement or path validation. Still avoid crashes.)

### 2) Map dtype

- Use `mxGetClassID(a)` and map to `descr` + `itemSize`.
- Disallow complex.

### 3) Build `shape` tuple string

- Use `mxGetNumberOfDimensions(a)` and `mxGetDimensions(a)`.
- Construct Python tuple:
  - `(N,)` for 1D
  - `(d0, d1, ..., dk)` for N-D

### 4) Build header dict string

Create ASCII header dict:

```cpp
"{'descr': '<f8', 'fortran_order': True, 'shape': (d0, d1), }"
```

Then:
- Append `\n`
- Pad with spaces so that `prefixLen + headerLen` is a multiple of 16.

Where:
- `prefixLen = 10` for v1.0 (magic(6)+ver(2)+hlen(2))
- `prefixLen = 12` for v2.0 (magic(6)+ver(2)+hlen(4))

### 5) Write file (fast path)

Use Windows I/O:
- `CreateFileW(..., GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL)`
- `WriteFile` to output:
  1. Magic `\x93NUMPY`
  2. Version bytes
  3. Header length (little-endian)
  4. Header bytes
  5. Payload bytes (raw MATLAB data)

For payload:
- `void* p = mxGetData(a)`
- `numel = mxGetNumberOfElements(a)`
- `nBytes = numel * itemSize`
- Write in chunks (e.g. 64 MiB) to avoid large `DWORD` limits.

### 6) Close handle

Always close the file handle before returning or throwing.

---

## Error behavior

Use `mexErrMsgIdAndTxt` with stable IDs, e.g.:

- `baa_save:usage` for argument count
- `baa_save:type` for unsupported type/sparse/complex
- `baa_save:io` for file or directory creation failures

---

## Performance considerations

- Avoid any temporary buffers for real arrays.
- Avoid `fopen/fwrite` if you want top performance; prefer `CreateFileW/WriteFile`.
- Add `FILE_FLAG_SEQUENTIAL_SCAN` for better caching hints.
- Use large chunks for `WriteFile` (e.g. 64 MiB).
- The header is tiny; payload dominates.

---

## Build instructions (MATLAB on Windows)

From MATLAB, in the project directory:

```matlab
mex -O CXXFLAGS="$CXXFLAGS /std:c++17" baa_save.cpp
```

This creates `baa_save.mexw64` which MATLAB can call directly.

---

## Basic test plan

### MATLAB → Python equivalence (recommended)

1) In MATLAB:

```matlab
A = rand(2000, 3000, 'single');
baa_save(A, 'out\A.npy');
```

2) In Python:

```python
import numpy as np
A = np.load(r"out\A.npy")
# Validate shape and values (within float tolerance):
# assert A.shape == (2000, 3000)
# compare random samples or full array if feasible
```

### Multi-dimensional arrays

Test shapes:
- `(1, N)` row vector
- `(N, 1)` column vector
- `(2,3,4)` 3D array
- Empty arrays (e.g., `zeros(0,5,'double')`)

### Type coverage

Test each supported class to ensure dtype mapping is correct.

---

## Repository layout suggestion

```
.
├── baa_save.cpp        # MEX implementation (C++)
└── AGENTS.md           # this document
```

---

## “Done” checklist

- [ ] `baa_save.cpp` compiles to `baa_save.mexw64` on Windows.
- [ ] Writes valid `.npy` v1.0 or v2.0 depending header size.
- [ ] `fortran_order` is **True**.
- [ ] `shape` matches MATLAB dimensions exactly.
- [ ] Dtype `descr` is correct for all supported types.
- [ ] Parent directory is created if missing.
- [ ] Errors out on complex, sparse, and unsupported classes.
- [ ] Verified with `numpy.load` for correctness.
