"""Data preprocessing for ASL Glove sensor data with IMU normalization."""

import pandas as pd
import numpy as np
import json
from pathlib import Path
from typing import Dict, Tuple, List


class SensorNormalizer:
    """Normalize sensor data for training and inference consistency."""

    def __init__(self):
        self.params = {}
        self.is_fitted = False

    def fit(self, df: pd.DataFrame, columns: List[str]):
        """Calculate normalization parameters from training data."""
        self.params = {}
        for col in columns:
            if col in df.columns:
                self.params[col] = {
                    'mean': float(df[col].mean()),
                    'std': float(df[col].std()),
                    'min': float(df[col].min()),
                    'max': float(df[col].max())
                }
        self.is_fitted = True
        return self

    def transform(self, df: pd.DataFrame, method='standardize') -> pd.DataFrame:
        """Apply normalization using fitted parameters."""
        if not self.is_fitted:
            raise ValueError("Normalizer must be fitted before transform. Call fit() first.")

        df_norm = df.copy()
        for col, params in self.params.items():
            if col in df_norm.columns:
                if method == 'standardize':
                    df_norm[col] = (df_norm[col] - params['mean']) / params['std']
                elif method == 'minmax':
                    df_norm[col] = (df_norm[col] - params['min']) / (params['max'] - params['min'])
        return df_norm

    def fit_transform(self, df: pd.DataFrame, columns: List[str], method='standardize') -> pd.DataFrame:
        """Fit and transform in one step."""
        self.fit(df, columns)
        return self.transform(df, method)

    def save_params(self, filepath: str):
        """Save normalization parameters to JSON."""
        if not self.is_fitted:
            raise ValueError("No parameters to save. Fit the normalizer first.")
        with open(filepath, 'w') as f:
            json.dump(self.params, f, indent=2)
        print(f"Normalization parameters saved to {filepath}")

    def load_params(self, filepath: str):
        """Load normalization parameters from JSON."""
        with open(filepath, 'r') as f:
            self.params = json.load(f)
        self.is_fitted = True
        print(f"Normalization parameters loaded from {filepath}")
        return self

    def get_params_for_firmware(self) -> Dict:
        """Get parameters formatted for firmware."""
        if not self.is_fitted:
            raise ValueError("No parameters available. Fit the normalizer first.")
        firmware_params = {}
        for sensor, params in self.params.items():
            firmware_params[sensor] = {
                'mean': params['mean'],
                'std': params['std']
            }
        return firmware_params


def load_csv_data(data_dir: str, trim_start=40, trim_end=15) -> pd.DataFrame:
    """Load CSV files and remove unstable start/end rows from each file."""
    csv_files = list(Path(data_dir).glob('*.csv'))
    if not csv_files:
        raise ValueError(f"No CSV files found in {data_dir}")

    dfs = []
    total_trimmed = 0
    for csv_file in csv_files:
        df = pd.read_csv(csv_file)
        original_len = len(df)
        df = df.iloc[trim_start:-trim_end] if trim_end > 0 else df.iloc[trim_start:]
        total_trimmed += (original_len - len(df))
        dfs.append(df)

    combined_df = pd.concat(dfs, ignore_index=True)
    print(f"Loaded {len(csv_files)} files, trimmed {total_trimmed} rows, {len(combined_df)} samples remaining")
    return combined_df


def preprocess_for_cnn(df: pd.DataFrame, normalizer: SensorNormalizer = None,
                       normalize_method='standardize') -> Tuple[np.ndarray, np.ndarray, SensorNormalizer]:
    """Preprocess sensor data for CNN training with normalization."""
    imu_columns = ['ax', 'ay', 'az', 'gx', 'gy', 'gz']
    flex_columns = ['flex1', 'flex2', 'flex3', 'flex4', 'flex5']

    if normalizer is None:
        normalizer = SensorNormalizer()
        df_processed = normalizer.fit_transform(df, imu_columns, method=normalize_method)
    else:
        df_processed = normalizer.transform(df, method=normalize_method)

    feature_columns = flex_columns + imu_columns
    X = df_processed[feature_columns].values
    y = df_processed['label'].values

    print(f"Preprocessed data shape: {X.shape}")
    print(f"Unique labels: {len(np.unique(y))}")
    return X, y, normalizer


def print_normalization_stats(normalizer: SensorNormalizer):
    """Print normalization statistics."""
    print("\n" + "="*60)
    print("NORMALIZATION PARAMETERS")
    print("="*60)
    for sensor, params in normalizer.params.items():
        print(f"{sensor}: mean={params['mean']:.4f}, std={params['std']:.4f}")
    print("="*60)


