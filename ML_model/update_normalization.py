"""Update firmware normalization parameters."""
import json
from pathlib import Path

params_path = Path(__file__).parent / "data" / "normalization_params_hello_eat.json"
if not params_path.exists():
    print("No normalization params found, skipping")
    exit(0)

params = json.load(params_path.open())
print("Updating normalization parameters...")

code = """#ifndef IMU_NORMALIZATION_H_
#define IMU_NORMALIZATION_H_

struct NormParams {
    float mean;
    float std;
};

"""

for sensor in ['ax', 'ay', 'az', 'gx', 'gy', 'gz']:
    if sensor in params:
        mean, std = params[sensor]['mean'], params[sensor]['std']
        code += f"constexpr NormParams k{sensor.capitalize()}Params = {{{mean:.6f}f, {std:.6f}f}};\n"

code += """
inline float normalizeSensor(float value, const NormParams& p) {
    return (value - p.mean) / p.std;
}

#endif  // IMU_NORMALIZATION_H_"""

output_path = Path(__file__).parent.parent / "ASL_firmware/src/ml/imu_normalization.h"
output_path.write_text(code)
print("Normalization updated successfully")
