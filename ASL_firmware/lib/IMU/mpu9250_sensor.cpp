#include "mpu9250_sensor.h"
#include <float.h>
#include <math.h>

// Register Definitions
#define REG_WHO_AM_I        0x75
#define REG_PWR_MGMT_1      0x6B
#define REG_CONFIG          0x1A
#define REG_SMPLRT_DIV      0x19
#define REG_GYRO_CONFIG     0x1B
#define REG_ACCEL_CONFIG    0x1C
#define REG_ACCEL_CONFIG2   0x1D
#define REG_INT_PIN_CFG     0x37
#define REG_USER_CTRL       0x6A
#define REG_ACCEL_XOUT_H    0x3B
#define REG_TEMP_OUT_H      0x41
#define REG_I2C_MST_CTRL    0x24
#define REG_I2C_MST_STATUS  0x36
#define REG_I2C_SLV0_ADDR   0x25
#define REG_I2C_SLV0_REG    0x26
#define REG_I2C_SLV0_CTRL   0x27
#define REG_I2C_SLV4_ADDR   0x31
#define REG_I2C_SLV4_REG    0x32
#define REG_I2C_SLV4_DO     0x33
#define REG_I2C_SLV4_CTRL   0x34
#define REG_I2C_SLV4_DI     0x35
#define REG_EXT_SENS_DATA_00 0x49

// MPU addresses
#define MPU_ADDR_LOW   0x68
#define MPU_ADDR_HIGH  0x69

// AK8963 Magnetometer
#define AK8963_ADDR    0x0C
#define AK_WHO_AM_I    0x00
#define AK_ST1         0x02
#define AK_HXL         0x03
#define AK_ST2         0x09
#define AK_CNTL1       0x0A
#define AK_ASAX        0x10

// QMC5883L Magnetometer
#define QMC_ADDR       0x0D
#define QMC_STATUS     0x06
#define QMC_X_L        0x00
#define QMC_CTRL1      0x09
#define QMC_RESET      0x0B

// Sensor scales
#define ACCEL_SCALE    (9.81f / 16384.0f)  // ±2g -> m/s²
#define GYRO_SCALE     (M_PI / 180.0f / 131.0f)  // ±250°/s -> rad/s
#define TEMP_SCALE     (1.0f / 333.87f)
#define TEMP_OFFSET    21.0f
#define AK_SCALE       (4912.0f / 32760.0f)  // μT
#define QMC_SCALE      (12000.0f / 32768.0f)  // Gauss to μT

// Constructor
MPU9250_Sensor::MPU9250_Sensor(TwoWire &bus, uint8_t addr)
    : wire(&bus), mpuAddr(addr), initialized(false), magMode(MAG_NONE),
      magOK(false), ax(0), ay(0), az(0), gx(0), gy(0), gz(0),
      mx(0), my(0), mz(0), temp(0), q0(1), q1(0), q2(0), q3(0),
      beta(0.1f), lastUpdate(0) {
    akAdj[0] = akAdj[1] = akAdj[2] = 1.0f;
}

// I2C Helpers
void MPU9250_Sensor::writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
    wire->beginTransmission(addr);
    wire->write(reg);
    wire->write(val);
    wire->endTransmission();
}

uint8_t MPU9250_Sensor::readReg(uint8_t addr, uint8_t reg) {
    wire->beginTransmission(addr);
    wire->write(reg);
    wire->endTransmission(false);
    wire->requestFrom((int)addr, 1);
    return wire->available() ? wire->read() : 0xFF;
}

void MPU9250_Sensor::readRegs(uint8_t addr, uint8_t reg, uint8_t count, uint8_t* data) {
    wire->beginTransmission(addr);
    wire->write(reg);
    wire->endTransmission(false);
    wire->requestFrom((int)addr, (int)count);
    for (uint8_t i = 0; i < count && wire->available(); i++) {
        data[i] = wire->read();
    }
}

// MPU Detection & Init
bool MPU9250_Sensor::detectMPU() {
    uint8_t whoami = readReg(mpuAddr, REG_WHO_AM_I);
    return (whoami == 0x68 || whoami == 0x70 || whoami == 0x71 || whoami == 0x73);
}

