#include "mpu9250_sensor.h"

MPU9250_Sensor::MPU9250_Sensor(TwoWire &bus, uint8_t addr)
    : imu(), initialized(false), sensor_addr(addr), i2c_bus(&bus) {
    fusion_quat[0] = 1.0f; fusion_quat[1] = 0.0f; fusion_quat[2] = 0.0f; fusion_quat[3] = 0.0f;
}

bool MPU9250_Sensor::begin() {
    Serial.println("[MPU9250] Starting initialization...");
    Serial.print("[MPU9250] I2C Address: 0x");
    Serial.println(sensor_addr, HEX);
    
    bool setup_success = imu.setup(sensor_addr, MPU9250Setting{}, *i2c_bus);

    if (!setup_success) {
        Serial.println("[MPU9250] ERROR: Setup failed! Sensor not found on I2C bus");
        Serial.println("[MPU9250] Check: I2C address, wiring, pullup resistors, power supply");
        return false;
    }

    Serial.println("[MPU9250] Sensor found! Setup successful");
    Serial.println("[MPU9250] Enabling AHRS fusion...");
    imu.ahrs(true);
    
    // Give the sensor time to stabilize
    delay(100);
    Serial.println("[MPU9250] Performing warm-up reads...");
    for (int i = 0; i < 10; i++) {
        imu.update();
        delay(10);
    }
    
    // Test first read
    Serial.println("[MPU9250] First test read:");
    float testAccel = imu.getAcc(0);
    float testGyro = imu.getGyro(0);
    Serial.print("[MPU9250] Test Accel X: ");
    Serial.print(testAccel, 3);
    Serial.println(" m/s2");
    Serial.print("[MPU9250] Test Gyro X: ");
    Serial.print(testGyro, 4);
    Serial.println(" rad/s");
    
    if (testAccel == 0.0f && testGyro == 0.0f) {
        Serial.println("[MPU9250] WARNING: Still getting zeros - sensor may not be responding!");
    }
    
    Serial.println("[MPU9250] Initialization complete");
    initialized = true;
    return true;
}

bool MPU9250_Sensor::isReady() {
    return initialized;
}

void MPU9250_Sensor::update() {
    if (initialized) {
        // Continuously update the sensor - MUST call this regularly
        if (imu.update()) {
            // Update quaternion from the library
            fusion_quat[0] = imu.getQuaternionW(); // [w, x, y, z]
            fusion_quat[1] = imu.getQuaternionX();
            fusion_quat[2] = imu.getQuaternionY();
            fusion_quat[3] = imu.getQuaternionZ();
        }
    }
}

// Get raw sensor data (m/s/s)
float MPU9250_Sensor::getAccelX_mss() { 
    if (!initialized) return 0.0f;
    return imu.getAcc(0);
} 
float MPU9250_Sensor::getAccelY_mss() { 
    if (!initialized) return 0.0f;
    return imu.getAcc(1);
}
float MPU9250_Sensor::getAccelZ_mss() { 
    if (!initialized) return 0.0f;
    return imu.getAcc(2);
}

// Get raw sensor data (rad/s)
float MPU9250_Sensor::getGyroX_rads() { 
    if (!initialized) return 0.0f;
    return imu.getGyro(0);
} 
float MPU9250_Sensor::getGyroY_rads() { 
    if (!initialized) return 0.0f;
    return imu.getGyro(1);
}
float MPU9250_Sensor::getGyroZ_rads() { 
    if (!initialized) return 0.0f;
    return imu.getGyro(2);
}

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