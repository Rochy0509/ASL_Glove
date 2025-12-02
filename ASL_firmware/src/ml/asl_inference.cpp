#include "ml/asl_inference.h"

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "ml/asl_model_data.h"
#include "ml/imu_normalization.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef TFLITE_SCHEMA_VERSION
#define TFLITE_SCHEMA_VERSION (3)
#endif

namespace {
constexpr size_t kWindowSize = 25;
constexpr size_t kNumFlex = 5;
constexpr size_t kNumIMU = 6;
constexpr size_t kNumFeatures = kNumFlex + kNumIMU;
constexpr size_t kNumClasses = 2;
constexpr int kTensorArenaSize = 90 * 1024;

alignas(16) uint8_t tensor_arena[kTensorArenaSize];
tflite::MicroErrorReporter micro_error_reporter;
tflite::ErrorReporter* error_reporter = &micro_error_reporter;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input_tensor = nullptr;
TfLiteTensor* output_tensor = nullptr;

constexpr std::array<const char*, kNumClasses> kLabelNames = {
    "EAT", "HELLO"};

constexpr std::array<char, kNumClasses> kLabelToChar = {
    'E',
    'H'};

inline int8_t quantize(float value, float scale, int zero_point) {
    int32_t quantized = static_cast<int32_t>(std::round(value / scale) + zero_point);
    quantized = std::max(-128, std::min(127, quantized));
    return static_cast<int8_t>(quantized);
}

float dequantize(int8_t value, float scale, int zero_point) {
    return (static_cast<int>(value) - zero_point) * scale;
}
}  // namespace

ASLInferenceEngine aslInference;

bool ASLInferenceEngine::begin() {
    const tflite::Model* model = tflite::GetModel(g_asl_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.println("[ML] Model schema mismatch.");
        ready_ = false;
        return false;
    }

    static tflite::MicroMutableOpResolver<10> resolver;
    resolver.AddConv2D();
    resolver.AddAdd();
    resolver.AddMul();
    resolver.AddMean();
    resolver.AddReshape();
    resolver.AddFullyConnected();
    resolver.AddMaxPool2D();
    resolver.AddSoftmax();
    resolver.AddExpandDims();
    resolver.AddQuantize();
    resolver.AddDequantize();

    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("[ML] Failed to allocate tensors.");
        ready_ = false;
        return false;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);

    if (!input_tensor || !output_tensor ||
        input_tensor->type != kTfLiteInt8 || output_tensor->type != kTfLiteInt8) {
        Serial.println("[ML] Unexpected tensor types.");
        ready_ = false;
        return false;
    }

    ready_ = true;
    Serial.printf("[ML] Inference ready. Input dims: %d x %d\n",
                  input_tensor->dims->data[1], input_tensor->dims->data[2]);
    return true;
}

bool ASLInferenceEngine::classify(const SensorSample* samples,
                                  size_t sample_count,
                                  char& letter,
                                  float& confidence,
                                  int& class_index) {
    letter = kNeutralToken;
    confidence = 0.0f;
    class_index = -1;

    if (!ready_ || !samples || sample_count == 0 || !input_tensor || !output_tensor) {
        return false;
    }

    const size_t window = std::min(sample_count, static_cast<size_t>(input_tensor->dims->data[1]));
    const float input_scale = input_tensor->params.scale;
    const int input_zero_point = input_tensor->params.zero_point;

    size_t offset = 0;
    for (size_t i = 0; i < window; ++i) {
        const SensorSample& sample = samples[i];

        for (size_t f = 0; f < kNumFlex; ++f) {
            float value = sample.fingersValid ? sample.flex[f] : 0.0f;
            value = std::min(1.0f, std::max(0.0f, value));
            input_tensor->data.int8[offset++] = quantize(value, input_scale, input_zero_point);
        }

        const float ax = sample.imuValid ? normalizeSensor(sample.accel[0], kAxParams) : 0.0f;
        const float ay = sample.imuValid ? normalizeSensor(sample.accel[1], kAyParams) : 0.0f;
        const float az = sample.imuValid ? normalizeSensor(sample.accel[2], kAzParams) : 0.0f;
        const float gx = sample.imuValid ? normalizeSensor(sample.gyro[0], kGxParams) : 0.0f;
        const float gy = sample.imuValid ? normalizeSensor(sample.gyro[1], kGyParams) : 0.0f;
        const float gz = sample.imuValid ? normalizeSensor(sample.gyro[2], kGzParams) : 0.0f;

        input_tensor->data.int8[offset++] = quantize(ax, input_scale, input_zero_point);
        input_tensor->data.int8[offset++] = quantize(ay, input_scale, input_zero_point);
        input_tensor->data.int8[offset++] = quantize(az, input_scale, input_zero_point);
        input_tensor->data.int8[offset++] = quantize(gx, input_scale, input_zero_point);
        input_tensor->data.int8[offset++] = quantize(gy, input_scale, input_zero_point);
        input_tensor->data.int8[offset++] = quantize(gz, input_scale, input_zero_point);
    }

    // Pad remaining frames with zeros if input expects a fixed length.
    const size_t total_features = input_tensor->bytes;
    while (offset < total_features) {
        input_tensor->data.int8[offset++] = quantize(0.0f, input_scale, input_zero_point);
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        Serial.println("[ML] Inference invoke failed.");
        return false;
    }

    const float output_scale = output_tensor->params.scale;
    const int output_zero_point = output_tensor->params.zero_point;

    float best_score = -1.0f;
    int best_index = -1;
    for (size_t i = 0; i < kNumClasses; ++i) {
        float value = dequantize(output_tensor->data.int8[i], output_scale, output_zero_point);
        if (value > best_score) {
            best_score = value;
            best_index = static_cast<int>(i);
        }
    }

    if (best_index < 0) {
        return false;
    }

    class_index = best_index;
    confidence = best_score;
    letter = kLabelToChar[best_index];
    return true;
}

const char* ASLInferenceEngine::labelForIndex(size_t index) const {
    if (index >= kLabelNames.size()) {
        return "";
    }
    return kLabelNames[index];
}
