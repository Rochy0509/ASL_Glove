"""Train model with HELLO and EAT gestures."""
import os
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent / 'src'))

import pandas as pd
import numpy as np
import tensorflow as tf
from sklearn.preprocessing import LabelEncoder
from src.data_processing.data_preprocessing import SensorNormalizer
from src.core.model import create_model

np.random.seed(42)
tf.random.set_seed(42)

PROJECT_ROOT = Path(__file__).parent.parent
print("Loading data...")
df_hello = pd.read_csv(PROJECT_ROOT / "python/data_logs/P1HELLO_data.csv").iloc[40:-15]
df_eat = pd.read_csv(PROJECT_ROOT / "python/data_logs/P1EAT_data.csv").iloc[40:-15]
df = pd.concat([df_hello, df_eat], ignore_index=True)

normalizer = SensorNormalizer()
imu_cols = ['ax', 'ay', 'az', 'gx', 'gy', 'gz']
df = normalizer.fit_transform(df, imu_cols, method='standardize')

feature_cols = ['flex1', 'flex2', 'flex3', 'flex4', 'flex5'] + imu_cols
X, y = df[feature_cols].values, df['label'].values

def create_windows(data, labels, window_size):
    windows, window_labels = [], []
    for label in np.unique(labels):
        label_data = data[labels == label]
        for i in range(len(label_data) - window_size + 1):
            windows.append(label_data[i:i+window_size])
            window_labels.append(label)
    return np.array(windows), np.array(window_labels)

window_size = 25
X_windows, y_labels = create_windows(X, y, window_size)

le = LabelEncoder()
y_encoded = le.fit_transform(y_labels)
y_cat = tf.keras.utils.to_categorical(y_encoded, len(le.classes_))

split = int(0.8 * len(X_windows))
X_train, X_test = X_windows[:split], X_windows[split:]
y_train, y_test = y_cat[:split], y_cat[split:]

print(f"Training on {len(X_train)} samples, testing on {len(X_test)}")
print(f"Classes: {le.classes_}")

model = create_model(
    model_type='cnn_small',
    num_classes=len(le.classes_),
    window_size=window_size,
    num_features=11,
    l2_reg=0.001,
    dropout_rate=0.5,
    verbose=False
)

model.compile(optimizer='adam', loss='categorical_crossentropy', metrics=['accuracy'])

history = model.fit(
    X_train, y_train,
    validation_data=(X_test, y_test),
    batch_size=32,
    epochs=50,
    verbose=1
)

loss, acc = model.evaluate(X_test, y_test, verbose=0)
print(f"\nTest accuracy: {acc*100:.2f}%")

output_dir = PROJECT_ROOT / "ML_model/model/hello_eat_simple"
output_dir.mkdir(parents=True, exist_ok=True)
model.save(output_dir / 'model.keras')
np.save(output_dir / 'classes.npy', le.classes_)
normalizer.save_params(str(PROJECT_ROOT / "ML_model/data/normalization_params_hello_eat.json"))

print(f"Model saved to {output_dir}")
