"""Training script for ASL Glove CNN model with 80/20 split and metrics."""

import os
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'  # Reduce TF logging

import numpy as np
import pandas as pd
import tensorflow as tf
from tensorflow.keras import callbacks, optimizers, metrics
from sklearn.preprocessing import LabelEncoder
from sklearn.utils.class_weight import compute_class_weight
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path
import json
from datetime import datetime

try:
    from .model import create_model
except ImportError:
    # Allow running as standalone script (python train.py)
    from model import create_model


class TrainingConfig:
    """Configuration for training hyperparameters."""

    def __init__(self):
        # Data parameters
        self.window_size = 25  # 500ms at 50Hz
        self.num_features = 11  # 5 flex + 6 IMU
        self.test_size = 0.2  # 80/20 split
        self.random_seed = 42

        # Model parameters
        self.model_type = 'cnn_small'  # 'cnn_small', 'cnn_medium', or 'dense'
        self.l2_reg = 0.001  # Increased from 0.0001
        self.dropout_rate = 0.5  # Increased from 0.3

        # Training parameters
        self.batch_size = 32
        self.epochs = 100
        self.learning_rate = 0.001
        self.patience_early_stop = 10  # Reduced from 15
        self.patience_reduce_lr = 5  # Reduced from 7
        self.reduce_lr_factor = 0.5

        # Data augmentation
        self.use_augmentation = True
        self.aug_noise_level = 0.01  # Gaussian noise std
        self.aug_scale_range = (0.95, 1.05)  # Scaling factor range
        self.aug_time_jitter = 2  # Max samples to shift

        # Class imbalance handling
        self.use_class_weights = True

        # Paths
        self.project_root = Path(__file__).parent.parent.parent
        self.data_file = self.project_root / 'data' / 'combined_dataset.csv'
        self.output_dir = self.project_root / 'model' / datetime.now().strftime('%Y%m%d_%H%M%S')

    def save(self, filepath: str):
        """Save configuration to JSON."""
        config_dict = {k: str(v) if isinstance(v, Path) else v
                      for k, v in self.__dict__.items()}
        with open(filepath, 'w') as f:
            json.dump(config_dict, f, indent=2)


