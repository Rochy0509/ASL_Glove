#include "mpu9250_sensor.h"

MPU9250_Sensor::MPU9250_Sensor(TwoWire &bus, uint8_t addr)
    : imu(), initialized(false), sensor_addr(addr), i2c_bus(&bus) {
    fusion_quat[0] = 1.0f; fusion_quat[1] = 0.0f; fusion_quat[2] = 0.0f; fusion_quat[3] = 0.0f;
}

bool MPU9250_Sensor::begin() {
    bool setup_success = imu.setup(sensor_addr, MPU9250Setting{}, *i2c_bus);

    if (!setup_success) {
        return false;
    }

    imu.ahrs(true);
    initialized = true;
    return true;
}

bool MPU9250_Sensor::isReady() {
    return initialized;
}

void MPU9250_Sensor::update() {
    if (initialized) {
        if (imu.update()) {
            fusion_quat[0] = imu.getQuaternionW(); // [w, x, y, z]
            fusion_quat[1] = imu.getQuaternionX();
            fusion_quat[2] = imu.getQuaternionY();
            fusion_quat[3] = imu.getQuaternionZ();
        }
    }
}

// Get raw sensor data (m/s/s) - Converting from library's internal units if necessary
float MPU9250_Sensor::getAccelX_mss() { return initialized ? imu.getAcc(0) : 0.0f; } 
float MPU9250_Sensor::getAccelY_mss() { return initialized ? imu.getAcc(1) : 0.0f; }
float MPU9250_Sensor::getAccelZ_mss() { return initialized ? imu.getAcc(2) : 0.0f; }

// Get raw sensor data (rad/s) - Converting from library's internal units if necessary
float MPU9250_Sensor::getGyroX_rads() { return initialized ? imu.getGyro(0) : 0.0f; } 
float MPU9250_Sensor::getGyroY_rads() { return initialized ? imu.getGyro(1) : 0.0f; }
float MPU9250_Sensor::getGyroZ_rads() { return initialized ? imu.getGyro(2) : 0.0f; }

// Get raw sensor data (uT) - Converting from library's internal units if necessary
float MPU9250_Sensor::getMagX_uT() { return initialized ? imu.getMag(0) : 0.0f; } 
float MPU9250_Sensor::getMagY_uT() { return initialized ? imu.getMag(1) : 0.0f; }
float MPU9250_Sensor::getMagZ_uT() { return initialized ? imu.getMag(2) : 0.0f; }

// Get raw sensor data (C)
float MPU9250_Sensor::getTemperature_C() { return initialized ? imu.getTemperature() : 0.0f; }

// Get fused orientation data (quaternion)
float MPU9250_Sensor::getFusedQuatW() { return initialized ? fusion_quat[0] : 1.0f; }
float MPU9250_Sensor::getFusedQuatX() { return initialized ? fusion_quat[1] : 0.0f; }
float MPU9250_Sensor::getFusedQuatY() { return initialized ? fusion_quat[2] : 0.0f; }
float MPU9250_Sensor::getFusedQuatZ() { return initialized ? fusion_quat[3] : 0.0f; }