void MPU9250_Sensor::initMPU() {
    writeReg(mpuAddr, REG_PWR_MGMT_1, 0x80);  // Reset
    delay(100);
    writeReg(mpuAddr, REG_PWR_MGMT_1, 0x01);  // Auto-select clock
    delay(100);
    writeReg(mpuAddr, REG_CONFIG, 0x03);       // DLPF 44Hz
    writeReg(mpuAddr, REG_SMPLRT_DIV, 0x04);   // 200Hz sample rate
    writeReg(mpuAddr, REG_GYRO_CONFIG, 0x00);  // ±250°/s
    writeReg(mpuAddr, REG_ACCEL_CONFIG, 0x00); // ±2g
    writeReg(mpuAddr, REG_ACCEL_CONFIG2, 0x03); // DLPF 44Hz
}

void MPU9250_Sensor::readAccelGyro() {
    uint8_t data[14];
    readRegs(mpuAddr, REG_ACCEL_XOUT_H, 14, data);

    int16_t axRaw = (int16_t)(data[0]  << 8 | data[1]);
    int16_t ayRaw = (int16_t)(data[2]  << 8 | data[3]);
    int16_t azRaw = (int16_t)(data[4]  << 8 | data[5]);
    int16_t tempRaw = (int16_t)(data[6] << 8 | data[7]);
    int16_t gxRaw = (int16_t)(data[8]  << 8 | data[9]);
    int16_t gyRaw = (int16_t)(data[10] << 8 | data[11]);
    int16_t gzRaw = (int16_t)(data[12] << 8 | data[13]);

    // PCB coordinate remapping: X->Yaw, Y->Pitch
    // Swap X and Y axes to match PCB orientation
    ax = ayRaw * ACCEL_SCALE;  // PCB X = sensor Y
    ay = axRaw * ACCEL_SCALE;  // PCB Y = sensor X
    az = azRaw * ACCEL_SCALE;  // PCB Z = sensor Z

    gx = gyRaw * GYRO_SCALE;   // PCB X = sensor Y
    gy = gxRaw * GYRO_SCALE;   // PCB Y = sensor X
    gz = gzRaw * GYRO_SCALE;   // PCB Z = sensor Z

    temp = tempRaw * TEMP_SCALE + TEMP_OFFSET;
}

// AK8963 Bypass Mode
bool MPU9250_Sensor::initAK8963Bypass() {
    writeReg(mpuAddr, REG_USER_CTRL, 0x00);
    delay(10);
    writeReg(mpuAddr, REG_INT_PIN_CFG, 0x02);  // Bypass enable
    delay(50);

    if (readReg(AK8963_ADDR, AK_WHO_AM_I) != 0x48) return false;

    writeReg(AK8963_ADDR, AK_CNTL1, 0x00);
    delay(100);
    writeReg(AK8963_ADDR, AK_CNTL1, 0x0F);  // Fuse ROM mode
    delay(100);

    uint8_t asa[3];
    readRegs(AK8963_ADDR, AK_ASAX, 3, asa);
    for (int i = 0; i < 3; i++) {
        akAdj[i] = ((float)asa[i] - 128.0f) / 256.0f + 1.0f;
    }

    writeReg(AK8963_ADDR, AK_CNTL1, 0x00);
    delay(100);
    writeReg(AK8963_ADDR, AK_CNTL1, 0x16);  // 16-bit, 100Hz
    delay(100);

    return true;
}

bool MPU9250_Sensor::readAK8963Bypass() {
    if (!(readReg(AK8963_ADDR, AK_ST1) & 0x01)) return false;

    uint8_t data[7];
    readRegs(AK8963_ADDR, AK_HXL, 7, data);
    if (data[6] & 0x08) return false;  // Overflow

    int16_t mxRaw = (int16_t)(data[1] << 8 | data[0]);
    int16_t myRaw = (int16_t)(data[3] << 8 | data[2]);
    int16_t mzRaw = (int16_t)(data[5] << 8 | data[4]);

    mx = mxRaw * AK_SCALE * akAdj[0];
    my = myRaw * AK_SCALE * akAdj[1];
    mz = mzRaw * AK_SCALE * akAdj[2];

    return true;
}

