# Sample fixtures

Tiny committed fixtures so `build → test` runs with **no** private data.

- Keep files here small (a few small patches / synthetic frames), not full RAW sets.
- Large RAW datasets are referenced by `configs/datasets.local.json`, never committed.
- The `.gitignore` allows files under `data/samples/` back in via `!data/samples/**`.
- `manifest_fixture/` contains RAW-like filenames and a CSV shape probe fixture.
  The `.RAF` files are plain text placeholders, not real camera files; use
  `camera_iq manifest data/samples/manifest_fixture --no-exif`.
