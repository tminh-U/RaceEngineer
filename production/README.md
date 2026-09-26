# RaceEngineer production package

This folder contains the repeatable Windows x64 Release build and package flow.

## Build

Run from an x64 MSVC environment with Qt and Ninja available:

```powershell
.\production\scripts\build-release.ps1 `
  -QtPrefix C:\Qt\6.8.3\msvc2022_64 `
  -EnableLtcg
```

The production build is kept in `build-production/`, separate from the
development build. Runtime models and voices must already be installed; the
build does not download them.

## Pit strategy

The preferred path scores legal pit laps by expected remaining race time. It
requires `profile.json` and an approved `plan_calibration.json` in
`models/pit_strategy/`; the planner uses one CPU worker and needs no XGBoost
DLL. `training/pit_strategy/calibrate_pit_plan.py` creates research artifacts
with `deployment_ready=false`. Real AC/ACC pit-service logs, held-out races and
prospective race comparisons are required before approval. The example profile
starts with all service and conditions flags disabled; set them only after
verifying the exact race rules and tyre/refuel setup. CMake copies only an
approved artifact into the app package.

Offline calibration: `python training/pit_strategy/calibrate_pit_plan.py --profile profile.json --output plan_calibration.json recordings/race-1.jsonl`.
Pass additional JSONL files as separate arguments. Inspect the reported missing
fields and validation metrics; the script never approves its own output.

The earlier CPU XGBoost Ranker path remains supported when its approved
bundle is present in `models/pit_strategy/`. That bundle needs `pit_ranker.json`,
`feature_schema.json`, `parity_vectors.json`, `manifest.json`, `profile.json`,
`xgboost.dll`, and its `LICENSE`. The manifest must be from real sim data,
explicitly marked `deployment_ready=true`, and contain SHA-256 values in
`model_sha256` and `xgboost_sha256`. Copy and fill
`production/config/pit_strategy.profile.example.json` as `profile.json`; verify
simulator, exact track/car IDs, race length, pit rules, service, conditions, and
pit loss before enabling it. CMake copies and packages only an approved bundle.
The app checks hashes, feature order, race profile, and notebook parity vectors.

The training notebook is `training/pit_strategy/train_pit_ranker.ipynb`. Its demo
model is never deployable. The app records one JSON line per completed race lap at
the Qt local application data path under `pit_strategy_laps.jsonl`; when it reaches
20 MiB, the file is preserved under `recordings/` with a unique name. These
observations need calibrated counterfactual labels before training a decision
model. No approved model bundle is included with the project yet.

## Package and verify

Install Inno Setup 6 and make `ISCC.exe` available on `PATH`, or pass its path
through `-ISCCPath`.

```powershell
.\production\scripts\package-portable.ps1 `
  -Version 1.0.3 `
  -QtPrefix C:\Qt\6.8.3\msvc2022_64 `
  -ISCCPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

.\production\scripts\verify-release.ps1 `
  -PackageDirectory .\production\dist\RaceEngineer-1.0.3
```

The package script:

1. Runs `cmake --install`.
2. Deploys Qt runtime files.
3. Copies spotter audio, models, voices, and the optional AC Python companion.
4. Bundles the MSVC runtime files and `vc_redist.x64.exe`.
5. Writes `release-manifest.json` with SHA-256 hashes.
6. Creates a Windows x64 portable ZIP.
7. Compiles `RaceEngineer-1.0.3-Setup.exe` with Inno Setup.

The Inno Setup installer installs per-user under
`%LOCALAPPDATA%\Programs\RaceEngineer`, creates a Start Menu shortcut, and
registers a standard Windows uninstaller. It can also run the bundled Visual
C++ x64 runtime installer with elevation when available.

Do not put API keys or user `settings.json` files in this folder. API keys are
stored through Windows Credential Manager.

## Local recording and training

Analysis & Strategy includes automatic lap/pit recording, Process, Train XGBoost
pace, cancellation and a shortcut to the data folder. Recording defaults on and
the choice persists. Logs stay in the app's local-data directory; 20 MB chunks
are archived without deleting older recordings. Process/train run only while
disconnected from the simulator and stop if it reconnects.

The package includes the Python pipeline. Process needs Python 3 in PATH;
training also needs `numpy` and `xgboost` in that interpreter:
`python -m pip install numpy xgboost`. A private interpreter can instead be
placed at `training/python/python.exe` beside the app. Training uses one CPU
thread, saves a new model/report run automatically, and predicts next-lap pace.
These research artifacts do not replace the approved pit Ranker bundle.

