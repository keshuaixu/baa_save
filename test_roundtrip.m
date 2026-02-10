% test_roundtrip.m
% Saves representative arrays with baa_save and verifies readback with numpy.
%
% Covered cases:
% - 1d array (MATLAB vector represented as 1xN)
% - 2d array
% - 3d array
% - int16 array
% - double array

if exist("baa_save", "file") ~= 3
    error("test_roundtrip:missingMex", "baa_save.mexw64 not found on MATLAB path.");
end

out_dir = fullfile(pwd, "test_out");
if ~exist(out_dir, "dir")
    mkdir(out_dir);
end

fprintf("Writing test arrays to %s\n", out_dir);

% 1D vector case (MATLAB stores vectors as 2D with one singleton dimension).
A1 = reshape(0:15, 1, 16);
baa_save(A1, char(fullfile(out_dir, "case_1d_double.npy")));

% 2D double.
A2 = reshape(0:23, 4, 6);
baa_save(A2, char(fullfile(out_dir, "case_2d_double.npy")));

% 3D double.
A3 = reshape(0:23, 2, 3, 4);
baa_save(A3, char(fullfile(out_dir, "case_3d_double.npy")));

% int16 array.
A4 = reshape(int16(-150:149), 20, 15);
baa_save(A4, char(fullfile(out_dir, "case_int16.npy")));

% explicit double array.
A5 = reshape((0:29) ./ 7 + 0.125, 5, 6);
baa_save(A5, char(fullfile(out_dir, "case_double.npy")));

py_script = fullfile(pwd, "verify_roundtrip.py");
cmd = sprintf('python "%s" "%s"', py_script, out_dir);
[status, output] = system(cmd);
fprintf("%s", output);

if status ~= 0
    error("test_roundtrip:pythonValidationFailed", ...
        "Python round-trip validation failed with status %d.", status);
end

fprintf("Round-trip test passed.\n");
