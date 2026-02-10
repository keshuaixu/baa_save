from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np


def check_case(path: Path, expected: np.ndarray, expected_dtype: np.dtype, name: str) -> None:
    arr = np.load(path)

    if arr.shape != expected.shape:
        raise AssertionError(f"{name}: shape mismatch, got {arr.shape}, expected {expected.shape}")
    if arr.dtype != expected_dtype:
        raise AssertionError(f"{name}: dtype mismatch, got {arr.dtype}, expected {expected_dtype}")
    if not arr.flags["F_CONTIGUOUS"]:
        raise AssertionError(f"{name}: expected Fortran-contiguous array")

    if np.issubdtype(arr.dtype, np.floating):
        if not np.allclose(arr, expected, rtol=1e-12, atol=0.0):
            raise AssertionError(f"{name}: value mismatch (floating)")
    else:
        if not np.array_equal(arr, expected):
            raise AssertionError(f"{name}: value mismatch (exact)")

    print(f"[OK] {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify baa_save round-trip test files.")
    parser.add_argument("out_dir", type=Path, help="Directory containing test .npy files")
    args = parser.parse_args()

    out_dir = args.out_dir.resolve()

    cases = [
        (
            "case_1d_double.npy",
            np.arange(16, dtype=np.float64).reshape((1, 16), order="F"),
            np.dtype("<f8"),
            "1d_vector_double",
        ),
        (
            "case_2d_double.npy",
            np.arange(24, dtype=np.float64).reshape((4, 6), order="F"),
            np.dtype("<f8"),
            "2d_double",
        ),
        (
            "case_3d_double.npy",
            np.arange(24, dtype=np.float64).reshape((2, 3, 4), order="F"),
            np.dtype("<f8"),
            "3d_double",
        ),
        (
            "case_int16.npy",
            np.arange(-150, 150, dtype=np.int16).reshape((20, 15), order="F"),
            np.dtype("<i2"),
            "int16_array",
        ),
        (
            "case_double.npy",
            (np.arange(30, dtype=np.float64) / 7.0 + 0.125).reshape((5, 6), order="F"),
            np.dtype("<f8"),
            "double_array",
        ),
    ]

    for filename, expected, dtype, name in cases:
        path = out_dir / filename
        if not path.exists():
            raise FileNotFoundError(f"missing test file: {path}")
        check_case(path, expected, dtype, name)

    print("All round-trip checks passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover
        print(f"[FAIL] {exc}")
        raise SystemExit(1)
