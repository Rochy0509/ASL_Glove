#include "IMU_test.h"

// ===== Register Map =====
const uint8_t REG_WHO_AM_I       = 0x75;
const uint8_t REG_PWR_MGMT_1     = 0x6B;
const uint8_t REG_CONFIG         = 0x1A;
const uint8_t REG_SMPLRT_DIV     = 0x19;
const uint8_t REG_GYRO_CONFIG    = 0x1B;
const uint8_t REG_ACCEL_CONFIG   = 0x1C;
const uint8_t REG_ACCEL_CONFIG2  = 0x1D;
const uint8_t REG_INT_PIN_CFG    = 0x37;
const uint8_t REG_USER_CTRL      = 0x6A;
const uint8_t REG_ACCEL_XOUT_H   = 0x3B;

// I2C Master Mode Registers
const uint8_t REG_I2C_MST_CTRL     = 0x24;
const uint8_t REG_I2C_MST_STATUS   = 0x36;
const uint8_t REG_I2C_SLV0_ADDR    = 0x25;
const uint8_t REG_I2C_SLV0_REG     = 0x26;
const uint8_t REG_I2C_SLV0_CTRL    = 0x27;
const uint8_t REG_I2C_SLV4_ADDR    = 0x31;
const uint8_t REG_I2C_SLV4_REG     = 0x32;
const uint8_t REG_I2C_SLV4_DO      = 0x33;
const uint8_t REG_I2C_SLV4_CTRL    = 0x34;
const uint8_t REG_EXT_SENS_DATA_00 = 0x49;

// Sensitivity Constants
const float ACCEL_SENS = 16384.0f; // ±2g
const float GYRO_SENS  = 131.0f;   // ±250 dps

// AK8963 Magnetometer
const uint8_t AK8963_ADDR = 0x0C;
const uint8_t AK_WHO_AM_I = 0x00;
const uint8_t AK_ST1      = 0x02;
const uint8_t AK_HXL      = 0x03;
const uint8_t AK_ST2      = 0x09;
const uint8_t AK_CNTL1    = 0x0A;
const uint8_t AK_ASAX     = 0x10;
const uint8_t AK_ASAY     = 0x11;
const uint8_t AK_ASAZ     = 0x12;
const float AK_SCALE = 4912.0f / 32760.0f;

// QMC5883L Magnetometer
const uint8_t QMC_ADDR   = 0x0D;
const uint8_t QMC_STATUS = 0x06;
const uint8_t QMC_X_L    = 0x00;
const uint8_t QMC_CTRL1  = 0x09;
const uint8_t QMC_RESET  = 0x0B;
const float QMC_SCALE    = 12000.0f / 32768.0f * 100.0f;

const uint8_t MPU_ADDR_LOW  = 0x68;
const uint8_t MPU_ADDR_HIGH = 0x69;

// ===== Constructor =====
IMUController::IMUController() 
  : ax(0), ay(0), az(0), 
    gx(0), gy(0), gz(0), 
    mx(0), my(0), mz(0), 
    magOK(false),
    mpuAddr(MPU_ADDR_LOW),
    magMode(MAG_NONE) {
  akAdj[0] = akAdj[1] = akAdj[2] = 1.0f;
}

// ===== I2C Helpers =====
void IMUController::writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t IMUController::readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((int)addr, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

void IMUController::readRegs(uint8_t addr, uint8_t reg, uint8_t count, uint8_t* data) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((int)addr, (int)count);
  for (uint8_t i = 0; i < count && Wire.available(); i++) {
    data[i] = Wire.read();
  }
}

void IMUController::scanI2C() {
  Serial.println("\n=== I2C Bus Scan ===");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Device at 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      found++;
    }
  }
  Serial.print("Found ");
  Serial.print(found);
  Serial.println(found == 1 ? " device\n" : " devices\n");
}

// ===== MPU Functions =====
bool IMUController::detectMPU() {
  uint8_t whoami = readReg(mpuAddr, REG_WHO_AM_I);
  Serial.print("MPU WHO_AM_I (0x");
  Serial.print(mpuAddr, HEX);
  Serial.print("): 0x");
  Serial.println(whoami, HEX);
  return (whoami == 0x68 || whoami == 0x70 || whoami == 0x71 || whoami == 0x73);
}

