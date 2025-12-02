# ASL Glove - Gesture Recognition System

Real-time sign language recognition using ESP32, flex sensors, and IMU.

## Project Status

### Hardware
- [x] Flex sensors (5x fingers)
- [x] MPU9250 IMU (accel + gyro)
- [x] I2S audio amplifier
- [x] SD card logging
- [x] ESP32 firmware with FreeRTOS

### Machine Learning
- [x] Data collection pipeline
- [x] Training pipeline (HELLO and EAT gestures)
- [x] Model quantization (INT8)
- [x] TFLite deployment to ESP32
- [ ] Real-time inference testing

## Quick Start

### 1. Collect Training Data
```bash
cd python/src
python3 csv_collector.py
```
Record gestures - data saved to `python/data_logs/`

### 2. Train Model
```bash
cd ML_model
python3 train_simple.py
```
Trains on HELLO and EAT gestures (~50 epochs, 95%+ accuracy)

### 3. Deploy to Board
```bash
python3 deploy_to_board.py
```
Converts model, updates firmware, and optionally flashes ESP32

### 4. Flash Firmware
```bash
cd ASL_firmware
pio run -t upload
```

## Project Structure

```
ASL_Glove/
├── ASL_firmware/          # ESP32 firmware (PlatformIO)
│   ├── lib/               # Sensor drivers
│   │   ├── IMU/          # MPU9250
│   │   ├── SD_module/    # SD card + audio
│   │   └── finger_sensors/ # Flex sensors
│   └── src/
│       ├── ml/           # TFLite inference engine
│       └── main.cpp
├── ML_model/             # Training and deployment
│   ├── train_simple.py   # Train model
│   ├── deploy_to_board.py # Deploy pipeline
│   └── src/
│       ├── core/         # Model architectures
│       └── data_processing/
├── python/               # Data collection
│   ├── src/
│   │   └── csv_collector.py
│   └── data_logs/        # Training data (CSV files)
└── README.md
```

## Current Model

**Gestures:** HELLO, EAT
**Input:** 25 timesteps × 11 features (5 flex + 6 IMU)
**Architecture:** 1D CNN with BatchNorm and Dropout
**Size:** ~30-50KB (INT8 quantized)
**Accuracy:** 95-100% on test set

## Commands

### Data Collection
```bash
cd python/src
python3 csv_collector.py  # Record gestures
```

### Training
```bash
cd ML_model
python3 train_simple.py   # Train model
```

### Deployment
```bash
cd ML_model
python3 deploy_to_board.py              # Full pipeline
# OR step by step:
python3 convert_to_tflite.py            # Convert model
python3 update_firmware.py              # Update code
python3 update_normalization.py         # Sync params
cd ../ASL_firmware && pio run -t upload # Flash
```

### Firmware
```bash
cd ASL_firmware
pio run              # Build
pio run -t upload    # Flash
pio device monitor   # Serial monitor
```

## Hardware Setup

**Sensors:**
- 5x flex sensors (finger positions, normalized 0-1)
- MPU9250 IMU (6-axis: ax, ay, az, gx, gy, gz)
- Sampling rate: 50Hz
- Window size: 25 samples (500ms)

**Connections:**
- Flex sensors → ADC pins
- MPU9250 → I2C
- SD card → SPI
- Audio → I2S

## Adding New Gestures

1. Collect data for new gesture:
   ```bash
   cd python/src && python3 csv_collector.py
   ```

2. Update training script to include new CSV:
   ```python
   # In ML_model/train_simple.py
   df_new = pd.read_csv("python/data_logs/P1NEWGESTURE_data.csv")
   df = pd.concat([df_hello, df_eat, df_new], ignore_index=True)
   ```

3. Retrain and deploy:
   ```bash
   cd ML_model
   python3 train_simple.py
   python3 deploy_to_board.py
   ```

## Troubleshooting

**No serial port?**
- Check USB cable (must support data)
- Install CH340 drivers for ESP32
- Check device permissions: `sudo usermod -a -G dialout $USER`

**Model not converting?**
- Ensure TensorFlow installed: `pip install tensorflow`
- Check model exists in `ML_model/model/hello_eat_simple/`

**Poor accuracy?**
- Collect more data (500+ samples per gesture)
- Adjust trim values (currently 40 start, 15 end)
- Increase training epochs
- Check sensor calibration

**Upload fails?**
- Hold BOOT button during upload
- Check correct COM port
- Try: `pio run -t erase` then upload again

## Next Steps

- [ ] Test real-time inference on board
- [ ] Add more gestures (full alphabet)
- [ ] Improve temporal modeling (LSTM/GRU)
- [ ] Add gesture rejection threshold
- [ ] Implement continuous recognition

## License

MIT
