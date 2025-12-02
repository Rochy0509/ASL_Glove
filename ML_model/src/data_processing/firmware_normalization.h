// Normalization parameters - must match training
struct NormParams { float mean; float std; };

const NormParams ax_params = {0.671686f, 3.251813f};
const NormParams ay_params = {-7.010152f, 4.836328f};
const NormParams az_params = {0.290774f, 3.978212f};
const NormParams gx_params = {-0.038167f, 1.003872f};
const NormParams gy_params = {-0.029873f, 1.048374f};
const NormParams gz_params = {-0.021842f, 0.830406f};

float normalize(float val, NormParams p) { return (val - p.mean) / p.std; }