void IMUController::initMPU() {
  Serial.println("Initializing MPU...");
  writeReg(mpuAddr, REG_PWR_MGMT_1, 0x80);
  delay(100);
  writeReg(mpuAddr, REG_PWR_MGMT_1, 0x01);
  delay(100);
  writeReg(mpuAddr, REG_CONFIG, 0x03);
  writeReg(mpuAddr, REG_SMPLRT_DIV, 0x04);
  writeReg(mpuAddr, REG_GYRO_CONFIG, 0x00);
  writeReg(mpuAddr, REG_ACCEL_CONFIG, 0x00);
  writeReg(mpuAddr, REG_ACCEL_CONFIG2, 0x03);
  Serial.println("MPU initialized\n");
}

void IMUController::readAccelGyro() {
  uint8_t data[14];
  readRegs(mpuAddr, REG_ACCEL_XOUT_H, 14, data);
  
  int16_t axRaw = (int16_t)(data[0]  << 8 | data[1]);
  int16_t ayRaw = (int16_t)(data[2]  << 8 | data[3]);
  int16_t azRaw = (int16_t)(data[4]  << 8 | data[5]);
  int16_t gxRaw = (int16_t)(data[8]  << 8 | data[9]);
  int16_t gyRaw = (int16_t)(data[10] << 8 | data[11]);
  int16_t gzRaw = (int16_t)(data[12] << 8 | data[13]);
  
  ax = axRaw / ACCEL_SENS;
  ay = ayRaw / ACCEL_SENS;
  az = azRaw / ACCEL_SENS;
  gx = gxRaw / GYRO_SENS;
  gy = gyRaw / GYRO_SENS;
  gz = gzRaw / GYRO_SENS;
}

// ===== AK8963 Bypass Mode =====
bool IMUController::initAK8963Bypass() {
  Serial.println("Initializing AK8963 (bypass mode)...");
  writeReg(mpuAddr, REG_USER_CTRL, 0x00);
  delay(10);
  writeReg(mpuAddr, REG_INT_PIN_CFG, 0x02);
  delay(50);
  
  uint8_t whoami = readReg(AK8963_ADDR, AK_WHO_AM_I);
  Serial.print("AK8963 WHO_AM_I: 0x");
  Serial.println(whoami, HEX);
  
  if (whoami != 0x48) {
    Serial.println("AK8963 not found\n");
    return false;
  }
  
  writeReg(AK8963_ADDR, AK_CNTL1, 0x00);
  delay(100);
  writeReg(AK8963_ADDR, AK_CNTL1, 0x0F);
  delay(100);
  
  uint8_t asa[3];
  readRegs(AK8963_ADDR, AK_ASAX, 3, asa);
  for (int i = 0; i < 3; i++) {
    akAdj[i] = ((float)asa[i] - 128.0f) / 256.0f + 1.0f;
  }
  
  Serial.print("Sensitivity: ");
  Serial.print(akAdj[0], 4); Serial.print(", ");
  Serial.print(akAdj[1], 4); Serial.print(", ");
  Serial.println(akAdj[2], 4);
  
  writeReg(AK8963_ADDR, AK_CNTL1, 0x00);
  delay(100);
  writeReg(AK8963_ADDR, AK_CNTL1, 0x16);
  delay(100);
  
  Serial.println("AK8963 initialized\n");
  return true;
}

bool IMUController::readAK8963Bypass() {
  uint8_t st1 = readReg(AK8963_ADDR, AK_ST1);
  if (!(st1 & 0x01)) {
    magOK = false;
    return false;
  }
  
  uint8_t data[7];
  readRegs(AK8963_ADDR, AK_HXL, 7, data);
  if (data[6] & 0x08) {
    magOK = false;
    return false;
  }
  
  int16_t mxRaw = (int16_t)(data[1] << 8 | data[0]);
  int16_t myRaw = (int16_t)(data[3] << 8 | data[2]);
  int16_t mzRaw = (int16_t)(data[5] << 8 | data[4]);
  
  mx = mxRaw * AK_SCALE * akAdj[0];
  my = myRaw * AK_SCALE * akAdj[1];
  mz = mzRaw * AK_SCALE * akAdj[2];
  magOK = true;
  return true;
}