def load_and_prepare_data(config: TrainingConfig):
    """Load CSV data with sequence-based split to prevent data leakage."""
    print("\n" + "="*70)
    print("LOADING DATA (SEQUENCE-BASED SPLIT)")
    print("="*70)

    # Load dataset
    df = pd.read_csv(config.data_file)
    print(f"Loaded {len(df)} samples from {config.data_file.name}")
    print(f"Columns: {list(df.columns)}")

    # Extract features
    feature_cols = ['flex1', 'flex2', 'flex3', 'flex4', 'flex5',
                   'ax', 'ay', 'az', 'gx', 'gy', 'gz']

    # Group by target class to split sequences
    print(f"\nSplitting sequences by class (train/test = {(1-config.test_size)*100:.0f}/{config.test_size*100:.0f})...")

    train_windows_list = []
    train_labels_list = []
    test_windows_list = []
    test_labels_list = []

    # Get unique labels and sort for consistency
    unique_labels = sorted(df['target'].unique())

    for label in unique_labels:
        # Get all samples for this class
        class_df = df[df['target'] == label].reset_index(drop=True)
        class_samples = class_df[feature_cols].values

        # Split sequence: first 80% for train, last 20% for test
        split_idx = int(len(class_samples) * (1 - config.test_size))

        train_seq = class_samples[:split_idx]
        test_seq = class_samples[split_idx:]

        # Create windows from train sequence
        if len(train_seq) >= config.window_size:
            train_windows = create_windows(train_seq, config.window_size)
            train_windows_list.append(train_windows)
            train_labels_list.extend([label] * len(train_windows))

        # Create windows from test sequence
        if len(test_seq) >= config.window_size:
            test_windows = create_windows(test_seq, config.window_size)
            test_windows_list.append(test_windows)
            test_labels_list.extend([label] * len(test_windows))

        print(f"  {label:10s}: {len(class_samples):4d} samples → "
              f"train: {len(train_seq):4d} → {len(train_windows) if len(train_seq) >= config.window_size else 0:4d} windows, "
              f"test: {len(test_seq):4d} → {len(test_windows) if len(test_seq) >= config.window_size else 0:4d} windows")

    # Concatenate all windows
    X_train = np.concatenate(train_windows_list, axis=0)
    X_test = np.concatenate(test_windows_list, axis=0)
    y_train_labels = np.array(train_labels_list)
    y_test_labels = np.array(test_labels_list)

    print(f"\nTotal windows:")
    print(f"  Train: {len(X_train)} windows")
    print(f"  Test:  {len(X_test)} windows")

    # Encode labels
    label_encoder = LabelEncoder()
    label_encoder.fit(unique_labels)  # Fit on all labels

    y_train_encoded = label_encoder.transform(y_train_labels)
    y_test_encoded = label_encoder.transform(y_test_labels)

    num_classes = len(label_encoder.classes_)
    print(f"\nUnique classes: {num_classes}")
    print(f"Classes: {label_encoder.classes_}")

    # Convert to categorical
    y_train_categorical = tf.keras.utils.to_categorical(y_train_encoded, num_classes)
    y_test_categorical = tf.keras.utils.to_categorical(y_test_encoded, num_classes)

    # Calculate class distribution
    print("\nTrain class distribution:")
    unique, counts = np.unique(y_train_encoded, return_counts=True)
    for cls, count in zip(label_encoder.classes_[unique], counts):
        print(f"  {cls:10s}: {count:4d} windows")

    print("\nTest class distribution:")
    unique, counts = np.unique(y_test_encoded, return_counts=True)
    for cls, count in zip(label_encoder.classes_[unique], counts):
        print(f"  {cls:10s}: {count:4d} windows")

    return X_train, X_test, y_train_categorical, y_test_categorical, label_encoder, num_classes


def create_windows(data, window_size):
    """Create sliding windows from flat sequential data."""
    n_samples = len(data)
    n_windows = n_samples - window_size + 1

    windows = np.array([data[i:i+window_size] for i in range(n_windows)])
    return windows


def shuffle_train_data(X_train, y_train, random_seed=42):
    """Shuffle training data for better batch diversity."""
    np.random.seed(random_seed)
    indices = np.random.permutation(len(X_train))
    return X_train[indices], y_train[indices]


def augment_window(window, config: TrainingConfig):
    """Apply data augmentation to a single window."""
    aug_window = window.copy()

    # 1. Add Gaussian noise
    if np.random.rand() < 0.5:
        noise = np.random.normal(0, config.aug_noise_level, window.shape)
        aug_window = aug_window + noise

    # 2. Scale features (simulate sensor variation)
    if np.random.rand() < 0.5:
        scale = np.random.uniform(*config.aug_scale_range)
        aug_window = aug_window * scale

    # 3. Time jittering (shift window slightly)
    if np.random.rand() < 0.3 and config.aug_time_jitter > 0:
        jitter = np.random.randint(-config.aug_time_jitter, config.aug_time_jitter + 1)
        if jitter > 0:
            aug_window = np.concatenate([aug_window[jitter:], aug_window[-jitter:]], axis=0)
        elif jitter < 0:
            aug_window = np.concatenate([aug_window[:jitter], aug_window[:abs(jitter)]], axis=0)

    return aug_window


