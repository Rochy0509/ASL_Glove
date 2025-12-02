#ifndef MPU9250_SENSOR_H
#define MPU9250_SENSOR_H

#include <Arduino.h>
#include <Wire.h>

class MPU9250_Sensor {
public:
    MPU9250_Sensor(TwoWire &bus = Wire, uint8_t addr = 0x68);

    bool begin();
    bool isReady();
    void update();
    void resetCalibration();
    bool runCalibrationRoutine(uint32_t durationMs = 6000);
    void printCalibrationInfo() const;
    bool isCalibrated() const { return calibrationReady; }
    void getNormalizedReadings(float* accelOut, float* gyroOut) const;

    // Raw sensor data
    float getAccelX_mss();
    float getAccelY_mss();
    float getAccelZ_mss();
    float getGyroX_rads();
    float getGyroY_rads();
    float getGyroZ_rads();
    float getMagX_uT();
    float getMagY_uT();
    float getMagZ_uT();
    float getTemperature_C();

    // Fused orientation (quaternion)
    float getFusedQuatW();
    float getFusedQuatX();
    float getFusedQuatY();
    float getFusedQuatZ();

private:
    // Magnetometer mode
    enum MagMode { MAG_NONE, MAG_AK_BYPASS, MAG_AK_MASTER, MAG_QMC };

    // I2C
    TwoWire* wire;
    uint8_t mpuAddr;

    // State
    bool initialized;
    MagMode magMode;
    bool magOK;

    // Raw sensor data
    float ax, ay, az;
    float gx, gy, gz;
    float mx, my, mz;
    float temp;

    // Madgwick filter
    float q0, q1, q2, q3;  // Quaternion
    float beta;             // Filter gain
    unsigned long lastUpdate;

    // AK8963 calibration
    float akAdj[3];

    // I2C helpers
    void writeReg(uint8_t addr, uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t addr, uint8_t reg);
    void readRegs(uint8_t addr, uint8_t reg, uint8_t count, uint8_t* data);

    // MPU functions
    bool detectMPU();
    void initMPU();
    void readAccelGyro();

    // AK8963 bypass mode
    bool initAK8963Bypass();
    bool readAK8963Bypass();

    // AK8963 master mode
    bool initAK8963Master();
    bool readAK8963Master();
    bool masterWrite(uint8_t reg, uint8_t val);
    uint8_t masterReadByte(uint8_t reg);
    void masterReadSetup(uint8_t startReg, uint8_t count);

    // QMC5883L
    bool initQMC5883L();
    bool readQMC5883L();

    // Madgwick filter
    void madgwickUpdate(float ax, float ay, float az,
                        float gx, float gy, float gz,
                        float mx, float my, float mz,
                        float dt);
    void madgwickUpdateIMU(float ax, float ay, float az,
                           float gx, float gy, float gz,
                           float dt);

    float normalizeAxis(float value, float minValue, float maxValue) const;

    float accelMin[3];
    float accelMax[3];
    float gyroMin[3];
    float gyroMax[3];
    bool calibrationReady;
};

#endif // MPU9250_SENSOR_H