// AK8963 Master Mode
bool MPU9250_Sensor::masterWrite(uint8_t reg, uint8_t val) {
    writeReg(mpuAddr, REG_I2C_SLV4_ADDR, AK8963_ADDR);
    writeReg(mpuAddr, REG_I2C_SLV4_REG, reg);
    writeReg(mpuAddr, REG_I2C_SLV4_DO, val);
    writeReg(mpuAddr, REG_I2C_SLV4_CTRL, 0x80);

    for (int i = 0; i < 50; i++) {
        if (readReg(mpuAddr, REG_I2C_MST_STATUS) & 0x40) return true;
        delay(2);
    }
    return false;
}

uint8_t MPU9250_Sensor::masterReadByte(uint8_t reg) {
    writeReg(mpuAddr, REG_I2C_SLV4_ADDR, 0x80 | AK8963_ADDR);
    writeReg(mpuAddr, REG_I2C_SLV4_REG, reg);
    writeReg(mpuAddr, REG_I2C_SLV4_CTRL, 0x80);
    delay(10);
    return readReg(mpuAddr, REG_I2C_SLV4_DI);
}

void MPU9250_Sensor::masterReadSetup(uint8_t startReg, uint8_t count) {
    writeReg(mpuAddr, REG_I2C_SLV0_ADDR, 0x80 | AK8963_ADDR);
    writeReg(mpuAddr, REG_I2C_SLV0_REG, startReg);
    writeReg(mpuAddr, REG_I2C_SLV0_CTRL, 0x80 | (count & 0x0F));
}

bool MPU9250_Sensor::initAK8963Master() {
    writeReg(mpuAddr, REG_INT_PIN_CFG, 0x00);
    delay(10);
    writeReg(mpuAddr, REG_USER_CTRL, 0x20);  // I2C master enable
    delay(10);
    writeReg(mpuAddr, REG_I2C_MST_CTRL, 0x0D);  // 400kHz
    delay(10);

    if (masterReadByte(AK_WHO_AM_I) != 0x48) return false;

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

    if (!masterWrite(AK_CNTL1, 0x00)) return false;
    delay(100);
    if (!masterWrite(AK_CNTL1, 0x16)) return false;
    delay(100);

    masterReadSetup(AK_ST1, 8);
    delay(10);

    return true;
}

bool MPU9250_Sensor::readAK8963Master() {
    uint8_t data[8];
    readRegs(mpuAddr, REG_EXT_SENS_DATA_00, 8, data);
    if (!(data[0] & 0x01) || (data[7] & 0x08)) return false;

    int16_t mxRaw = (int16_t)(data[2] << 8 | data[1]);
    int16_t myRaw = (int16_t)(data[4] << 8 | data[3]);
    int16_t mzRaw = (int16_t)(data[6] << 8 | data[5]);

    mx = mxRaw * AK_SCALE * akAdj[0];
    my = myRaw * AK_SCALE * akAdj[1];
    mz = mzRaw * AK_SCALE * akAdj[2];

    return true;
}

// QMC5883L
bool MPU9250_Sensor::initQMC5883L() {
    writeReg(QMC_ADDR, QMC_RESET, 0x01);
    delay(100);
    writeReg(QMC_ADDR, QMC_CTRL1, 0x1D);  // 200Hz, 8x oversample, continuous
    delay(10);

    wire->beginTransmission(QMC_ADDR);
    return (wire->endTransmission() == 0);
}

bool MPU9250_Sensor::readQMC5883L() {
    if (!(readReg(QMC_ADDR, QMC_STATUS) & 0x01)) return false;

    uint8_t data[6];
    readRegs(QMC_ADDR, QMC_X_L, 6, data);

    int16_t mxRaw = (int16_t)(data[1] << 8 | data[0]);
    int16_t myRaw = (int16_t)(data[3] << 8 | data[2]);
    int16_t mzRaw = (int16_t)(data[5] << 8 | data[4]);

    mx = mxRaw * QMC_SCALE;
    my = myRaw * QMC_SCALE;
    mz = mzRaw * QMC_SCALE;

    return true;
}

