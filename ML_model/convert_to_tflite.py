"""Convert Keras model to TFLite and generate C array."""
import os
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'

from pathlib import Path
import numpy as np
import tensorflow as tf

MODEL_BASE = Path(__file__).parent / "model"
latest_dir = max([d for d in MODEL_BASE.iterdir() if d.is_dir()], key=lambda x: x.stat().st_mtime)

print(f"Converting model from {latest_dir.name}")

for fname in ["best_model.keras", "final_model.keras", "model.keras"]:
    model_path = latest_dir / fname
    if model_path.exists():
        break

model = tf.keras.models.load_model(model_path)

for fname in ["label_encoder_classes.npy", "classes.npy"]:
    classes_path = latest_dir / fname
    if classes_path.exists():
        break

classes = np.load(classes_path)
print(f"Classes: {list(classes)}")

def representative_dataset():
    for _ in range(100):
        yield [np.random.rand(1, 25, 11).astype(np.float32)]

converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_model = converter.convert()

tflite_path = latest_dir / "model_quantized.tflite"
tflite_path.write_bytes(tflite_model)
print(f"Saved TFLite model: {len(tflite_model)/1024:.1f} KB")

def generate_c_array(data, var_name):
    header = f"""#ifndef ASL_MODEL_DATA_H_
#define ASL_MODEL_DATA_H_

extern const unsigned char {var_name}[];
extern const int {var_name}_len;

#endif  // ASL_MODEL_DATA_H_"""

    formatted_lines = []
    for i in range(0, len(data), 12):
        line_bytes = [f'0x{b:02x}' for b in data[i:i+12]]
        formatted_lines.append('  ' + ', '.join(line_bytes) + ',')

    source = f"""#include "ml/asl_model_data.h"

alignas(8) const unsigned char {var_name}[] = {{
{chr(10).join(formatted_lines)}
}};

const int {var_name}_len = {len(data)};"""

    return header, source

header_content, source_content = generate_c_array(tflite_model, 'g_asl_model_data')

firmware_ml = Path(__file__).parent.parent / "ASL_firmware/src/ml"
(firmware_ml / "asl_model_data.h").write_text(header_content)
(firmware_ml / "asl_model_data.cc").write_text(source_content)

print("Generated firmware model files")
