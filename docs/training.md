# Voice Training Guide

This guide explains how to use
[`training/vieneu/train_vieneu_colab.ipynb`](../training/vieneu/train_vieneu_colab.ipynb)
to create a Vietnamese voice for RaceEngineer with VieNeu-TTS v3 Turbo.

The notebook has two workflows:

- **LoRA fine-tuning:** recommended when you have roughly 10–30 minutes of
  clean speech. This creates a customized model and voice preset.
- **Fast preset extraction:** recommended when you only have one or a few
  recordings. This keeps the original model and creates only a voice preset.

The notebook runs in Google Colab and downloads the VieNeu-TTS source code,
Python packages, and the base model. It does not train on the local Windows
machine.

## 1. Requirements

- A Google account with access to Google Colab.
- A Colab runtime with an NVIDIA GPU. Select **Runtime > Change runtime type >
  T4 GPU** when available.
- Internet access in the Colab session.
- A voice dataset belonging to one speaker.
- Enough Google Drive or local Colab storage for the base model, dataset, and
  generated ZIP file.

Run the notebook cells from top to bottom. Re-run the setup, model, and dataset
cells after a Colab runtime disconnects.

## 2. Prepare the dataset

Create a ZIP file named `dataset.zip`. The preferred layout is:

```text
dataset/
├── metadata.csv
└── raw_audio/
    ├── 001.wav
    ├── 002.wav
    └── ...
```

Each audio file must have one matching line in `metadata.csv`:

```text
001.wav|Xin chào, đây là giọng kỹ sư đường đua.
002.wav|Nhiên liệu hiện tại đủ cho thêm năm vòng.
```

Use these rules:

- Keep the filename in `metadata.csv` identical to the audio filename.
- Use one `filename|transcript` record per line.
- Do not use `|` inside the transcript.
- Prefer clear WAV recordings with little background noise, clipping, music, or
  reverb.
- Keep each clip short and focused, normally around 1–20 seconds.
- Use consistent microphone distance, volume, and speaking style.
- For LoRA, use varied Vietnamese sentences and include words the engineer
  will say during a race.

The notebook accepts `dataset.zip` uploaded through the Colab Files panel.
It can also mount Google Drive when `MOUNT_DRIVE` is enabled.

## 3. Start the notebook

Open the notebook, select a GPU runtime, then run the cells in order:

1. Check CUDA and the detected GPU.
2. Clone VieNeu-TTS and install the training and export dependencies.
3. Download the `pnnbao-ump/VieNeu-TTS-v3-Turbo` base checkpoint.
4. Upload or mount `dataset.zip`.
5. Create or verify `metadata.csv`.
6. Prepare the training dataset.
7. Run either LoRA fine-tuning or fast preset extraction.
8. Export the generated files and download the ZIP.

The important Colab paths are:

```text
/content/dataset/
/content/base_model/
/content/VieNeu-TTS/
```

## 4. Choose a workflow

### Option A: LoRA fine-tuning

Use this when the voice needs to learn a specific speaking style, pronunciation,
or race-engineer vocabulary.

Edit the configuration cell before starting training:

```python
VOICE_NAME = "Minh Quân"       # Name shown in RaceEngineer
RUN_ID = "minh_quan_race"     # Output folder name
EPOCHS = 15
BATCH_SIZE = 2
LEARNING_RATE = 1e-4
```

The notebook then:

1. Copies the dataset to
   `/content/VieNeu-TTS/finetune/dataset/`.
2. Creates `train.parquet` with `finetune/prepare_dataset.py`.
3. Runs `finetune/train_lora.py`.
4. Merges the LoRA result into the model.
5. Creates a voice preset with `finetune/make_voice.py`.
6. Converts the merged weights to native C++ files.

The merged model is written to:

```text
/content/VieNeu-TTS/finetune/output/<RUN_ID>/merged/
```

The expected export files are:

```text
/content/RaceEngineer_CustomModel/backbone.gguf
/content/RaceEngineer_CustomModel/vieneu_v3_heads.npz
/content/RaceEngineer_CustomModel/voices_v3_turbo.json
```

Training time depends on the dataset and the Colab GPU. Check the output after
each cell. A successful run must contain the `merged` directory before
conversion.

### Option B: Fast preset extraction

Use this when you want a usable voice quickly without fine-tuning the model.
The notebook extracts speaker embeddings from the WAV files and averages them
into a new preset.

This option requires at least one usable WAV file. Clips shorter than one second
are skipped by the speaker-embedding step. The output is:

```text
/content/RaceEngineer_FastPreset/voices_v3_turbo.json
```

This option keeps the original `backbone.gguf` and
`vieneu_v3_heads.npz`. It is usually the best first test before spending time
on LoRA training.

## 5. Automatic transcription

If you have audio but do not have transcripts, set:

```python
AUTO_TRANSCRIBE = True
```

The notebook installs OpenAI Whisper, transcribes the WAV/MP3 files in
`/content/dataset/raw_audio/`, and writes:

```text
/content/dataset/metadata.csv
```

Review the generated text before preprocessing. Correct names, numbers,
technical terms, and punctuation manually when necessary. Set
`AUTO_TRANSCRIBE = False` when you already prepared a correct `metadata.csv`.

## 6. Install the result in RaceEngineer

### LoRA output

Copy these three files from the downloaded ZIP:

```text
backbone.gguf
vieneu_v3_heads.npz
voices_v3_turbo.json
```

to the source model directory:

```text
models/vieneu-v3/
```

If you run an already-built executable directly from a separate build directory,
copy the same files to:

```text
build/models/vieneu-v3/
```

For a production package, update the source `models/vieneu-v3/` directory
before running the production packaging script described in
[`build.md`](build.md).

### Fast preset output

Copy only:

```text
voices_v3_turbo.json
```

to `models/vieneu-v3/`. Keep the original model files in that directory.

Restart RaceEngineer after replacing the files. The new voice name should then
appear in the voice selection list.

## 7. Troubleshooting

### No GPU is detected

Stop the run, select **Runtime > Change runtime type > T4 GPU**, reconnect, and
run the notebook setup cells again. Do not start LoRA training on a CPU runtime.

### CUDA out-of-memory

Reduce `BATCH_SIZE` to `1`. If it still fails, use shorter clips, reduce the
dataset size for a test run, or restart the Colab runtime to release VRAM.

### `metadata.csv` is missing

Check that it is at `/content/dataset/metadata.csv`, that every referenced
audio file exists under `raw_audio/`, and that each line uses exactly one `|`
separator. Alternatively, enable `AUTO_TRANSCRIBE`.

### `train.parquet` is not created

Fix the dataset layout and metadata first, then rerun the preprocessing cell.
The notebook expects the dataset at
`/content/VieNeu-TTS/finetune/dataset/` after the copy step.

### A generated voice is missing or sounds wrong

Check that the voice name is unique, the reference audio is clear and at least
one second long, and the transcript matches the recording. For LoRA, inspect
the training output and confirm that the `merged` directory was created.

### Colab disconnects or resets

The `/content` directory is temporary. Re-upload or remount the dataset, rerun
the setup and model cells, and download the ZIP immediately after export. Do not
assume checkpoints in `/content` survive a runtime reset.

### RaceEngineer does not show the new voice

Confirm that `voices_v3_turbo.json` was copied to the directory used by the
executable, not only to the repository source directory. Close and reopen the
application after replacing the file.