// ===== AK8963 Master Mode =====
bool IMUController::masterWrite(uint8_t reg, uint8_t val) {
  writeReg(mpuAddr, REG_I2C_SLV4_ADDR, AK8963_ADDR);
  writeReg(mpuAddr, REG_I2C_SLV4_REG, reg);
  writeReg(mpuAddr, REG_I2C_SLV4_DO, val);
  writeReg(mpuAddr, REG_I2C_SLV4_CTRL, 0x80);
  for (int i = 0; i < 100; i++) {
    if (readReg(mpuAddr, REG_I2C_MST_STATUS) & 0x40) return true;
    delay(2);
  }
  return false;
}

uint8_t IMUController::masterReadByte(uint8_t reg) {
  writeReg(mpuAddr, REG_I2C_SLV4_ADDR, 0x80 | AK8963_ADDR);
  writeReg(mpuAddr, REG_I2C_SLV4_REG, reg);
  writeReg(mpuAddr, REG_I2C_SLV4_CTRL, 0x80);
  delay(10);
  return readReg(mpuAddr, REG_EXT_SENS_DATA_00);
}

void IMUController::masterReadSetup(uint8_t startReg, uint8_t count) {
  writeReg(mpuAddr, REG_I2C_SLV0_ADDR, 0x80 | AK8963_ADDR);
  writeReg(mpuAddr, REG_I2C_SLV0_REG, startReg);
  writeReg(mpuAddr, REG_I2C_SLV0_CTRL, 0x80 | (count & 0x0F));
}

bool IMUController::initAK8963Master() {
  Serial.println("Initializing AK8963 (master mode)...");
  writeReg(mpuAddr, REG_INT_PIN_CFG, 0x00);
  delay(10);
  writeReg(mpuAddr, REG_USER_CTRL, 0x20);
  delay(10);
  writeReg(mpuAddr, REG_I2C_MST_CTRL, 0x0D);
  delay(10);
  
  uint8_t whoami = masterReadByte(AK_WHO_AM_I);
  Serial.print("AK8963 WHO_AM_I: 0x");
  Serial.println(whoami, HEX);
  
  if (whoami != 0x48) {
    Serial.println("AK8963 not found\n");
    return false;
  }
  
  if (!masterWrite(AK_CNTL1, 0x00)) return false;
  delay(100);
  if (!masterWrite(AK_CNTL1, 0x0F)) return false;
  delay(100);
  
  masterReadSetup(AK_ASAX, 3);
  delay(10);
  
  uint8_t asa[3];
  readRegs(mpuAddr, REG_EXT_SENS_DATA_00, 3, asa);
  for (int i = 0; i < 3; i++) {
    akAdj[i] = ((float)asa[i] - 128.0f) / 256.0f + 1.0f;
  }
  
  Serial.print("Sensitivity: ");
  Serial.print(akAdj[0], 4); Serial.print(", ");
  Serial.print(akAdj[1], 4); Serial.print(", ");
  Serial.println(akAdj[2], 4);
  
  if (!masterWrite(AK_CNTL1, 0x00)) return false;
  delay(100);
  if (!masterWrite(AK_CNTL1, 0x16)) return false;
  delay(100);
  
  masterReadSetup(AK_ST1, 8);
  delay(10);
  
  Serial.println("AK8963 initialized\n");
  return true;
}

bool IMUController::readAK8963Master() {
  uint8_t data[8];
  readRegs(mpuAddr, REG_EXT_SENS_DATA_00, 8, data);
  if (!(data[0] & 0x01) || (data[7] & 0x08)) {
    magOK = false;
    return false;
  }
  
  int16_t mxRaw = (int16_t)(data[2] << 8 | data[1]);
  int16_t myRaw = (int16_t)(data[4] << 8 | data[3]);
  int16_t mzRaw = (int16_t)(data[6] << 8 | data[5]);
  
  mx = mxRaw * AK_SCALE * akAdj[0];
  my = myRaw * AK_SCALE * akAdj[1];
  mz = mzRaw * AK_SCALE * akAdj[2];
  magOK = true;
  return true;
}