// Madgwick Filter
void MPU9250_Sensor::madgwickUpdate(float ax, float ay, float az,
                                    float gx, float gy, float gz,
                                    float mx, float my, float mz,
                                    float dt) {
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float hx, hy;
    float _2q0mx, _2q0my, _2q0mz, _2q1mx, _2bx, _2bz, _4bx, _4bz;
    float _2q0, _2q1, _2q2, _2q3, _2q0q2, _2q2q3;
    float q0q0, q0q1, q0q2, q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;

    // Normalize accelerometer
    recipNorm = 1.0f / sqrtf(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    // Normalize magnetometer
    recipNorm = 1.0f / sqrtf(mx * mx + my * my + mz * mz);
    mx *= recipNorm;
    my *= recipNorm;
    mz *= recipNorm;

    // Auxiliary variables
    _2q0mx = 2.0f * q0 * mx;
    _2q0my = 2.0f * q0 * my;
    _2q0mz = 2.0f * q0 * mz;
    _2q1mx = 2.0f * q1 * mx;
    _2q0 = 2.0f * q0;
    _2q1 = 2.0f * q1;
    _2q2 = 2.0f * q2;
    _2q3 = 2.0f * q3;
    _2q0q2 = 2.0f * q0 * q2;
    _2q2q3 = 2.0f * q2 * q3;
    q0q0 = q0 * q0;
    q0q1 = q0 * q1;
    q0q2 = q0 * q2;
    q0q3 = q0 * q3;
    q1q1 = q1 * q1;
    q1q2 = q1 * q2;
    q1q3 = q1 * q3;
    q2q2 = q2 * q2;
    q2q3 = q2 * q3;
    q3q3 = q3 * q3;

    // Reference direction of Earth's magnetic field
    hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 + _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3;
    hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 + my * q2q2 + _2q2 * mz * q3 - my * q3q3;
    _2bx = sqrtf(hx * hx + hy * hy);
    _2bz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3 - mz * q1q1 + _2q2 * my * q3 - mz * q2q2 + mz * q3q3;
    _4bx = 2.0f * _2bx;
    _4bz = 2.0f * _2bz;

    // Gradient descent
    s0 = -_2q2 * (2.0f * q1q3 - _2q0q2 - ax) + _2q1 * (2.0f * q0q1 + _2q2q3 - ay) - _2bz * q2 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q3 + _2bz * q1) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q2 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    s1 = _2q3 * (2.0f * q1q3 - _2q0q2 - ax) + _2q0 * (2.0f * q0q1 + _2q2q3 - ay) - 4.0f * q1 * (1.0f - 2.0f * q1q1 - 2.0f * q2q2 - az) + _2bz * q3 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q2 + _2bz * q0) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q3 - _4bz * q1) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    s2 = -_2q0 * (2.0f * q1q3 - _2q0q2 - ax) + _2q3 * (2.0f * q0q1 + _2q2q3 - ay) - 4.0f * q2 * (1.0f - 2.0f * q1q1 - 2.0f * q2q2 - az) + (-_4bx * q2 - _2bz * q0) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q1 + _2bz * q3) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q0 - _4bz * q2) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    s3 = _2q1 * (2.0f * q1q3 - _2q0q2 - ax) + _2q2 * (2.0f * q0q1 + _2q2q3 - ay) + (-_4bx * q3 + _2bz * q1) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q0 + _2bz * q2) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q1 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);

    recipNorm = 1.0f / sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
    s0 *= recipNorm;
    s1 *= recipNorm;
    s2 *= recipNorm;
    s3 *= recipNorm;

    // Rate of change of quaternion
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz) - beta * s0;
    qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy) - beta * s1;
    qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx) - beta * s2;
    qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx) - beta * s3;

    // Integrate
    q0 += qDot1 * dt;
    q1 += qDot2 * dt;
    q2 += qDot3 * dt;
    q3 += qDot4 * dt;

    // Normalize quaternion
    recipNorm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= recipNorm;
    q1 *= recipNorm;
    q2 *= recipNorm;
    q3 *= recipNorm;
}

