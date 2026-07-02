# Sample fixtures

Tiny committed fixtures so `build → test` runs with **no** private data.

- Keep files here small (a few small patches / synthetic frames), not full RAW sets.
- Large RAW datasets are referenced by `configs/datasets.local.json`, never committed.
- The `.gitignore` allows files under `data/samples/` back in via `!data/samples/**`.
