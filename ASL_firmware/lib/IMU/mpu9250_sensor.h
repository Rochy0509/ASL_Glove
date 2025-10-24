#ifndef MPU9250_SENSOR_H
#define MPU9250_SENSOR_H
#include <Arduino.h>
#include "MPU9250.h"

class MPU9250_Sensor {
private:
    MPU9250 imu;
    bool initialized;
    float fusion_quat[4];
    uint8_t sensor_addr;
    TwoWire* i2c_bus;

public:
    MPU9250_Sensor(TwoWire &bus = Wire, uint8_t addr = 0x68);
    bool begin();
    bool isReady();
    void update();
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
    float getFusedQuatW();
    float getFusedQuatX();
    float getFusedQuatY();
    float getFusedQuatZ();
};

#endif // MPU9250_SENSOR_H