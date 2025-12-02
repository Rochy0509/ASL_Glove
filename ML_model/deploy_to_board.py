"""Deploy trained model to ESP32."""
import os
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'

import sys
import subprocess
from pathlib import Path

print("Deploying model to ESP32...\n")

scripts = [
    ("Converting to TFLite", "convert_to_tflite.py"),
    ("Updating firmware code", "update_firmware.py"),
    ("Updating normalization", "update_normalization.py")
]

for step, (desc, script) in enumerate(scripts, 1):
    print(f"[{step}/{len(scripts)}] {desc}...")
    result = subprocess.run([sys.executable, script], capture_output=False, text=True)
    if result.returncode != 0 and step < 3:
        print(f"Error in {script}")
        sys.exit(1)

print("\nReady to flash. Flash now? (y/n): ", end='')
if input().strip().lower() == 'y':
    firmware_dir = Path(__file__).parent.parent / "ASL_firmware"
    result = subprocess.run(["pio", "run", "-t", "upload"], cwd=firmware_dir)
    print("\nDeployment complete!" if result.returncode == 0 else "\nFlash failed")
else:
    print("\nTo flash manually: cd ../ASL_firmware && pio run -t upload")