def augment_data(X, y, config: TrainingConfig, augmentation_factor=0.5):
    """Augment training data to reduce overfitting."""
    print(f"\nApplying data augmentation (factor={augmentation_factor})...")

    n_augment = int(len(X) * augmentation_factor)
    aug_indices = np.random.choice(len(X), n_augment, replace=False)

    X_aug_list = []
    y_aug_list = []

    for idx in aug_indices:
        aug_window = augment_window(X[idx], config)
        X_aug_list.append(aug_window)
        y_aug_list.append(y[idx])

    X_augmented = np.concatenate([X, np.array(X_aug_list)], axis=0)
    y_augmented = np.concatenate([y, np.array(y_aug_list)], axis=0)

    print(f"  Original: {len(X)} samples")
    print(f"  Augmented: {len(X_aug_list)} samples")
    print(f"  Total: {len(X_augmented)} samples")

    return X_augmented, y_augmented


def compute_class_weights_dict(y_train, num_classes):
    """Compute class weights to handle imbalanced dataset."""
    y_train_labels = y_train.argmax(axis=1)
    class_weights = compute_class_weight(
        class_weight='balanced',
        classes=np.unique(y_train_labels),
        y=y_train_labels
    )
    class_weight_dict = dict(enumerate(class_weights))

    print("\nClass weights (for imbalanced dataset):")
    for cls, weight in class_weight_dict.items():
        print(f"  Class {cls}: {weight:.4f}")

    return class_weight_dict


def create_callbacks(config: TrainingConfig):
    """Create training callbacks for early stopping, LR reduction, and checkpointing."""
    config.output_dir.mkdir(parents=True, exist_ok=True)

    callback_list = [
        # Early stopping
        callbacks.EarlyStopping(
            monitor='val_loss',
            patience=config.patience_early_stop,
            restore_best_weights=True,
            verbose=1
        ),

        # Reduce learning rate on plateau
        callbacks.ReduceLROnPlateau(
            monitor='val_loss',
            factor=config.reduce_lr_factor,
            patience=config.patience_reduce_lr,
            min_lr=1e-7,
            verbose=1
        ),

        # Model checkpoint
        callbacks.ModelCheckpoint(
            filepath=str(config.output_dir / 'best_model.keras'),
            monitor='val_accuracy',
            save_best_only=True,
            verbose=1
        ),

        # TensorBoard logging
        callbacks.TensorBoard(
            log_dir=str(config.output_dir / 'logs'),
            histogram_freq=1
        )
    ]

    return callback_list


def compile_model(model, config: TrainingConfig):
    """Compile model with loss function, optimizer, and metrics."""
    print("\n" + "="*70)
    print("COMPILING MODEL")
    print("="*70)

    # Loss function
    loss = tf.keras.losses.CategoricalCrossentropy()

    # Optimizer
    optimizer = optimizers.Adam(learning_rate=config.learning_rate)

    # Metrics
    metric_list = [
        metrics.CategoricalAccuracy(name='accuracy'),
        metrics.TopKCategoricalAccuracy(k=3, name='top_3_accuracy'),
        metrics.TopKCategoricalAccuracy(k=5, name='top_5_accuracy')
    ]

    model.compile(
        loss=loss,
        optimizer=optimizer,
        metrics=metric_list
    )

    print(f"Loss: Categorical Crossentropy")
    print(f"Optimizer: Adam (lr={config.learning_rate})")
    print(f"Metrics: {[m.name for m in metric_list]}")

    return model


