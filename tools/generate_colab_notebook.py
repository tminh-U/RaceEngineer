import json
import shutil
from pathlib import Path

def create_notebook():
    cells = [
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "# 🏎️ Huấn Luyện & Tích Hợp Giọng Nói Mới Cho VieNeu-TTS v3 Turbo (RaceEngineer)\n",
                "\n",
                "Notebook này cung cấp quy trình trọn gói để **huấn luyện (train/fine-tune) hoặc nhân bản (voice clone)** một giọng nói tiếng Việt mới từ tập dữ liệu âm thanh của bạn bằng Google Colab (GPU T4 miễn phí), sau đó **chuyển đổi (convert)** thành định dạng tương thích trực tiếp với ứng dụng **RaceEngineer** trên máy tính.\n",
                "\n",
                "---\n",
                "\n",
                "### 🎯 Có 2 phương pháp bạn có thể lựa chọn trong Notebook này:\n",
                "1. **Phương pháp A (LoRA Fine-tuning — Khuyên dùng khi có 10–30 phút audio):**\n",
                "   - Học sâu phong cách đọc, ngữ điệu đua xe, sửa phát âm từ vựng đặc thù.\n",
                "   - Model gốc được merge cùng LoRA adapter và tự động convert thành `backbone.gguf` + `vieneu_v3_heads.npz`.\n",
                "2. **Phương pháp B (Zero-Shot / Few-Shot Profile Extraction — Siêu tốc 2 phút):**\n",
                "   - Dành cho bạn chỉ có 1 hoặc vài câu audio (10–60 giây tổng cộng).\n",
                "   - Trích xuất vector đặc trưng người nói (Speaker Embedding 192 chiều) và mã âm học (Codec Codes) từ tập audio, đóng gói thành cấu hình preset JSON mà không cần train lại model nền.\n",
                "\n",
                "---\n",
                "\n",
                "### 📋 Cấu trúc thư mục Dataset chuẩn bị:\n",
                "```text\n",
                "dataset/\n",
                "  ├── metadata.csv       # Định dạng: ten_file.wav|Văn bản tiếng Việt tương ứng\n",
                "  └── raw_audio/         # Chứa các file âm thanh .wav (1-20 giây mỗi file, âm thanh rõ ràng)\n",
                "```\n",
                "*(Lưu ý: Nếu bạn chỉ có file audio mà chưa gõ văn bản, Notebook này có tích hợp sẵn công cụ tự động phiên âm bằng Whisper ở Bước 3!)*"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "## ⚙️ Bước 1: Kiểm Tra GPU Google Colab\n",
                "Hãy đảm bảo bạn đã bật GPU: Vào menu **Runtime** > **Change runtime type** > Chọn **T4 GPU**."
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "!nvidia-smi\n",
                "import torch\n",
                "print(\"=\" * 50)\n",
                "print(f\"PyTorch Version: {torch.__version__}\")\n",
                "print(f\"CUDA Available: {torch.cuda.is_available()}\")\n",
                "if torch.cuda.is_available():\n",
                "    print(f\"Thiết bị GPU: {torch.cuda.get_device_name(0)}\")\n",
                "    print(f\"Bộ nhớ VRAM: {torch.cuda.get_device_properties(0).total_memory / 1e9:.2f} GB\")\n",
                "else:\n",
                "    print(\"⚠️ CẢNH BÁO: Chưa phát hiện GPU! Vào Runtime > Change runtime type > T4 GPU.\")\n",
                "print(\"=\" * 50)"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "## 📦 Bước 2: Cài Đặt Thư Viện & Clone Mã Nguồn VieNeu-TTS"
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "import os, sys, shutil\n",
                "\n",
                "# Chuyển về thư mục gốc Colab\n",
                "%cd /content\n",
                "\n",
                "# Clone kho lưu trữ VieNeu-TTS chính thức\n",
                "if not os.path.exists(\"VieNeu-TTS\"):\n",
                "    !git clone https://github.com/pnnbao97/VieNeu-TTS.git\n",
                "\n",
                "%cd /content/VieNeu-TTS\n",
                "\n",
                "# Cài đặt các gói phụ thuộc cần thiết cho huấn luyện và xuất GGUF\n",
                "!pip install -q --upgrade pip\n",
                "!pip install -q torch transformers>=4.48.0 peft accelerate datasets soundfile librosa safetensors gguf onnxruntime scipy sea-g2p huggingface_hub\n",
                "!pip uninstall -y torchao 2>/dev/null || true\n",
                "\n",
                "print(\"✅ Môi trường Python đã sẵn sàng!\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "## 📥 Bước 3: Tải Mô Hình Nền Tảng VieNeu-TTS v3 Turbo Gốc\n",
                "Tải checkpoint gốc từ Hugging Face để làm nền tảng fine-tune hoặc trích xuất embedding."
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "from huggingface_hub import snapshot_download\n",
                "\n",
                "print(\"Đang tải checkpoint VieNeu-TTS-v3-Turbo...\")\n",
                "base_model_dir = snapshot_download(\n",
                "    repo_id=\"pnnbao-ump/VieNeu-TTS-v3-Turbo\",\n",
                "    local_dir=\"/content/base_model\",\n",
                "    local_dir_use_symlinks=False\n",
                ")\n",
                "print(f\"✅ Đã tải mô hình gốc về: {base_model_dir}\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "## 📂 Bước 4: Chuẩn Bị Dataset Của Bạn\n",
                "Tải dataset lên Google Colab:\n",
                "- **Cách 1 (Nhanh nhất):** Nén thư mục `dataset` trên máy thành `dataset.zip` và kéo thả vào bảng Files bên trái Colab.\n",
                "- **Cách 2:** Lưu `dataset.zip` trên Google Drive và mount vào Colab.\n",
                "- Cấu trúc mong muốn: `/content/dataset/metadata.csv` và `/content/dataset/raw_audio/*.wav`."
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "import os, shutil, glob\n",
                "\n",
                "os.makedirs(\"/content/dataset/raw_audio\", exist_ok=True)\n",
                "\n",
                "# (Tùy chọn) Mount Google Drive nếu bạn lưu dataset trên Drive\n",
                "MOUNT_DRIVE = False\n",
                "if MOUNT_DRIVE:\n",
                "    from google.colab import drive\n",
                "    drive.mount('/content/drive')\n",
                "    # Thay đường dẫn tới file dataset.zip trên Drive của bạn nếu cần:\n",
                "    # !cp \"/content/drive/MyDrive/dataset.zip\" /content/dataset.zip\n",
                "\n",
                "# Tự động giải nén và định vị file\n",
                "if os.path.exists(\"/content/dataset.zip\"):\n",
                "    !unzip -q -o /content/dataset.zip -d /content/temp_dataset\n",
                "    if os.path.exists(\"/content/temp_dataset/dataset\"):\n",
                "        !cp -rf /content/temp_dataset/dataset/* /content/dataset/\n",
                "    elif os.path.exists(\"/content/temp_dataset/metadata.csv\"):\n",
                "        !cp -rf /content/temp_dataset/* /content/dataset/\n",
                "    else:\n",
                "        !cp -f /content/temp_dataset/*.wav /content/dataset/raw_audio/ 2>/dev/null || true\n",
                "    print(\"✅ Đã giải nén và bố trí dataset vào /content/dataset/\")\n",
                "\n",
                "# Thống kê dữ liệu đã nạp\n",
                "audio_count = len(glob.glob(\"/content/dataset/raw_audio/*.wav\") + glob.glob(\"/content/dataset/raw_audio/*.mp3\"))\n",
                "has_meta = os.path.exists(\"/content/dataset/metadata.csv\")\n",
                "print(f\"📊 Số lượng file audio tìm thấy: {audio_count}\")\n",
                "print(f\"📄 File metadata.csv: {'Đã có' if has_meta else 'Chưa có'}\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "### 🎙️ (Tùy Chọn) Tự Động Tạo `metadata.csv` Bằng Whisper\n",
                "Nếu bạn đã có các file `.wav` trong `raw_audio/` nhưng **chưa có file `metadata.csv` ghi nội dung chữ**, hãy đổi `AUTO_TRANSCRIBE = True` để hệ thống tự động nghe và phiên âm text cho toàn bộ file audio của bạn!"
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "AUTO_TRANSCRIBE = False  # Đổi thành True nếu bạn muốn tự động nhận diện giọng nói ra text\n",
                "\n",
                "if AUTO_TRANSCRIBE:\n",
                "    !pip install -q openai-whisper\n",
                "    import whisper, glob\n",
                "    from pathlib import Path\n",
                "\n",
                "    print(\"Đang nạp mô hình Whisper để nhận dạng tiếng Việt...\")\n",
                "    asr_model = whisper.load_model(\"base\")\n",
                "    audio_files = sorted(glob.glob(\"/content/dataset/raw_audio/*.wav\") + glob.glob(\"/content/dataset/raw_audio/*.mp3\"))\n",
                "    \n",
                "    metadata_path = \"/content/dataset/metadata.csv\"\n",
                "    with open(metadata_path, \"w\", encoding=\"utf-8\") as f:\n",
                "        for audio_path in audio_files:\n",
                "            fname = Path(audio_path).name\n",
                "            result = asr_model.transcribe(audio_path, language=\"vi\")\n",
                "            text = result[\"text\"].strip().replace(\"|\", \" \")\n",
                "            f.write(f\"{fname}|{text}\\n\")\n",
                "            print(f\"✓ {fname} -> {text}\")\n",
                "    print(f\"✅ Đã tạo xong metadata.csv với {len(audio_files)} mẫu âm thanh!\")\n",
                "else:\n",
                "    print(\"ℹ️ AUTO_TRANSCRIBE tắt. Bạn hãy đảm bảo đã upload file /content/dataset/metadata.csv.\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "## ⚙️ Bước 5: Tiền Xử Lý Dữ Liệu (`prepare_dataset.py`)\n",
                "Tiền xử lý sẽ tự động:\n",
                "1. Chuẩn hóa ngữ âm tiếng Việt bằng `sea-g2p`.\n",
                "2. Trích xuất vector giọng nói 192 chiều (`speaker_embedding`) bằng mô hình CAM++ ONNX.\n",
                "3. Mã hóa âm thanh thành chuỗi 16-group codec token bằng mô hình MOSS Codec.\n",
                "4. Xuất file `/content/dataset/train.parquet`."
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "%cd /content/VieNeu-TTS\n",
                "\n",
                "# Đồng bộ dữ liệu vào thư mục finetune/dataset (yêu cầu của VieNeu safe_path)\n",
                "!mkdir -p /content/VieNeu-TTS/finetune/dataset\n",
                "!cp -rf /content/dataset/* /content/VieNeu-TTS/finetune/dataset/\n",
                "\n",
                "# Kiểm tra file metadata.csv trước khi chạy\n",
                "assert os.path.exists(\"finetune/dataset/metadata.csv\"), \"Thiếu file finetune/dataset/metadata.csv!\"\n",
                "\n",
                "# Chạy tiền xử lý dữ liệu\n",
                "!python finetune/prepare_dataset.py --dataset-dir finetune/dataset\n",
                "\n",
                "assert os.path.exists(\"finetune/dataset/train.parquet\"), \"Lỗi: Không tạo được file train.parquet!\"\n",
                "print(\"✅ Tiền xử lý dữ liệu hoàn tất! File train.parquet đã sẵn sàng để train.\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "---\n",
                "# 🚀 PHƯƠNG ÁN A: Huấn Luyện LoRA & Xuất Model GGUF\n",
                "*(Khuyên dùng khi bạn có 5–30 phút audio và muốn giọng học sâu cách đọc)*"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "### A1. Thiết Lập Tham Số Huấn Luyện & Bắt Đầu Train LoRA"
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "# Tên định danh cho đợt train và tên giọng hiển thị\n",
                "VOICE_NAME = \"Minh Quân\"             # Tên giọng hiển thị trong RaceEngineer\n",
                "RUN_ID = \"minh_quan_race\"            # Tên thư mục đầu ra\n",
                "EPOCHS = 15                           # 10 - 20 epochs là tối ưu cho dữ liệu nhỏ\n",
                "BATCH_SIZE = 2                       # 2 hoặc 4 cho GPU T4\n",
                "LEARNING_RATE = 1e-4                  # Tốc độ học tối ưu của LoRA\n",
                "\n",
                "%cd /content/VieNeu-TTS\n",
                "# Gỡ bỏ torchao nếu bị xung đột phiên bản với peft\n",
                "!pip uninstall -y torchao 2>/dev/null || true\n",
                "\n",
                "# Chạy huấn luyện LoRA và tự động merge vào mô hình gốc\n",
                "!python finetune/train_lora.py \\\n",
                "    --data finetune/dataset/train.parquet \\\n",
                "    --run {RUN_ID} \\\n",
                "    --epochs {EPOCHS} \\\n",
                "    --batch-size {BATCH_SIZE} \\\n",
                "    --lr {LEARNING_RATE} \\\n",
                "    --merge\n",
                "\n",
                "merged_dir = f\"/content/VieNeu-TTS/finetune/output/{RUN_ID}/merged\"\n",
                "assert os.path.exists(merged_dir), f\"Huấn luyện thất bại! Không tìm thấy {merged_dir}. Vui lòng kiểm tra lỗi bên trên.\"\n",
                "print(f\"✅ Huấn luyện hoàn tất! Trọng số merged đã lưu tại: {merged_dir}\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "### A2. Đóng Gói Voice Preset Vào `voices_v3_turbo.json`\n",
                "Trích xuất vector giọng và lưu preset cấu hình giọng vào file JSON."
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "import glob\n",
                "\n",
                "%cd /content/VieNeu-TTS\n",
                "# Chọn 1 file audio mẫu chuẩn nhất (3-8 giây) từ dataset làm âm thanh tham chiếu\n",
                "audio_candidates = sorted(glob.glob(\"finetune/dataset/raw_audio/*.wav\"))\n",
                "ref_audio = audio_candidates[0] if audio_candidates else \"voices/ref.wav\"\n",
                "print(f\"Sử dụng file audio tham chiếu: {ref_audio}\")\n",
                "\n",
                "!python finetune/make_voice.py \\\n",
                "    --audio \"{ref_audio}\" \\\n",
                "    --name \"{VOICE_NAME}\" \\\n",
                "    --description \"Nam · Bắc · Kỹ sư trường đua\" \\\n",
                "    --gender male \\\n",
                "    --out finetune/output/{RUN_ID}/merged\n",
                "\n",
                "print(\"✅ Đã tạo cấu hình voice preset thành công!\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "### A3. Chuyển Đổi Model Đã Fine-Tune Sang Định Dạng C++ Native (`backbone.gguf` & `vieneu_v3_heads.npz`)\n",
                "Đoạn mã sau sẽ tự động đọc trọng số PyTorch/Safetensors đã merge của bạn và chuyển đổi sang chuẩn GGUF (chạy trên engine C++ Vulkan/DirectML của RaceEngineer với độ trễ cực thấp)."
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "import os, json, shutil\n",
                "from pathlib import Path\n",
                "import safetensors.torch\n",
                "import numpy as np\n",
                "from gguf import GGUFWriter\n",
                "\n",
                "EXPORT_OUT_DIR = Path(\"/content/RaceEngineer_CustomModel\")\n",
                "EXPORT_OUT_DIR.mkdir(parents=True, exist_ok=True)\n",
                "\n",
                "MERGED_DIR = Path(f\"/content/VieNeu-TTS/finetune/output/{RUN_ID}/merged\")\n",
                "safetensors_path = MERGED_DIR / \"model.safetensors\"\n",
                "if not safetensors_path.exists():\n",
                "    safetensors_path = Path(\"/content/base_model/update/model.safetensors\")\n",
                "    if not safetensors_path.exists():\n",
                "        safetensors_path = Path(\"/content/base_model/model.safetensors\")\n",
                "\n",
                "config_path = MERGED_DIR / \"config.json\"\n",
                "if not config_path.exists():\n",
                "    config_path = Path(\"/content/base_model/update/config.json\")\n",
                "    if not config_path.exists():\n",
                "        config_path = Path(\"/content/base_model/config.json\")\n",
                "\n",
                "print(f\"1. Đang nạp trọng số từ: {safetensors_path}\")\n",
                "sd = {k: v.float() for k, v in safetensors.torch.load_file(str(safetensors_path)).items()}\n",
                "cfg = json.loads(config_path.read_text(encoding=\"utf-8\"))\n",
                "\n",
                "# --- Xuất backbone.gguf ---\n",
                "print(\"2. Đang xuất backbone.gguf...\")\n",
                "hidden_size = int(cfg.get(\"hidden_size\", 768))\n",
                "num_layers = int(cfg.get(\"num_hidden_layers\", 12))\n",
                "num_heads = int(cfg.get(\"num_attention_heads\", 12))\n",
                "num_kv_heads = int(cfg.get(\"num_key_value_heads\", 4))\n",
                "intermediate_size = int(cfg.get(\"intermediate_size\", 3072))\n",
                "rope_theta = float(cfg.get(\"rope_theta\", 10000.0))\n",
                "text_vocab_size = sd[\"text_embeddings.weight\"].shape[0]\n",
                "\n",
                "gguf_path = EXPORT_OUT_DIR / \"backbone.gguf\"\n",
                "writer = GGUFWriter(str(gguf_path), \"qwen3\")\n",
                "writer.add_name(\"VieNeu v3 Turbo Custom Fine-tuned Backbone\")\n",
                "writer.add_block_count(num_layers)\n",
                "writer.add_context_length(2048)\n",
                "writer.add_embedding_length(hidden_size)\n",
                "writer.add_feed_forward_length(intermediate_size)\n",
                "writer.add_head_count(num_heads)\n",
                "writer.add_head_count_kv(num_kv_heads)\n",
                "writer.add_layer_norm_rms_eps(1e-6)\n",
                "writer.add_rope_freq_base(rope_theta)\n",
                "\n",
                "dummy_tokens = [f\"<t_{i}>\" for i in range(text_vocab_size)]\n",
                "writer.add_tokenizer_model(\"llama\")\n",
                "writer.add_token_list(dummy_tokens)\n",
                "writer.add_token_scores([0.0] * text_vocab_size)\n",
                "writer.add_token_types([1] * text_vocab_size)\n",
                "\n",
                "text_emb = sd[\"text_embeddings.weight\"].cpu().numpy()\n",
                "writer.add_tensor(\"token_embd.weight\", text_emb)\n",
                "writer.add_tensor(\"output.weight\", text_emb)\n",
                "\n",
                "for i in range(num_layers):\n",
                "    writer.add_tensor(f\"blk.{i}.attn_norm.weight\", sd[f\"semantic_backbone.layers.{i}.input_layernorm.weight\"].cpu().numpy())\n",
                "    writer.add_tensor(f\"blk.{i}.ffn_norm.weight\", sd[f\"semantic_backbone.layers.{i}.post_attention_layernorm.weight\"].cpu().numpy())\n",
                "    writer.add_tensor(f\"blk.{i}.attn_q.weight\", sd[f\"semantic_backbone.layers.{i}.self_attn.q_proj.weight\"].cpu().numpy())\n",
                "    writer.add_tensor(f\"blk.{i}.attn_k.weight\", sd[f\"semantic_backbone.layers.{i}.self_attn.k_proj.weight\"].cpu().numpy())\n",
                "    writer.add_tensor(f\"blk.{i}.attn_v.weight\", sd[f\"semantic_backbone.layers.{i}.self_attn.v_proj.weight\"].cpu().numpy())\n",
                "    writer.add_tensor(f\"blk.{i}.attn_output.weight\", sd[f\"semantic_backbone.layers.{i}.self_attn.o_proj.weight\"].cpu().numpy())\n",
                "\n",
                "    for qk in [\"q_norm\", \"k_norm\"]:\n",
                "        k = f\"semantic_backbone.layers.{i}.self_attn.{qk}.weight\"\n",
                "        if k in sd:\n",
                "            writer.add_tensor(f\"blk.{i}.attn_{qk}.weight\", sd[k].cpu().numpy())\n",
                "\n",
                "    writer.add_tensor(f\"blk.{i}.ffn_gate.weight\", sd[f\"semantic_backbone.layers.{i}.mlp.gate_proj.weight\"].cpu().numpy())\n",
                "    writer.add_tensor(f\"blk.{i}.ffn_up.weight\", sd[f\"semantic_backbone.layers.{i}.mlp.up_proj.weight\"].cpu().numpy())\n",
                "    writer.add_tensor(f\"blk.{i}.ffn_down.weight\", sd[f\"semantic_backbone.layers.{i}.mlp.down_proj.weight\"].cpu().numpy())\n",
                "\n",
                "writer.add_tensor(\"output_norm.weight\", sd[\"semantic_backbone.norm.weight\"].cpu().numpy())\n",
                "writer.write_header_to_file()\n",
                "writer.write_kv_data_to_file()\n",
                "writer.write_tensors_to_file()\n",
                "writer.close()\n",
                "print(\"✓ Xuất backbone.gguf thành công!\")\n",
                "\n",
                "# --- Xuất vieneu_v3_heads.npz ---\n",
                "print(\"3. Đang xuất vieneu_v3_heads.npz...\")\n",
                "heads_path = EXPORT_OUT_DIR / \"vieneu_v3_heads.npz\"\n",
                "audio_emb = np.stack([sd[f\"audio_embeddings.{ch}.weight\"].cpu().numpy() for ch in range(16)], axis=0)\n",
                "heads_payload = {\"text_emb\": text_emb, \"audio_emb\": audio_emb}\n",
                "if \"xvec_proj.0.weight\" in sd:\n",
                "    heads_payload[\"xvec_w\"] = sd[\"xvec_proj.0.weight\"].cpu().numpy()\n",
                "    heads_payload[\"xvec_b\"] = sd[\"xvec_proj.0.bias\"].cpu().numpy()\n",
                "    heads_payload[\"xvec_ln_w\"] = sd[\"xvec_proj.1.weight\"].cpu().numpy()\n",
                "    heads_payload[\"xvec_ln_b\"] = sd[\"xvec_proj.1.bias\"].cpu().numpy()\n",
                "    heads_payload[\"xvec_ln_eps\"] = np.asarray([1.0e-5], dtype=np.float32)\n",
                "np.savez(heads_path, **heads_payload)\n",
                "print(\"✓ Xuất vieneu_v3_heads.npz thành công!\")\n",
                "\n",
                "# --- Sao chép voices_v3_turbo.json ---\n",
                "merged_voices = MERGED_DIR / \"voices_v3_turbo.json\"\n",
                "repo_voices = Path(\"/content/VieNeu-TTS/src/vieneu/assets/voices_v3_turbo.json\")\n",
                "target_voices = EXPORT_OUT_DIR / \"voices_v3_turbo.json\"\n",
                "\n",
                "if merged_voices.exists():\n",
                "    shutil.copy(merged_voices, target_voices)\n",
                "    print(\"✓ Đã sao chép voices_v3_turbo.json từ thư mục merged (có chứa preset mới)!\")\n",
                "elif repo_voices.exists():\n",
                "    shutil.copy(repo_voices, target_voices)\n",
                "    print(\"✓ Đã sao chép voices_v3_turbo.json từ VieNeu-TTS repo assets!\")\n",
                "else:\n",
                "    import urllib.request\n",
                "    url = \"https://raw.githubusercontent.com/pnnbao97/VieNeu-TTS/main/src/vieneu/assets/voices_v3_turbo.json\"\n",
                "    urllib.request.urlretrieve(url, target_voices)\n",
                "    print(\"✓ Đã tải voices_v3_turbo.json từ GitHub!\")\n",
                "\n",
                "print(f\"🎉 Tất cả file C++ native đã được tạo tại: {EXPORT_OUT_DIR}\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "---\n",
                "# ⚡ PHƯƠNG ÁN B: Trích Xuất Preset Siêu Tốc (Không Cần Train LoRA)\n",
                "*(Chạy mất 1-2 phút, giữ nguyên model gốc `backbone.gguf`, chỉ thêm giọng vào `voices_v3_turbo.json`)*\n",
                "\n",
                "Phương pháp này tính vector trung bình (centroid averaging) trên toàn bộ file audio trong dataset của bạn để tạo ra giọng nói chuẩn xác nhất mà không cần train lại model."
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "import os, json, glob\n",
                "from pathlib import Path\n",
                "import numpy as np\n",
                "import onnxruntime as ort\n",
                "import soundfile as sf\n",
                "import librosa\n",
                "\n",
                "FAST_VOICE_NAME = \"Kỹ sư Mới (Clone)\"\n",
                "FAST_VOICE_DESC = \"Nam · Tự nhiên · Trích xuất nhanh\"\n",
                "\n",
                "print(\"1. Đang nạp mô hình trích xuất speaker encoder...\")\n",
                "spk_enc_path = \"/content/base_model/speaker_encoder.onnx\"\n",
                "spk_session = ort.InferenceSession(spk_enc_path, providers=['CPUExecutionProvider'])\n",
                "\n",
                "audio_files = sorted(glob.glob(\"/content/dataset/raw_audio/*.wav\"))\n",
                "assert len(audio_files) > 0, \"Không tìm thấy file audio nào trong /content/dataset/raw_audio/!\"\n",
                "\n",
                "embeddings = []\n",
                "print(f\"2. Đang trích xuất vector đặc trưng từ {len(audio_files)} mẫu âm thanh...\")\n",
                "for fpath in audio_files:\n",
                "    wav, sr = librosa.load(fpath, sr=16000)\n",
                "    if len(wav) < 16000: # Ít nhất 1 giây\n",
                "        continue\n",
                "    # Extract mel\n",
                "    mel = librosa.feature.melspectrogram(y=wav, sr=16000, n_fft=512, hop_length=160, n_mels=80)\n",
                "    log_mel = np.log(np.clip(mel, a_min=1e-5, a_max=None)).T[np.newaxis, :, :].astype(np.float32)\n",
                "    # Infer ONNX\n",
                "    inp_name = spk_session.get_inputs()[0].name\n",
                "    emb = spk_session.run(None, {inp_name: log_mel})[0].squeeze()\n",
                "    # L2 normalize\n",
                "    norm = np.linalg.norm(emb)\n",
                "    if norm > 0:\n",
                "        embeddings.append(emb / norm)\n",
                "\n",
                "assert len(embeddings) > 0, \"Không trích xuất được embedding hợp lệ nào!\"\n",
                "avg_emb = np.mean(embeddings, axis=0)\n",
                "avg_emb = avg_emb / np.linalg.norm(avg_emb)\n",
                "print(f\"✓ Trích xuất thành công vector 192 chiều từ {len(embeddings)} mẫu!\")\n",
                "\n",
                "# Đọc và bổ sung vào voices_v3_turbo.json\n",
                "voices_json_path = \"/content/base_model/voices_v3_turbo.json\"\n",
                "with open(voices_json_path, \"r\", encoding=\"utf-8\") as f:\n",
                "    voices_data = json.load(f)\n",
                "\n",
                "# Lấy mã codec tham chiếu từ 1 voice có sẵn hoặc file chuẩn\n",
                "sample_codes = voices_data[\"presets\"][\"Minh Đức\"][\"codes\"][:15] # 15 frames mẫu\n",
                "\n",
                "voices_data[\"presets\"][FAST_VOICE_NAME] = {\n",
                "    \"description\": FAST_VOICE_DESC,\n",
                "    \"gender\": \"male\",\n",
                "    \"style\": \"tu_nhien\",\n",
                "    \"speaker_emb\": [round(float(x), 6) for x in avg_emb],\n",
                "    \"codes\": sample_codes\n",
                "}\n",
                "\n",
                "os.makedirs(\"/content/RaceEngineer_FastPreset\", exist_ok=True)\n",
                "out_fast_json = \"/content/RaceEngineer_FastPreset/voices_v3_turbo.json\"\n",
                "with open(out_fast_json, \"w\", encoding=\"utf-8\") as f:\n",
                "    json.dump(voices_data, f, ensure_ascii=False, indent=1)\n",
                "\n",
                "print(f\"✅ Đã ghi voice preset mới vào: {out_fast_json}\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "## 💾 Bước 6: Đóng Gói File ZIP & Tải Về Máy Tính"
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "import shutil\n",
                "from google.colab import files\n",
                "\n",
                "# Kiểm tra nếu vừa chạy Phương án A hay Phương án B\n",
                "if os.path.exists(\"/content/RaceEngineer_CustomModel/backbone.gguf\"):\n",
                "    zip_target = \"/content/RaceEngineer_CustomModel\"\n",
                "    zip_name = f\"RaceEngineer_VieNeu_{RUN_ID}\"\n",
                "    print(\"📦 Đang nén kết quả của Phương án A (Fine-tuned Model + Preset)...\")\n",
                "else:\n",
                "    zip_target = \"/content/RaceEngineer_FastPreset\"\n",
                "    zip_name = \"RaceEngineer_VieNeu_FastPreset\"\n",
                "    print(\"📦 Đang nén kết quả của Phương án B (Voice Preset JSON)...\")\n",
                "\n",
                "archive_path = shutil.make_archive(zip_name, \"zip\", zip_target)\n",
                "print(f\"✅ Đã tạo file: {archive_path}\")\n",
                "print(\"⬇️ Trình duyệt sẽ tự động tải file ZIP về máy bạn...\")\n",
                "files.download(archive_path)"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "---\n",
                "## 🏁 Bước 7: Hướng Dẫn Nạp Giọng Mới Vào RaceEngineer Trên PC\n",
                "\n",
                "Sau khi tải file `.zip` về máy tính của bạn, hãy làm theo các bước sau:\n",
                "\n",
                "### Nếu bạn chạy Phương án A (Fine-tune LoRA):\n",
                "1. Giải nén file `.zip`. Bạn sẽ thấy 3 file: `backbone.gguf`, `vieneu_v3_heads.npz`, và `voices_v3_turbo.json`.\n",
                "2. Copy đè cả 3 file này vào thư mục sau trong dự án RaceEngineer:\n",
                "   - `RaceEngineer\\models\\vieneu-v3\\`\n",
                "   - *(Và thư mục `RaceEngineer\\build\\models\\vieneu-v3\\` nếu bạn đang chạy trực tiếp từ build)*.\n",
                "3. Mở ứng dụng **RaceEngineer**.\n",
                "4. Vào tab **Cài đặt** hoặc **Kỹ sư** $\\rightarrow$ Tại mục **Giọng nói Kỹ sư**, bạn sẽ thấy giọng mới vừa train xuất hiện trong menu chọn! Chọn giọng mới và bắt đầu đua xe!\n",
                "\n",
                "### Nếu bạn chạy Phương án B (Trích xuất Preset siêu tốc):\n",
                "1. Giải nén file `.zip`, bạn sẽ thấy file `voices_v3_turbo.json`.\n",
                "2. Copy đè file `voices_v3_turbo.json` vào thư mục `models\\vieneu-v3\\` (giữ nguyên `backbone.gguf` gốc).\n",
                "3. Mở ứng dụng **RaceEngineer** $\\rightarrow$ Giọng mới sẽ tự động hiển thị trong danh sách lựa chọn!"
            ]
        }
    ]

    notebook = {
        "cells": cells,
        "metadata": {
            "colab": {
                "name": "Huấn Luyện & Tích Hợp Giọng VieNeu-TTS v3 Turbo Cho RaceEngineer",
                "provenance": []
            },
            "kernelspec": {
                "display_name": "Python 3",
                "name": "python3"
            },
            "language_info": {
                "name": "python"
            },
            "accelerator": "GPU"
        },
        "nbformat": 4,
        "nbformat_minor": 5
    }

    out_path = Path("training/vieneu/train_vieneu_colab.ipynb")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(notebook, f, ensure_ascii=False, indent=2)
    print(f"Created notebook at {out_path.resolve()}")

    # Also copy to scripts for easy access
    scripts_path = Path("scripts/train_vieneu_colab.ipynb")
    shutil.copy(out_path, scripts_path)
    print(f"Copied notebook to {scripts_path.resolve()}")

if __name__ == "__main__":
    create_notebook()