// ===== QMC5883L =====
bool IMUController::initQMC5883L() {
  Serial.println("Initializing QMC5883L...");
  writeReg(QMC_ADDR, QMC_RESET, 0x01);
  delay(100);
  writeReg(QMC_ADDR, QMC_CTRL1, 0x1D);
  delay(10);
  
  Wire.beginTransmission(QMC_ADDR);
  if (Wire.endTransmission() == 0) {
    Serial.println("QMC5883L initialized\n");
    return true;
  }
  Serial.println("QMC5883L not found\n");
  return false;
}

bool IMUController::readQMC5883L() {
  if (!(readReg(QMC_ADDR, QMC_STATUS) & 0x01)) {
    magOK = false;
    return false;
  }
  
  uint8_t data[6];
  readRegs(QMC_ADDR, QMC_X_L, 6, data);
  
  int16_t mxRaw = (int16_t)(data[1] << 8 | data[0]);
  int16_t myRaw = (int16_t)(data[3] << 8 | data[2]);
  int16_t mzRaw = (int16_t)(data[5] << 8 | data[4]);
  
  mx = mxRaw * QMC_SCALE;
  my = myRaw * QMC_SCALE;
  mz = mzRaw * QMC_SCALE;
  magOK = true;
  return true;
}

// ===== Public API =====
void IMUController::begin() {
  Serial.println("\n\n====================================");
  Serial.println("  ESP32-S3 IMU Sensor Test");
  Serial.println("====================================\n");
  Serial.print("I2C: SDA=GPIO");
  Serial.print(SDA_PIN);
  Serial.print(", SCL=GPIO");
  Serial.println(SCL_PIN);
  
  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  delay(100);
  
  scanI2C();
  
  mpuAddr = MPU_ADDR_LOW;
  if (!detectMPU()) {
    mpuAddr = MPU_ADDR_HIGH;
    if (!detectMPU()) {
      Serial.println("\nMPU not found!");
      Serial.println("Check wiring:");
      Serial.println("  VCC -> 3.3V");
      Serial.println("  GND -> GND");
      Serial.print("  SDA -> GPIO");
      Serial.println(SDA_PIN);
      Serial.print("  SCL -> GPIO");
      Serial.println(SCL_PIN);
      while (1) delay(1000);
    }
  }
  
  Serial.print("MPU at 0x");
  Serial.print(mpuAddr, HEX);
  Serial.println("\n");
  
  initMPU();
  
  Serial.println("--- Magnetometer Detection ---");
  if (initAK8963Bypass()) {
    magMode = MAG_AK_BYPASS;
  } else if (initAK8963Master()) {
    magMode = MAG_AK_MASTER;
  } else if (initQMC5883L()) {
    magMode = MAG_QMC;
  } else {
    magMode = MAG_NONE;
    Serial.println("No magnetometer detected");
    Serial.println("Running 6-axis mode\n");
  }
  
  Serial.println("====================================");
  Serial.println("Streaming @ ~20Hz");
  Serial.println("====================================\n");
  delay(1000);
}

void IMUController::update() {
  readAccelGyro();
  
  magOK = false;
  switch (magMode) {
    case MAG_AK_BYPASS:
      readAK8963Bypass();
      break;
    case MAG_AK_MASTER:
      readAK8963Master();
      break;
    case MAG_QMC:
      readQMC5883L();
      break;
    default:
      break;
  }
}

void IMUController::printReadings() {
  Serial.print("A:");
  Serial.print(ax, 2); Serial.print(",");
  Serial.print(ay, 2); Serial.print(",");
  Serial.print(az, 2);
  
  Serial.print(" G:");
  Serial.print(gx, 1); Serial.print(",");
  Serial.print(gy, 1); Serial.print(",");
  Serial.print(gz, 1);
  
  Serial.print(" M:");
  if (magMode == MAG_NONE) {
    Serial.print("N/A");
  } else if (!magOK) {
    Serial.print("...");
  } else {
    Serial.print(mx, 0); Serial.print(",");
    Serial.print(my, 0); Serial.print(",");
    Serial.print(mz, 0);
  }
  
  Serial.println();
}
