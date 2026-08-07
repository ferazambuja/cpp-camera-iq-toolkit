# Sample fixtures

Tiny committed fixtures so `build → test` runs with **no** private data.

- Keep files here small (a few small patches / synthetic frames), not full RAW sets.
- Large RAW datasets are referenced by `configs/datasets.local.json`, never committed.
- The `.gitignore` allows files under `data/samples/` back in via `!data/samples/**`.
- `manifest_fixture/` contains RAW-like filenames and a CSV shape probe fixture.
  The `.RAF` files are plain text placeholders, not real camera files; use
  `camera_iq manifest data/samples/manifest_fixture --no-exif`.
- `spectral_2017/` contains retained text measurements used by the spectral
  cross-check. `hid_repeats.csv` and the two `colorchecker_measurement_*.csv`
  files are normalized public schemas; the four CGATS files are exact source
  copies because their layout and metadata differences are part of the study.
  `source_receipt.json` binds all eight retained inputs and eight derived or
  copied outputs by SHA-256 without retaining a machine path.
  `d800_legacy_method_receipt.json` separately binds the unredistributed legacy
  scripts, derived table/figure, and complete two-file NEF inventory used for a
  code-level method audit; it does not make that historical run reproducible.
