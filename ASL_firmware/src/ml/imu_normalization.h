// Auto-generated normalization parameters
// Must match training data preprocessing
#ifndef IMU_NORMALIZATION_H_
#define IMU_NORMALIZATION_H_

struct NormParams {
    float mean;
    float std;
};

constexpr NormParams kAxParams = {0.652877f, 0.246747f};
constexpr NormParams kAyParams = {0.662821f, 0.117152f};
constexpr NormParams kAzParams = {0.410897f, 0.252453f};
constexpr NormParams kGxParams = {0.504955f, 0.301887f};
constexpr NormParams kGyParams = {0.501236f, 0.212698f};
constexpr NormParams kGzParams = {0.485134f, 0.303284f};

// Normalize sensor value using z-score normalization
inline float normalizeSensor(float value, const NormParams& p) {
    return (value - p.mean) / p.std;
}

#endif  // IMU_NORMALIZATION_H_