def plot_training_history(history, output_dir):
    """Plot training and validation metrics."""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    # Loss
    axes[0, 0].plot(history.history['loss'], label='Train Loss')
    axes[0, 0].plot(history.history['val_loss'], label='Val Loss')
    axes[0, 0].set_title('Loss')
    axes[0, 0].set_xlabel('Epoch')
    axes[0, 0].set_ylabel('Loss')
    axes[0, 0].legend()
    axes[0, 0].grid(True)

    # Accuracy
    axes[0, 1].plot(history.history['accuracy'], label='Train Acc')
    axes[0, 1].plot(history.history['val_accuracy'], label='Val Acc')
    axes[0, 1].set_title('Accuracy')
    axes[0, 1].set_xlabel('Epoch')
    axes[0, 1].set_ylabel('Accuracy')
    axes[0, 1].legend()
    axes[0, 1].grid(True)

    # Top-3 Accuracy
    axes[1, 0].plot(history.history['top_3_accuracy'], label='Train Top-3')
    axes[1, 0].plot(history.history['val_top_3_accuracy'], label='Val Top-3')
    axes[1, 0].set_title('Top-3 Accuracy')
    axes[1, 0].set_xlabel('Epoch')
    axes[1, 0].set_ylabel('Top-3 Accuracy')
    axes[1, 0].legend()
    axes[1, 0].grid(True)

    # Top-5 Accuracy
    axes[1, 1].plot(history.history['top_5_accuracy'], label='Train Top-5')
    axes[1, 1].plot(history.history['val_top_5_accuracy'], label='Val Top-5')
    axes[1, 1].set_title('Top-5 Accuracy')
    axes[1, 1].set_xlabel('Epoch')
    axes[1, 1].set_ylabel('Top-5 Accuracy')
    axes[1, 1].legend()
    axes[1, 1].grid(True)

    plt.tight_layout()
    plt.savefig(output_dir / 'training_history.png', dpi=300)
    print(f"\nTraining history plot saved to {output_dir / 'training_history.png'}")
    plt.close()


def evaluate_model(model, X_test, y_test, label_encoder, output_dir):
    """Evaluate model on test set and generate confusion matrix."""
    print("\n" + "="*70)
    print("EVALUATING MODEL")
    print("="*70)

    # Evaluate
    results = model.evaluate(X_test, y_test, verbose=0)

    print(f"Test Loss: {results[0]:.4f}")
    print(f"Test Accuracy: {results[1]:.4f}")
    print(f"Test Top-3 Accuracy: {results[2]:.4f}")
    print(f"Test Top-5 Accuracy: {results[3]:.4f}")

    # Predictions
    y_pred = model.predict(X_test, verbose=0)
    y_pred_classes = y_pred.argmax(axis=1)
    y_true_classes = y_test.argmax(axis=1)

    # Confusion matrix
    from sklearn.metrics import confusion_matrix, classification_report

    cm = confusion_matrix(y_true_classes, y_pred_classes)

    # Plot confusion matrix
    plt.figure(figsize=(14, 12))
    sns.heatmap(cm, annot=True, fmt='d', cmap='Blues',
                xticklabels=label_encoder.classes_,
                yticklabels=label_encoder.classes_)
    plt.title('Confusion Matrix')
    plt.xlabel('Predicted')
    plt.ylabel('True')
    plt.tight_layout()
    plt.savefig(output_dir / 'confusion_matrix.png', dpi=300)
    print(f"\nConfusion matrix saved to {output_dir / 'confusion_matrix.png'}")
    plt.close()

    # Classification report
    report = classification_report(y_true_classes, y_pred_classes,
                                   target_names=label_encoder.classes_)
    print("\nClassification Report:")
    print(report)

    # Save report
    with open(output_dir / 'classification_report.txt', 'w') as f:
        f.write(report)

    return results


def train():
    """Main training function."""
    print("\n" + "="*70)
    print("ASL GLOVE CNN TRAINING")
    print("="*70)

    # Configuration
    config = TrainingConfig()

    # Load data with sequence-based split (prevents data leakage)
    X_train, X_test, y_train, y_test, label_encoder, num_classes = load_and_prepare_data(config)

    # Apply data augmentation
    if config.use_augmentation:
        X_train, y_train = augment_data(X_train, y_train, config, augmentation_factor=0.5)

    # Shuffle training data for better batch diversity
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

    return model, history, config


if __name__ == "__main__":
    # Set random seeds for reproducibility
    np.random.seed(42)
    tf.random.set_seed(42)

    # Train model
    model, history, config = train()