void MPU9250_Sensor::madgwickUpdateIMU(float ax, float ay, float az,
                                       float gx, float gy, float gz,
                                       float dt) {
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2, _8q1, _8q2;
    float q0q0, q1q1, q2q2, q3q3;

    // Normalize accelerometer
    recipNorm = 1.0f / sqrtf(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    // Auxiliary variables
    _2q0 = 2.0f * q0;
    _2q1 = 2.0f * q1;
    _2q2 = 2.0f * q2;
    _2q3 = 2.0f * q3;
    _4q0 = 4.0f * q0;
    _4q1 = 4.0f * q1;
    _4q2 = 4.0f * q2;
    _8q1 = 8.0f * q1;
    _8q2 = 8.0f * q2;
    q0q0 = q0 * q0;
    q1q1 = q1 * q1;
    q2q2 = q2 * q2;
    q3q3 = q3 * q3;

    // Gradient descent
    s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
    s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
    s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
    s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;

    recipNorm = 1.0f / sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
    s0 *= recipNorm;
    s1 *= recipNorm;
    s2 *= recipNorm;
    s3 *= recipNorm;

    // Rate of change of quaternion
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz) - beta * s0;
    qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy) - beta * s1;
    qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx) - beta * s2;
    qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx) - beta * s3;

    // Integrate
    q0 += qDot1 * dt;
    q1 += qDot2 * dt;
    q2 += qDot3 * dt;
    q3 += qDot4 * dt;

    // Normalize quaternion
    recipNorm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= recipNorm;
    q1 *= recipNorm;
    q2 *= recipNorm;
    q3 *= recipNorm;
}

// Public API
bool MPU9250_Sensor::begin() {
    Serial.println("\nMPU9250 Initialization");

    // Try both I2C addresses
    if (!detectMPU()) {
        mpuAddr = (mpuAddr == MPU_ADDR_LOW) ? MPU_ADDR_HIGH : MPU_ADDR_LOW;
        if (!detectMPU()) {
            Serial.println("MPU9250 not found!");
            return false;
        }
    }

    Serial.printf("MPU6050/MPU9250 found at 0x%02X\n", mpuAddr);
    initMPU();

    // 6-DOF mode only (no magnetometer)
    magMode = MAG_NONE;
    Serial.println("6-axis IMU mode (accel + gyro only)");
    Serial.println("PCB coordinate mapping: X->Yaw, Y->Pitch");
    Serial.println("IMU ready!\n");
    initialized = true;
    lastUpdate = millis();
    return true;
}

bool MPU9250_Sensor::isReady() {
    return initialized;
}

void MPU9250_Sensor::update() {
    if (!initialized) return;

    unsigned long now = millis();
    float dt = (now - lastUpdate) / 1000.0f;
    lastUpdate = now;

    // Read accel/gyro
    readAccelGyro();

    // Update Madgwick filter (6-DOF only, no magnetometer)
    madgwickUpdateIMU(ax, ay, az, gx, gy, gz, dt);
}

// Sensor Data Accessors
float MPU9250_Sensor::getAccelX_mss() { return ax; }
float MPU9250_Sensor::getAccelY_mss() { return ay; }
float MPU9250_Sensor::getAccelZ_mss() { return az; }
float MPU9250_Sensor::getGyroX_rads() { return gx; }
float MPU9250_Sensor::getGyroY_rads() { return gy; }
float MPU9250_Sensor::getGyroZ_rads() { return gz; }
float MPU9250_Sensor::getMagX_uT() { return magOK ? mx : 0.0f; }
float MPU9250_Sensor::getMagY_uT() { return magOK ? my : 0.0f; }
float MPU9250_Sensor::getMagZ_uT() { return magOK ? mz : 0.0f; }
float MPU9250_Sensor::getTemperature_C() { return temp; }

// Fused Orientation (Quaternion)
float MPU9250_Sensor::getFusedQuatW() { return q0; }
float MPU9250_Sensor::getFusedQuatX() { return q1; }
float MPU9250_Sensor::getFusedQuatY() { return q2; }
float MPU9250_Sensor::getFusedQuatZ() { return q3; }

void MPU9250_Sensor::resetCalibration() {
    calibrationReady = false;
    for (int i = 0; i < 3; ++i) {
        accelMin[i] = -9.81f;
        accelMax[i] = 9.81f;
        gyroMin[i] = -2.0f;
        gyroMax[i] = 2.0f;
    }
}

float MPU9250_Sensor::normalizeAxis(float value, float minValue, float maxValue) const {
    const float span = maxValue - minValue;
    if (span <= 1e-5f) {
        return 0.5f;
    }
    float normalized = (value - minValue) / span;
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    return normalized;
}

