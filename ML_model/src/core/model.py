"""1D CNN model architecture for ASL gesture classification on ESP32."""

import tensorflow as tf
from tensorflow.keras import layers, models, regularizers


class ASLGloveModel:
    """ASL Glove CNN model builder for temporal sensor data."""

    def __init__(self,
                 num_classes: int = 29,
                 window_size: int = 25,
                 num_features: int = 11,
                 model_type: str = 'cnn_small'):
        """Initialize model configuration."""
        self.num_classes = num_classes
        self.window_size = window_size
        self.num_features = num_features
        self.model_type = model_type

    def build_cnn_small(self, l2_reg: float = 0.001, dropout_rate: float = 0.5) -> models.Model:
        """Build small 1D CNN (~50-80KB quantized) with BatchNorm and increased dropout."""
        inputs = layers.Input(shape=(self.window_size, self.num_features),
                             name='sensor_input')

        # Conv block 1
        x = layers.Conv1D(16, 3, padding='same',
                         kernel_regularizer=regularizers.l2(l2_reg),
                         name='conv1d_1')(inputs)
        x = layers.BatchNormalization(name='bn_1')(x)
        x = layers.Activation('relu', name='relu_1')(x)
        x = layers.MaxPooling1D(2, name='maxpool_1')(x)
        x = layers.Dropout(dropout_rate * 0.5, name='dropout_1')(x)

        # Conv block 2
        x = layers.Conv1D(32, 3, padding='same',
                         kernel_regularizer=regularizers.l2(l2_reg),
                         name='conv1d_2')(x)
        x = layers.BatchNormalization(name='bn_2')(x)
        x = layers.Activation('relu', name='relu_2')(x)
        x = layers.Dropout(dropout_rate * 0.5, name='dropout_2')(x)

        x = layers.GlobalAveragePooling1D(name='global_avg_pool')(x)

        # Dense layer
        x = layers.Dense(32,
                        kernel_regularizer=regularizers.l2(l2_reg),
                        name='dense_1')(x)
        x = layers.BatchNormalization(name='bn_3')(x)
        x = layers.Activation('relu', name='relu_3')(x)
        x = layers.Dropout(dropout_rate, name='dropout_3')(x)

        outputs = layers.Dense(self.num_classes, activation='softmax',
                              name='output')(x)

        return models.Model(inputs=inputs, outputs=outputs, name='ASL_CNN_Small')

    def build_cnn_medium(self, l2_reg: float = 0.0001) -> models.Model:
        """Build medium 1D CNN (~150-200KB quantized) with more capacity."""
        inputs = layers.Input(shape=(self.window_size, self.num_features),
                             name='sensor_input')

        # Conv blocks
        x = layers.Conv1D(16, 3, activation='relu',
                         kernel_regularizer=regularizers.l2(l2_reg),
                         name='conv1d_1')(inputs)
        x = layers.MaxPooling1D(2, name='maxpool_1')(x)

        x = layers.Conv1D(32, 3, activation='relu',
                         kernel_regularizer=regularizers.l2(l2_reg),
                         name='conv1d_2')(x)
        x = layers.MaxPooling1D(2, name='maxpool_2')(x)

        x = layers.Conv1D(64, 3, activation='relu',
                         kernel_regularizer=regularizers.l2(l2_reg),
                         name='conv1d_3')(x)

        x = layers.GlobalAveragePooling1D(name='global_avg_pool')(x)

        # Dense layers
        x = layers.Dense(64, activation='relu',
                        kernel_regularizer=regularizers.l2(l2_reg),
                        name='dense_1')(x)
        x = layers.Dropout(0.3, name='dropout_1')(x)

        x = layers.Dense(32, activation='relu',
                        kernel_regularizer=regularizers.l2(l2_reg),
                        name='dense_2')(x)
        x = layers.Dropout(0.3, name='dropout_2')(x)

        outputs = layers.Dense(self.num_classes, activation='softmax',
                              name='output')(x)

        return models.Model(inputs=inputs, outputs=outputs, name='ASL_CNN_Medium')

    def build_dense_baseline(self, l2_reg: float = 0.0001) -> models.Model:
        """Build simple dense network (~20-30KB quantized) baseline without temporal modeling."""
        inputs = layers.Input(shape=(self.window_size, self.num_features),
                             name='sensor_input')

        x = layers.Flatten(name='flatten')(inputs)

        x = layers.Dense(64, activation='relu',
                        kernel_regularizer=regularizers.l2(l2_reg),
                        name='dense_1')(x)
        x = layers.Dropout(0.3, name='dropout_1')(x)

        x = layers.Dense(32, activation='relu',
                        kernel_regularizer=regularizers.l2(l2_reg),
                        name='dense_2')(x)
        x = layers.Dropout(0.3, name='dropout_2')(x)

        outputs = layers.Dense(self.num_classes, activation='softmax',
                              name='output')(x)

        return models.Model(inputs=inputs, outputs=outputs, name='ASL_Dense_Baseline')

    def build(self, l2_reg: float = 0.001, dropout_rate: float = 0.5) -> models.Model:
        """Build model based on model_type."""
        if self.model_type == 'cnn_small':
            return self.build_cnn_small(l2_reg, dropout_rate)
        elif self.model_type == 'cnn_medium':
            return self.build_cnn_medium(l2_reg)
        elif self.model_type == 'dense':
            return self.build_dense_baseline(l2_reg)
        else:
            raise ValueError(f"Unknown model_type: {self.model_type}")

    def print_model_info(self, model: models.Model):
        """Print model architecture and size estimates."""
        print("\n" + "="*70)
        print(f"MODEL: {model.name}")
        print("="*70)
        model.summary()

        param_count = model.count_params()
        float32_size_mb = (param_count * 4) / (1024 ** 2)
        int8_size_kb = (param_count * 1) / 1024

        print("\n" + "="*70)
        print("MODEL SIZE ESTIMATES")
        print("="*70)
        print(f"Total parameters: {param_count:,}")
        print(f"Float32 size: {float32_size_mb:.2f} MB")
        print(f"INT8 quantized (est.): {int8_size_kb:.2f} KB")
        print("="*70)


def create_model(model_type: str = 'cnn_small',
                 num_classes: int = 29,
                 window_size: int = 25,
                 num_features: int = 11,
                 l2_reg: float = 0.001,
                 dropout_rate: float = 0.5,
                 verbose: bool = True) -> models.Model:
    """Create and build a model with specified configuration."""
    builder = ASLGloveModel(num_classes, window_size, num_features, model_type)
    model = builder.build(l2_reg, dropout_rate)

    if verbose:
        builder.print_model_info(model)

    return model


if __name__ == "__main__":
    print("Testing model architectures...\n")
    for model_type in ['cnn_small', 'cnn_medium', 'dense']:
        model = create_model(model_type=model_type, verbose=True)
        print("\n" + "="*70 + "\n")
