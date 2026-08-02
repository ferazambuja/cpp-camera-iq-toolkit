function export_spectro_crosscheck(archive_root, ledger_csv, output_csv)
%EXPORT_SPECTRO_CROSSCHECK Read the declared spectroradiometer measurements.
%
% The output matches the parser-independent columns emitted by:
%   camera_iq spectro-ingest ... --readings-csv cpp_readings.csv
%
% Compare the two files with:
%   python3 tools/compare_spectro_crosscheck.py cpp_readings.csv matlab_readings.csv
%
% Vector identity uses SHA-256 over explicitly little-endian IEEE-754 binary64
% bytes. Numeric metadata is also exported for tolerance-based comparison.

arguments
    archive_root (1,1) string
    ledger_csv (1,1) string
    output_csv (1,1) string
end

ledger = readtable(ledger_csv, TextType="string", VariableNamingRule="preserve");
required = ["group_id", "repeat_index", "canonical_path"];
if ~all(ismember(required, string(ledger.Properties.VariableNames)))
    error("spectro:ledgerSchema", "Ledger is missing required columns.");
end

n = height(ledger);
group_id = strings(n, 1);
measurement_index = zeros(n, 1);
canonical_path = strings(n, 1);
wavelength_binary64_le_sha256 = strings(n, 1);
radiance_binary64_le_sha256 = strings(n, 1);
spectral_integral = zeros(n, 1);
recorded_x = zeros(n, 1);
recorded_y = zeros(n, 1);
recorded_z = zeros(n, 1);
recorded_total_radiance = zeros(n, 1);
recorded_cct_k = zeros(n, 1);
recorded_duv = zeros(n, 1);

for row = 1:n
    relative = ledger.canonical_path(row);
    source = fullfile(archive_root, relative);
    loaded = load(source, "measurements");
    if ~isfield(loaded, "measurements")
        error("spectro:missingMeasurements", ...
              "No measurements struct in %s", relative);
    end
    measurement = loaded.measurements;
    wavelength = double(measurement.wl(:));
    radiance = double(measurement.radiance(:));
    if numel(wavelength) < 2 || numel(wavelength) ~= numel(radiance)
        error("spectro:vectorShape", "Invalid vectors in %s", relative);
    end
    steps = diff(wavelength);
    if any(~isfinite(steps)) || any(abs(steps - steps(1)) > 1e-9)
        error("spectro:wavelengthGrid", "Nonuniform grid in %s", relative);
    end
    xyz = double(measurement.XYZ(:));
    if numel(xyz) ~= 3
        error("spectro:xyzShape", "XYZ is not length three in %s", relative);
    end

    group_id(row) = ledger.group_id(row);
    measurement_index(row) = ledger.repeat_index(row);
    canonical_path(row) = relative;
    wavelength_binary64_le_sha256(row) = binary64_le_sha256(wavelength);
    radiance_binary64_le_sha256(row) = binary64_le_sha256(radiance);
    spectral_integral(row) = steps(1) * sum(radiance);
    recorded_x(row) = xyz(1);
    recorded_y(row) = xyz(2);
    recorded_z(row) = xyz(3);
    recorded_total_radiance(row) = double(measurement.totalRadiance);
    recorded_cct_k(row) = double(measurement.CCT);
    recorded_duv(row) = double(measurement.Duv);
end

output = table(group_id, measurement_index, canonical_path, ...
    wavelength_binary64_le_sha256, radiance_binary64_le_sha256, ...
    spectral_integral, recorded_x, recorded_y, recorded_z, ...
    recorded_total_radiance, recorded_cct_k, recorded_duv);
writetable(output, output_csv);
end

function digest = binary64_le_sha256(values)
if ~isa(values, "double")
    error("spectro:hashType", "Hash input must be double.");
end
[~, ~, endian] = computer;
if endian == 'B'
    values = swapbytes(values);
end
bytes = typecast(values(:), "uint8");
engine = java.security.MessageDigest.getInstance("SHA-256");
engine.update(typecast(bytes, "int8"));
raw = typecast(int8(engine.digest()), "uint8");
digest = string(lower(reshape(dec2hex(raw, 2).', 1, [])));
end