def generate_firmware_code(normalizer: SensorNormalizer) -> str:
    """Generate C++ normalization code for firmware."""
    params = normalizer.get_params_for_firmware()
    code = "// Normalization parameters - must match training\n"
    code += "struct NormParams { float mean; float std; };\n\n"

    for sensor in ['ax', 'ay', 'az', 'gx', 'gy', 'gz']:
        if sensor in params:
            code += f"const NormParams {sensor}_params = {{{params[sensor]['mean']:.6f}f, {params[sensor]['std']:.6f}f}};\n"

    code += "\nfloat normalize(float val, NormParams p) { return (val - p.mean) / p.std; }\n"
    return code


def create_combined_dataset(data_dir: str, output_file: str, trim_start=40, trim_end=15,
                           normalize_method='standardize') -> Tuple[pd.DataFrame, SensorNormalizer]:
    """Trim and normalize each CSV, combine into single dataset with target column at end."""
    csv_files = list(Path(data_dir).glob('*.csv'))
    if not csv_files:
        raise ValueError(f"No CSV files found in {data_dir}")

    print(f"Found {len(csv_files)} CSV files")

    # Define columns
    imu_columns = ['ax', 'ay', 'az', 'gx', 'gy', 'gz']
    flex_columns = ['flex1', 'flex2', 'flex3', 'flex4', 'flex5']

    # First pass: collect all data to fit normalizer
    print("\nFirst pass: collecting data for normalization parameters...")
    all_dfs = []
    total_trimmed = 0

    for csv_file in csv_files:
        df = pd.read_csv(csv_file)
        original_len = len(df)
        df = df.iloc[trim_start:-trim_end] if trim_end > 0 else df.iloc[trim_start:]
        total_trimmed += (original_len - len(df))
        all_dfs.append(df)
        print(f"  Loaded {csv_file.name}: {original_len} -> {len(df)} rows (trimmed {original_len - len(df)})")

    # Combine all data to fit normalizer
    combined_temp = pd.concat(all_dfs, ignore_index=True)
    print(f"\nTotal rows after trimming: {len(combined_temp)} (trimmed {total_trimmed} total)")

    # Fit normalizer on all IMU data
    normalizer = SensorNormalizer()
    normalizer.fit(combined_temp, imu_columns)
    print(f"\nFitted normalizer on {len(imu_columns)} IMU columns")

    # Second pass: normalize each dataframe
    print("\nSecond pass: normalizing data...")
    normalized_dfs = []
    for df in all_dfs:
        df_normalized = normalizer.transform(df, method=normalize_method)
        normalized_dfs.append(df_normalized)

    # Combine all normalized data
    combined_df = pd.concat(normalized_dfs, ignore_index=True)

    # Drop person_id if it exists
    if 'person_id' in combined_df.columns:
        combined_df = combined_df.drop(columns=['person_id'])
        print("Dropped 'person_id' column")

    # Move label column to end and rename to target
    if 'label' in combined_df.columns:
        label_data = combined_df['label']
        combined_df = combined_df.drop(columns=['label'])
        combined_df['target'] = label_data
        print("Moved 'label' column to end and renamed to 'target'")

    # Save combined dataset
    combined_df.to_csv(output_file, index=False)
    print(f"\nCombined dataset saved to: {output_file}")
    print(f"Final shape: {combined_df.shape}")
    print(f"Columns: {list(combined_df.columns)}")
    print(f"Unique targets: {combined_df['target'].nunique()}")

    return combined_df, normalizer


if __name__ == "__main__":
    project_root = Path(__file__).parent.parent.parent.parent
    data_dir = project_root / "python" / "data_logs"
    output_dir = project_root / "ML_model" / "src" / "data_processing"

    # Create combined dataset
    print("="*60)
    print("CREATING COMBINED DATASET")
    print("="*60)
    output_csv = project_root / "ML_model" / "data" / "combined_dataset.csv"
    output_csv.parent.mkdir(parents=True, exist_ok=True)

    combined_df, normalizer = create_combined_dataset(
        str(data_dir),
        str(output_csv),
        trim_start=40,
        trim_end=15,
        normalize_method='standardize'
    )

    # Print normalization stats
    print_normalization_stats(normalizer)

    # Save normalization parameters
    params_file = output_dir / "normalization_params.json"
    params_file.parent.mkdir(parents=True, exist_ok=True)
    normalizer.save_params(str(params_file))

    # Generate firmware code
    firmware_code = generate_firmware_code(normalizer)
    firmware_file = output_dir / "firmware_normalization.h"
    with open(firmware_file, 'w') as f:
        f.write(firmware_code)
    print(f"\nFirmware code saved to {firmware_file}")

    print("\n" + "="*60)
    print("COMPLETE!")
    print("="*60)
    print("\nGenerated files:")
    print(f"1. {output_csv} - Combined dataset")
    print(f"2. {params_file} - Normalization parameters")
    print(f"3. {firmware_file} - Firmware normalization code")
    print("\nNEXT STEPS:")
    print("1. Use combined_dataset.csv for model training")
    print("2. Use normalization_params.json for Python inference")
    print("3. Copy firmware_normalization.h to ESP32 project")
