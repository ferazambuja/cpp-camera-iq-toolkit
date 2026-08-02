function export_spectro_crosscheck(archive_root, output_csv)
%EXPORT_SPECTRO_CROSSCHECK  Emit MATLAB's own reading of every spectro MAT file.
%
%   This is a verification aid, not part of the ingest path. The C++ reader in
%   src/mat_file.cpp parses the archived .mat files directly, so no published
%   number depends on running this. What this script provides is an independent
%   second opinion: MATLAB is the reference implementation of its own format, so
%   agreement between its values and the C++ reader's is evidence the subset
%   parser is right, where "it did not throw" is not.
%
%   Every field is printed with %.17g, which round-trips an IEEE-754 double
%   exactly. A comparison that used fewer digits would hide precisely the
%   low-bit disagreements this check exists to find.
%
%   The two vector fields are compared by SHA-256 over their raw IEEE-754 bytes
%   rather than by a sum. MATLAB's SUM is pairwise, so it does not reproduce a
%   left-to-right accumulation in C++ bit for bit; a sum that disagreed in the
%   last place would be indistinguishable from a genuine parse difference.
%   Hashing the widened doubles compares all 201 samples exactly and does not
%   depend on either side's summation order.
%
%   Usage:
%     export_spectro_crosscheck('/path/to/Project Camera', 'matlab_crosscheck.csv')
%
%   Then compare that file against the per-reading CSV the C++ ingest path
%   emits for the same archive root, with tools/compare_spectro_crosscheck.py.

    if nargin < 2
        output_csv = 'matlab_crosscheck.csv';
    end

    listing = dir(fullfile(archive_root, '**', '*.mat'));
    handle = fopen(output_csv, 'w');
    if handle < 0
        error('export_spectro_crosscheck:output', ...
              'cannot open %s for writing', output_csv);
    end
    closer = onCleanup(@() fclose(handle));

    fprintf(handle, ['relative_path,points,wl_first,wl_last,wl_sha256,' ...
                     'radiance_sha256,X,Y,Z,total_radiance,cct_k,duv,' ...
                     'repeat_on_error,repetitions\n']);

    accepted = 0;
    skipped = 0;
    for index = 1:numel(listing)
        entry = listing(index);
        full_path = fullfile(entry.folder, entry.name);
        relative = strrep(erase(full_path, [archive_root filesep]), filesep, '/');

        contents = whos('-file', full_path);
        if ~any(strcmp({contents.name}, 'measurements'))
            % The legacy workspace saves hold dozens of per-patch variables and
            % no `measurements` struct. The C++ reader refuses them by name;
            % skipping them here keeps the two sides comparing the same set.
            skipped = skipped + 1;
            continue
        end

        loaded = load(full_path, 'measurements');
        m = loaded.measurements;

        wl = double(m.wl(:));
        radiance = double(m.radiance(:));
        xyz = double(m.XYZ(:));

        fprintf(handle, '%s,%d,%.17g,%.17g,%s,%s,', ...
                relative, numel(wl), wl(1), wl(end), ...
                double_vector_sha256(wl), double_vector_sha256(radiance));
        fprintf(handle, '%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%d\n', ...
                xyz(1), xyz(2), xyz(3), double(m.totalRadiance), ...
                double(m.CCT), double(m.Duv), ...
                double(logical(m.repeatOnError)), ...
                double(m.numCurrentRepetitions));
        accepted = accepted + 1;
    end

    fprintf('wrote %d readings to %s; skipped %d files without a measurements struct\n', ...
            accepted, output_csv, skipped);
end

function hex = double_vector_sha256(values)
%DOUBLE_VECTOR_SHA256  SHA-256 over the little-endian IEEE-754 bytes of a column.
%   TYPECAST reinterprets without converting, so this hashes the stored bit
%   patterns. MessageDigest returns signed int8; widening it through uint8 keeps
%   the hex digits from going negative.
    digest = java.security.MessageDigest.getInstance('SHA-256');
    digest.update(typecast(double(values(:)), 'uint8'));
    bytes = typecast(digest.digest(), 'uint8');
    hex = lower(reshape(dec2hex(bytes, 2).', 1, []));
end
