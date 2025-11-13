#ifndef IMU_TEST_H
#define IMU_TEST_H

#include <Wire.h>
#include <Arduino.h>

// ===== CONFIGURATION =====
#define SDA_PIN  18    // Your soldered SDA pin
#define SCL_PIN 46      // Your soldered SCL pin
#define BAUD    115200
// =========================

class IMUController {
public:
  enum MagMode { MAG_NONE, MAG_AK_BYPASS, MAG_AK_MASTER, MAG_QMC };
  
  IMUController();
  void begin();
  void update();
  void printReadings();
  
  float getAccelX() const { return ax; }
  float getAccelY() const { return ay; }
  float getAccelZ() const { return az; }
  float getGyroX() const { return gx; }
  float getGyroY() const { return gy; }
  float getGyroZ() const { return gz; }
  float getMagX() const { return mx; }
  float getMagY() const { return my; }
  float getMagZ() const { return mz; }
  bool isMagReady() const { return magOK; }
  MagMode getMagMode() const { return magMode; }

private:
  // I2C Helpers
  void writeReg(uint8_t addr, uint8_t reg, uint8_t val);
  uint8_t readReg(uint8_t addr, uint8_t reg);
  void readRegs(uint8_t addr, uint8_t reg, uint8_t count, uint8_t* data);
  void scanI2C();
  
  // MPU Functions
  bool detectMPU();
  void initMPU();
  void readAccelGyro();
  
  // AK8963 Bypass Mode
  bool initAK8963Bypass();
  bool readAK8963Bypass();
  
  // AK8963 Master Mode
  bool masterWrite(uint8_t reg, uint8_t val);
  uint8_t masterReadByte(uint8_t reg);
  void masterReadSetup(uint8_t startReg, uint8_t count);
  bool initAK8963Master();
  bool readAK8963Master();
  
  // QMC5883L
  bool initQMC5883L();
  bool readQMC5883L();
  
  // Sensor data
  float ax, ay, az;
  float gx, gy, gz;
  float mx, my, mz;
  bool magOK;
  
  // Configuration
  uint8_t mpuAddr;
  MagMode magMode;
  float akAdj[3];
};

#endif // IMU_TEST_H