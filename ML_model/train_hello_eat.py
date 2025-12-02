"""Train model with only HELLO and EAT data."""
import os
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent / 'src'))

import pandas as pd
import numpy as np
from src.data_processing.data_preprocessing import SensorNormalizer

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
DATA_DIR = PROJECT_ROOT / "python" / "data_logs"
OUTPUT_DIR = PROJECT_ROOT / "ML_model" / "data"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

print("="*70)
print("PREPARING HELLO AND EAT DATASET")
print("="*70)

# Load only HELLO and EAT CSV files
hello_file = DATA_DIR / "P1HELLO_data.csv"
eat_file = DATA_DIR / "P1EAT_data.csv"

print(f"\nLoading files:")
print(f"  - {hello_file}")
print(f"  - {eat_file}")

df_hello = pd.read_csv(hello_file)
df_eat = pd.read_csv(eat_file)

print(f"\nOriginal sizes:")
print(f"  HELLO: {len(df_hello)} rows")
print(f"  EAT: {len(df_eat)} rows")

# Trim unstable start/end portions
trim_start = 40
trim_end = 15

df_hello_trimmed = df_hello.iloc[trim_start:-trim_end]
df_eat_trimmed = df_eat.iloc[trim_start:-trim_end]

print(f"\nAfter trimming (first {trim_start}, last {trim_end} rows):")
print(f"  HELLO: {len(df_hello_trimmed)} rows")
print(f"  EAT: {len(df_eat_trimmed)} rows")

# Combine dataframes
combined_df = pd.concat([df_hello_trimmed, df_eat_trimmed], ignore_index=True)
print(f"\nCombined: {len(combined_df)} total rows")

# Define columns
imu_columns = ['ax', 'ay', 'az', 'gx', 'gy', 'gz']
flex_columns = ['flex1', 'flex2', 'flex3', 'flex4', 'flex5']

# Fit normalizer on IMU data
print("\nNormalizing IMU data...")
normalizer = SensorNormalizer()
normalizer.fit(combined_df, imu_columns)

# Transform the data
combined_df_normalized = normalizer.transform(combined_df, method='standardize')

# Drop person_id if exists
if 'person_id' in combined_df_normalized.columns:
    combined_df_normalized = combined_df_normalized.drop(columns=['person_id'])

# Rename label to target and move to end
if 'label' in combined_df_normalized.columns:
    label_data = combined_df_normalized['label']
    combined_df_normalized = combined_df_normalized.drop(columns=['label'])
    combined_df_normalized['target'] = label_data

# Save combined dataset
output_file = OUTPUT_DIR / "hello_eat_dataset.csv"
combined_df_normalized.to_csv(output_file, index=False)
print(f"\nDataset saved to: {output_file}")
print(f"Shape: {combined_df_normalized.shape}")
print(f"Columns: {list(combined_df_normalized.columns)}")
print(f"Classes: {combined_df_normalized['target'].unique()}")

# Save normalization parameters
params_file = OUTPUT_DIR / "normalization_params_hello_eat.json"
normalizer.save_params(str(params_file))

print("\n" + "="*70)
print("STARTING TRAINING")
print("="*70)

# Now train the model
import tensorflow as tf
from src.core.train import (
    TrainingConfig, load_and_prepare_data, shuffle_train_data,
    augment_data, compute_class_weights_dict, create_callbacks,
    compile_model, plot_training_history, evaluate_model
)
from src.core.model import create_model

# Set random seeds
np.random.seed(42)
tf.random.set_seed(42)

# Create custom config
config = TrainingConfig()
config.data_file = output_file
config.num_features = 11  # 5 flex + 6 IMU

# Load and prepare data
X_train, X_test, y_train, y_test, label_encoder, num_classes = load_and_prepare_data(config)

# Apply data augmentation
if config.use_augmentation:
    X_train, y_train = augment_data(X_train, y_train, config, augmentation_factor=0.5)

# Shuffle training data
X_train, y_train = shuffle_train_data(X_train, y_train, config.random_seed)

# Class weights
class_weights = None
if config.use_class_weights:
    class_weights = compute_class_weights_dict(y_train, num_classes)

# Create model
print("\n" + "="*70)
print("CREATING MODEL")
print("="*70)
model = create_model(
    model_type=config.model_type,
    num_classes=num_classes,
    window_size=config.window_size,
    num_features=config.num_features,
    l2_reg=config.l2_reg,
    dropout_rate=config.dropout_rate,
    verbose=True
)

# Compile model
model = compile_model(model, config)

# Callbacks
callback_list = create_callbacks(config)

# Save configuration
config.save(config.output_dir / 'config.json')
print(f"\nConfiguration saved to {config.output_dir / 'config.json'}")

# Train
print("\n" + "="*70)
print("TRAINING")
print("="*70)

history = model.fit(
    X_train, y_train,
    validation_data=(X_test, y_test),
    batch_size=config.batch_size,
    epochs=config.epochs,
    class_weight=class_weights,
    callbacks=callback_list,
    verbose=1
)

# Plot history
plot_training_history(history, config.output_dir)

# Evaluate
evaluate_model(model, X_test, y_test, label_encoder, config.output_dir)

# Save final model
model.save(config.output_dir / 'final_model.keras')
print(f"\nFinal model saved to {config.output_dir / 'final_model.keras'}")

# Save label encoder
np.save(config.output_dir / 'label_encoder_classes.npy', label_encoder.classes_)
print(f"Label encoder saved to {config.output_dir / 'label_encoder_classes.npy'}")

print("\n" + "="*70)
print("TRAINING COMPLETE!")
print("="*70)
print(f"\nAll outputs saved to: {config.output_dir}")
print(f"\nModel classes: {label_encoder.classes_}")
