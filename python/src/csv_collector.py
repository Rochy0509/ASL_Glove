import serial
import serial.tools.list_ports
import csv
import re
from datetime import datetime

# Configuration
SERIAL_PORT = '/dev/ttyUSB0'
BAUD_RATE = 115200
OUTPUT_FILE = 'finger_angles.csv'

def parse_data_line(line):
    """
    Parse the ESP32 output line format:
    Raw V | Filtered V | Angle | Status
    Example: 3.280 | 3.280 | 0.0° | Extended
    """
    # Skip header lines and separator lines
    if '|' not in line or '---' in line or 'Raw V' in line:
        return None
    
    try:
        parts = [p.strip() for p in line.split('|')]
        
        if len(parts) >= 4:
            raw_voltage = float(parts[0])
            filtered_voltage = float(parts[1])
            angle_str = parts[2].replace('°', '').strip()
            angle = float(angle_str)
            status = parts[3]
            
            return {
                'raw_voltage': raw_voltage,
                'filtered_voltage': filtered_voltage,
                'angle': angle,
                'status': status
            }
    except (ValueError, IndexError) as e:
        # Failed to parse, skip this line
        return None
    
    return None

def main():
    print(f"Opening serial port {SERIAL_PORT} at {BAUD_RATE} baud...")
    
    try:
        # Open serial connection
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print("Serial port opened successfully!")
        print(f"Logging finger angle data to {OUTPUT_FILE}")
        print("Press Ctrl+C to stop\n")
        
        # Open CSV file for writing
        with open(OUTPUT_FILE, 'w', newline='') as csvfile:
            writer = csv.writer(csvfile)
            # Write header
            writer.writerow(['Timestamp', 'Raw Voltage (V)', 'Filtered Voltage (V)', 'Angle (degrees)', 'Status'])
            
            while True:
                # Read line from serial port
                if ser.in_waiting > 0:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    # Display all lines for debugging
                    if line:
                        print(line)
                    
                    # Parse data
                    data = parse_data_line(line)
                    
                    if data is not None:
                        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
                        writer.writerow([
                            timestamp,
                            data['raw_voltage'],
                            data['filtered_voltage'],
                            data['angle'],
                            data['status']
                        ])
                        csvfile.flush()  # Ensure data is written immediately
                        
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        print("\nAvailable ports:")
        ports = serial.tools.list_ports.comports()
        for port in ports:
            print(f"  - {port.device}: {port.description}")
            
    except KeyboardInterrupt:
        print("\n\nStopping data collection...")
        
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("Serial port closed.")
        print(f"Data saved to {OUTPUT_FILE}")

if __name__ == "__main__":
    main()