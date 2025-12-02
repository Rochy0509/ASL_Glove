"""Update firmware inference code with new classes."""
import re
from pathlib import Path
import numpy as np

MODEL_BASE = Path(__file__).parent / "model"
latest_dir = max([d for d in MODEL_BASE.iterdir() if d.is_dir()], key=lambda x: x.stat().st_mtime)

for fname in ["label_encoder_classes.npy", "classes.npy"]:
    classes_path = latest_dir / fname
    if classes_path.exists():
        break

classes = np.load(classes_path)
num_classes = len(classes)

print(f"Updating firmware for {num_classes} classes: {list(classes)}")

firmware_path = Path(__file__).parent.parent / "ASL_firmware/src/ml/asl_inference.cpp"
content = firmware_path.read_text()

content = re.sub(
    r'constexpr size_t kNumClasses = \d+;',
    f'constexpr size_t kNumClasses = {num_classes};',
    content
)

label_names_str = ', '.join([f'"{cls}"' for cls in classes])
label_names_array = f'constexpr std::array<const char*, kNumClasses> kLabelNames = {{\n    {label_names_str}}};'

content = re.sub(
    r'constexpr std::array<const char\*, kNumClasses> kLabelNames = \{[^}]+\};',
    label_names_array,
    content,
    flags=re.DOTALL
)

label_mapping = {
    "EAT": "'E'", "HELLO": "'H'",
    "NEUTRAL": "ASLInferenceEngine::kNeutralToken",
    "NEUTR": "ASLInferenceEngine::kNeutralToken",
    "SPACE": "ASLInferenceEngine::kSpaceToken",
    "BACKSPACE": "ASLInferenceEngine::kBackspaceToken",
    "BACK": "ASLInferenceEngine::kBackspaceToken"
}

label_to_char = []
for cls in classes:
    if cls.upper() in label_mapping:
        label_to_char.append(label_mapping[cls.upper()])
    elif len(cls) == 1:
        label_to_char.append(f"'{cls.upper()}'")
    else:
        label_to_char.append(f"'{cls[0].upper()}'")

label_to_char_array = f"constexpr std::array<char, kNumClasses> kLabelToChar = {{\n    {',\n    '.join(label_to_char)}}};"

content = re.sub(
    r'constexpr std::array<char, kNumClasses> kLabelToChar = \{[^}]+\};',
    label_to_char_array,
    content,
    flags=re.DOTALL
)

firmware_path.write_text(content)
print("Firmware updated successfully")
