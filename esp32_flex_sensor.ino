/*
  ASL Glove - Flex Sensor Reader for ESP32 S3
  Reads flex sensors on analog pins and sends calibrated angle readings
  over Serial at 115200 baud
*/

// Configuration
#define NUM_SENSORS 5  // 5 flex sensors (one per finger)

// ADC Pins for flex sensors (change these based on your wiring)
// ESP32 S3 has multiple ADC pins available
const int flexPins[NUM_SENSORS] = {
  A0,   // Thumb
  A1,   // Index
  A2,   // Middle
  A3,   // Ring
  A4    // Pinky
};

// Sensor calibration values (adjust these based on your sensors)
// min = straight finger, max = fully bent finger
const int flexMin[NUM_SENSORS] = {200, 200, 200, 200, 200};    // Adjust based on extended reading
const int flexMax[NUM_SENSORS] = {900, 900, 900, 900, 900};    // Adjust based on bent reading

// Sensor names for debug output
const char* sensorNames[NUM_SENSORS] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};

// Data smoothing with exponential moving average
float smoothedReadings[NUM_SENSORS] = {0};
const float SMOOTHING_FACTOR = 0.3;  // Lower = more smoothing, Higher = more responsive

// Timing variables
unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL = 100;  // Print every 100ms

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(1000);  // Wait for serial connection to stabilize
  
  Serial.println("\n\n===========================================");
  Serial.println("    ASL GLOVE - FLEX SENSOR READER");
  Serial.println("    ESP32 S3 Dev Kit");
  Serial.println("===========================================\n");
  
  // Initialize ADC
  analogReadResolution(12);  // 12-bit resolution (0-4095)
  
  // Print calibration info
  Serial.println("FLEX SENSOR CALIBRATION VALUES:");
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.printf("  %s: Min=%d, Max=%d\n", sensorNames[i], flexMin[i], flexMax[i]);
  }
  
  Serial.println("\nStarting measurements...");
  Serial.println("Raw V | Filtered V | Angle | Status");
  Serial.println("------|------------|-------|----------");
}

void loop() {
  unsigned long currentTime = millis();
  
  // Read all sensors
  if (currentTime - lastPrintTime >= PRINT_INTERVAL) {
    lastPrintTime = currentTime;
    
    for (int i = 0; i < NUM_SENSORS; i++) {
      // Read raw ADC value
      int rawValue = analogRead(flexPins[i]);
      
      // Convert to voltage (12-bit ADC, 3.3V reference)
      float voltage = (rawValue / 4095.0) * 3.3;
      
      // Apply exponential moving average smoothing
      smoothedReadings[i] = (SMOOTHING_FACTOR * voltage) + 
                            ((1 - SMOOTHING_FACTOR) * smoothedReadings[i]);
      
      // Convert voltage to angle (0-90 degrees)
      // Assuming flex sensor reduces resistance when bent (most common)
      int angle = map(rawValue, flexMin[i], flexMax[i], 0, 90);
      angle = constrain(angle, 0, 90);  // Clamp to 0-90 range
      
      // Determine status
      const char* status = "Extended";
      if (angle > 60) {
        status = "Bent";
      } else if (angle > 30) {
        status = "Partial";
      }
      
      // Send formatted output
      Serial.printf("%.3f | %.3f | %d° | %s\n", 
                    voltage, 
                    smoothedReadings[i], 
                    angle, 
                    status);
    }
    
    // Print separator every 10 readings
    static int readingCount = 0;
    if (++readingCount >= 10) {
      Serial.println("------|------------|-------|----------");
      readingCount = 0;
    }
  }
}
