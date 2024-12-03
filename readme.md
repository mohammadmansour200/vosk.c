# C CLI wrapper for VOSK speech recognition toolkit.

## Usage

Wav file should be in a PCM 16-bit mono format:

```bash
ffmpeg -i <input_file_path> -ar 16000 -ac 1 -sample_fmt s16 <output_file_path>.wav
```

```bash
./vosk <model_folder_path> <wav_file_path> <output_json_file_path>
```

## Build

### 1. Download Vosk shared library

Download your own platform's shared lib: https://github.com/alphacep/vosk-api/releases
then move it to vosk folder

### 2. Build the program using cmake

```bash
cmake -B build -S .
cmake --build build
```
