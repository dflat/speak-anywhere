# speak-anywhere Setup Guide

## 1. Build speak-anywhere

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Produces two binaries in `build/`:
- `speak-anywhere` — daemon
- `sa` — CLI client

## 2. Set up whisper-server (Ubuntu LAN machine)

whisper.cpp is an existing open-source project that includes `whisper-server`, an HTTP
server accepting WAV uploads and returning JSON transcripts.

### Install dependencies

```bash
sudo apt update
sudo apt install build-essential cmake git
# If you have an NVIDIA GPU:
sudo apt install nvidia-cuda-toolkit
```

### Build whisper.cpp

```bash
git clone https://github.com/ggml-org/whisper.cpp.git
cd whisper.cpp

# CPU only:
cmake -B build
# With CUDA (recommended if you have a GPU):
cmake -B build -DGGML_CUDA=1

cmake --build build -j$(nproc)
```

### Download a model

```bash
# Best balance of speed and quality:
./models/download-ggml-model.sh large-v3-turbo

# Or download directly:
wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin \
    -P models/
```

| Model | Size | Notes |
|-------|------|-------|
| `large-v3-turbo` | 1.5 GB | **Recommended** — near large-v3 quality, much faster |
| `large-v3-turbo-q5_0` | 574 MB | Quantized, minimal quality loss, if VRAM is tight |
| `large-v3` | 2.9 GB | Best quality, slowest |

### Run the server

```bash
./build/bin/whisper-server \
    --model models/ggml-large-v3-turbo.bin \
    --host 0.0.0.0 \
    --port 8080 \
    --threads 4 \
    --convert \
    --print-progress
```

Key flags:
- `--host 0.0.0.0` — listen on all interfaces (default is localhost only)
- `--convert` — auto-convert audio formats via ffmpeg
- `--print-progress` — useful for monitoring

### Test whisper-server directly

```bash
# From your Arch machine (replace SERVER_IP):
curl http://SERVER_IP:8080/inference \
    -F file="@test.wav" \
    -F response_format="json" \
    -F temperature="0.0"
```

### (Optional) systemd service on the server

Create `/etc/systemd/system/whisper-server.service`:

```ini
[Unit]
Description=whisper.cpp server
After=network.target

[Service]
Type=simple
ExecStart=/path/to/whisper.cpp/build/bin/whisper-server \
    --model /path/to/models/ggml-large-v3-turbo.bin \
    --host 0.0.0.0 --port 8080 --threads 4 --convert
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now whisper-server
```

## 3. Configure speak-anywhere

Create `~/.config/speak-anywhere/config.json`:

```json
{
  "backend": {
    "type": "lan",
    "url": "http://SERVER_IP:8080",
    "api_format": "whisper.cpp",
    "language": "en"
  },
  "output": { "default": "clipboard" }
}
```

See `config/config.example.json` for all options.

## 4. Run speak-anywhere

### Foreground (for testing)

```bash
# Terminal 1: start the daemon
./build/speak-anywhere --foreground --verbose

# Terminal 2: use the CLI client
./build/sa status                        # → "State: idle"
./build/sa start                         # start recording
# speak into your mic...
./build/sa stop                          # stop recording, transcribe, print result

# Toggle is useful for keybindings (start if idle, stop if recording):
./build/sa toggle --output clipboard
```

### As a systemd user service

```bash
# Install binaries
cp build/speak-anywhere build/sa ~/.local/bin/

# Install and start the service
cp systemd/speak-anywhere.service ~/.config/systemd/user/
systemctl --user enable --now speak-anywhere
```

### Sway keybinding

Add to your Sway config (`~/.config/sway/config`):

```
bindsym $mod+grave exec sa toggle --output clipboard
```

Press `$mod+`` to start recording, press again to stop and get the transcript
in your clipboard.

## 5. CLI reference

```
sa start  [--output clipboard|type]   Start recording
sa stop                               Stop and transcribe
sa toggle [--output clipboard|type]   Toggle recording on/off
sa status                             Show daemon state
sa history [--limit N]                Show recent transcriptions
```

Output methods:
- `clipboard` — copies transcript to clipboard via wl-copy
- `type` — types transcript into the focused window via wtype
  (uses clipboard+paste shortcut for terminals)