The separate **Train model lốp** action uses processed, confirmed clean-lap pairs to train CPU research regressors for one-lap tyre-wear and core-temperature changes. It saves held-out metrics against a persistence baseline; fuel range remains deterministic. Missing or flat wear data yields a skipped model; ACC wear still needs external validation even if a research model trains. No research artifact enables pit recommendations.

## Voluntary race-data sharing

The release operator supplies `RACEENGINEER_SHARE_ENDPOINT` (the HTTPS receiver
URL) and `RACEENGINEER_SHARE_TOKEN` (the receiver's ingest token, at least 32
characters) in `.env` beside the executable. Process environment variables
override that file when set. Existing saved endpoint/token
settings remain readable for compatibility. Players only see the consent switch
and upload status in Analysis & Strategy; if no receiver is configured, the
switch is unavailable. Consent is off by default. After a race ends, the app
prepares a field-restricted JSONL with a new random session ID and shifted
timestamps; it queues uploads locally and retries when the user presses
**Gửi lại** or restarts the app. No voice, chat or AI-provider API credential is sent.
Turning consent off cancels the current upload and removes queued share copies;
original local recordings remain.

### Personal Google Drive (no VPS)

After updating `google-drive-receiver.gs`, paste the new code into the existing Apps Script project and create a **new deployment version**. The current app sends `realism_confirmed` and tyre temperature fields; an older deployed script rejects them. Only confirmed Race sessions are queued, while raw local logs remain available. See [logging guide](../docs/logging-guide.md).

1. Create a private folder in your Google Drive. Copy its ID from the folder URL.
2. At [script.google.com](https://script.google.com), create a project and paste
   `production/google-drive-receiver.gs` into `Code.gs`.
3. In **Project Settings → Script properties**, add `RACEENGINEER_FOLDER_ID`
   (the folder ID) and `RACEENGINEER_SHARE_TOKEN` (a random token of at least
   32 characters). Generate one with
   `python -c "import secrets; print(secrets.token_urlsafe(32))"`.
4. **Deploy → New deployment → Web app**. Select **Execute as: Me** and
   **Who has access: Anyone**. Authorize Drive access as the folder owner.
   Copy the deployed `/exec` URL, not the `/dev` test URL.
5. Create a UTF-8 file named `.env` beside `RaceEngineer.exe` (`build/.env`
   when running from this repository, or `bin/.env` in a portable package):

   ```dotenv
   RACEENGINEER_SHARE_ENDPOINT=https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID/exec
   RACEENGINEER_SHARE_TOKEN=THE_SAME_RANDOM_TOKEN
   ```

   Save it, then launch the app normally. Reuse the exact token from Script
   properties; generate it only once. Process environment variables still
   override `.env` when set. Never put Google account credentials in this file.

The script validates each upload, keeps the token out of the Drive file, and
deduplicates retries by file hash. Uploaded files are named
`pit_strategy_laps-<sha256>.jsonl`; download them into a `recordings/` folder
to use the existing local training pipeline. The token distributed to a desktop
client is an ingest gate, not a secret that can prove a player's identity.
Use this direct path for a small trusted pilot; a public release needs a
receiver with per-client controls and rate limits. Keep Google account OAuth
credentials entirely in Apps Script.

### Oracle VPS receiver (optional)

`production/share_receiver.py` is the HTTPS upload receiver's local backend. Copy it
to the VPS, run it as an unprivileged service, and put an HTTPS reverse proxy in
front of `127.0.0.1:8087`. Set `RACEENGINEER_SHARE_TOKEN` to a random value of at
least 32 characters and `RACEENGINEER_SHARE_DIR` to a private data directory.
Generate a token with `python3 -c "import secrets; print(secrets.token_urlsafe(32))"`.
The receiver endpoint is `https://your-domain.example/v1/race-data`; `/health`
is local health check. Do not expose port 8087 directly or put the token in a
public package. Use a separate receiver instance for each pilot group; rotate
its token if exposed. Files are stored in `recordings/pit_strategy_laps-<sha256>.jsonl`,
which `training/pit_strategy/local_training.py --data-dir <share-dir>` can read.
Collecting logs does not automatically approve or deploy a pit model. Decide
the retention and deletion policy before opening uploads to players.

## Optional Assetto Corsa addon

The package includes the optional Python companion at
`extras\AssettoCorsa\apps\python\RaceEngineer`. The installer does not write into
the game directory; copy that folder to Assetto Corsa or install it through
Content Manager when extra telemetry is needed.