void MPU9250_Sensor::getNormalizedReadings(float* accelOut, float* gyroOut) const {
    if (!calibrationReady) {
        if (accelOut) {
            accelOut[0] = accelOut[1] = accelOut[2] = 0.5f;
        }
        if (gyroOut) {
            gyroOut[0] = gyroOut[1] = gyroOut[2] = 0.5f;
        }
        return;
    }

    if (accelOut) {
        accelOut[0] = normalizeAxis(ax, accelMin[0], accelMax[0]);
        accelOut[1] = normalizeAxis(ay, accelMin[1], accelMax[1]);
        accelOut[2] = normalizeAxis(az, accelMin[2], accelMax[2]);
    }
    if (gyroOut) {
        gyroOut[0] = normalizeAxis(gx, gyroMin[0], gyroMax[0]);
        gyroOut[1] = normalizeAxis(gy, gyroMin[1], gyroMax[1]);
        gyroOut[2] = normalizeAxis(gz, gyroMin[2], gyroMax[2]);
    }
}

bool MPU9250_Sensor::runCalibrationRoutine(uint32_t durationMs) {
    if (!initialized) {
        Serial.println("[IMU] Cannot calibrate: sensor not initialized.");
        return false;
    }

    Serial.println("\nIMU CALIBRATION");
    Serial.println("----------------------------------------");
    Serial.println("Move the glove slowly through your full range of motion.");
    Serial.println("Rotate and tilt along every axis to cover typical extremes.");
    Serial.println("Press any key to start...");

    while (!Serial.available()) {
        delay(100);
    }
    while (Serial.available()) Serial.read();

    Serial.println("\nCalibration starting in:");
    for (int i = 3; i > 0; --i) {
        Serial.printf("%d...\n", i);
        delay(1000);
    }
    Serial.println("GO!\n");

    float accMin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float accMax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    float gyroMinVals[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float gyroMaxVals[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    const unsigned long start = millis();
    unsigned long samples = 0;
    while (millis() - start < durationMs) {
        update();

        float accVals[3] = {ax, ay, az};
        float gyroVals[3] = {gx, gy, gz};

        for (int i = 0; i < 3; ++i) {
            if (accVals[i] < accMin[i]) accMin[i] = accVals[i];
            if (accVals[i] > accMax[i]) accMax[i] = accVals[i];
            if (gyroVals[i] < gyroMinVals[i]) gyroMinVals[i] = gyroVals[i];
            if (gyroVals[i] > gyroMaxVals[i]) gyroMaxVals[i] = gyroVals[i];
        }

        samples++;
        if (samples % 50 == 0) {
            Serial.print(".");
        }

        delay(10);
    }

    Serial.println("\nDone!\n");

    auto sanitizeRange = [](float& minVal, float& maxVal, float fallbackSpan) {
        if (!isfinite(minVal) || !isfinite(maxVal) || maxVal - minVal < 0.01f) {
            float center = (isfinite(minVal) && isfinite(maxVal)) ? (minVal + maxVal) * 0.5f : 0.0f;
            minVal = center - fallbackSpan;
            maxVal = center + fallbackSpan;
        }
    };

    for (int i = 0; i < 3; ++i) {
        sanitizeRange(accMin[i], accMax[i], 2.0f);
        sanitizeRange(gyroMinVals[i], gyroMaxVals[i], 1.0f);
        accelMin[i] = accMin[i];
        accelMax[i] = accMax[i];
        gyroMin[i] = gyroMinVals[i];
        gyroMax[i] = gyroMaxVals[i];
    }

    calibrationReady = true;
    printCalibrationInfo();
    Serial.println("IMU normalization now maps calibrated ranges to 0-1.\n");
    return true;
}

void MPU9250_Sensor::printCalibrationInfo() const {
    Serial.println("IMU Calibration Information");
    if (!calibrationReady) {
        Serial.println("IMU not calibrated. Use 'u' to run the calibration routine.\n");
        return;
    }

    const char* axis[3] = {"X", "Y", "Z"};
    Serial.println("Accelerometer ranges (m/s^2):");
    for (int i = 0; i < 3; ++i) {
        Serial.printf("  %s: %.2f to %.2f\n", axis[i], accelMin[i], accelMax[i]);
    }
    Serial.println("Gyroscope ranges (rad/s):");
    for (int i = 0; i < 3; ++i) {
        Serial.printf("  %s: %.2f to %.2f\n", axis[i], gyroMin[i], gyroMax[i]);
    }
    Serial.println();
}
