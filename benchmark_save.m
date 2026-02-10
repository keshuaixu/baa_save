% benchmark_save.m
% Benchmarks write time for:
% 1) MATLAB save default format
% 2) MATLAB save -v7.3
% 3) baa_save (.npy)
%
% Array: exact 1 GiB int16, 2D (16384 x 32768).

rows = 16384;
cols = 32768;
numel_target = rows * cols;  %#ok<NASGU>
bytes_target = rows * cols * 2;

if exist("baa_save", "file") ~= 3
    error("benchmark:missingMex", "baa_save.mexw64 not found on MATLAB path.");
end

out_dir = fullfile(pwd, "bench_out");
if ~exist(out_dir, "dir")
    mkdir(out_dir);
end

cleanup_files = {};

fprintf("Generating int16 array: %d x %d (%.2f GiB payload) ...\n", ...
    rows, cols, bytes_target / 1024^3);
rng(42, "twister");
A = randi([-32768, 32767], rows, cols, "int16");
fprintf("Array generated.\n");

file_default = fullfile(out_dir, "save_default.mat");
file_v73 = fullfile(out_dir, "save_v73.mat");
file_npy = fullfile(out_dir, "baa_save.npy");
file_csv = fullfile(out_dir, "benchmark_results.csv");
file_mat = fullfile(out_dir, "benchmark_results.mat");
cleanup_files = {file_default, file_v73, file_npy, file_csv, file_mat};

if exist(file_default, "file"), delete(file_default); end
if exist(file_v73, "file"), delete(file_v73); end
if exist(file_npy, "file"), delete(file_npy); end
if exist(file_csv, "file"), delete(file_csv); end
if exist(file_mat, "file"), delete(file_mat); end

fprintf("Running benchmark writes...\n");

t0 = tic;
save(file_default, "A");
t_default = toc(t0);
pause(10);

t0 = tic;
save(file_v73, "A", "-v7.3");
t_v73 = toc(t0);
pause(10);

t0 = tic;
baa_save(A, char(file_npy));
t_baa = toc(t0);

info_default = dir(file_default);
info_v73 = dir(file_v73);
info_npy = dir(file_npy);

method = ["save_default"; "save_v7_3"; "baa_save_npy"];
seconds = [t_default; t_v73; t_baa];
bytes = [info_default.bytes; info_v73.bytes; info_npy.bytes];
mb_per_sec = (bytes / 1e6) ./ seconds;

results = table(method, seconds, bytes, mb_per_sec);
disp(results);

for i = 1:numel(cleanup_files)
    if exist(cleanup_files{i}, "file")
        delete(cleanup_files{i});
    end
end
fprintf("Generated benchmark files deleted.\n");
