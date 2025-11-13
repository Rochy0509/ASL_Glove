import serial
import serial.tools.list_ports
import csv
import re
from datetime import datetime

# Configuration
SERIAL_PORT = 'COM15'  # ESP32 CH343 chip
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

def find_esp32_port():
    """Auto-detect ESP32 port - prioritize CH343/CH340/CP2102"""
    ports = serial.tools.list_ports.comports()
    
    # Priority list: CH343, CH340, CP2102 (most common ESP32 chips)
    priority_keywords = ['CH343', 'CH340', 'CP2102']
    secondary_keywords = ['USB Serial', 'USB-SERIAL', 'UART', 'Silicon Labs', 'ESP']
    
    print("Available serial ports:")
    for port in ports:
        print(f"  {port.device}: {port.description}")
    
    # First pass: look for priority chips
    for port in ports:
        for keyword in priority_keywords:
            if keyword.lower() in port.description.lower():
                print(f"✓ Found ESP32 on {port.device} ({keyword})")
                return port.device
    
    # Second pass: look for secondary identifiers
    for port in ports:
        for keyword in secondary_keywords:
            if keyword.lower() in port.description.lower():
                print(f"✓ Found device on {port.device} ({keyword})")
                return port.device
    
    # Fall back to first available port
    if len(ports) > 0:
        print(f"✓ Using first available port: {ports[0].device}")
        return ports[0].device
    
    return None

def main():
    # Auto-detect port if not hardcoded
    port = SERIAL_PORT
    if port is None:
        print("Auto-detecting ESP32 port...")
        port = find_esp32_port()
        if port is None:
            print("❌ ERROR: No serial ports found!")
            print("   Make sure your ESP32 is connected via USB")
            return
    
    print(f"Opening serial port {port} at {BAUD_RATE} baud...")
    
    try:
        # Open serial connection
        ser = serial.Serial(port, BAUD_RATE, timeout=1)
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
        print(f"❌ PERMISSION ERROR on {port}")
        print(f"   Error: {e}")
        print("\n🔧 SOLUTIONS:")
        print("   1. Close Arduino IDE or any other serial monitor")
        print("   2. Close VS Code serial monitor (if open)")
        print("   3. Unplug ESP32, wait 5 seconds, plug back in")
        print("   4. Try Device Manager - right-click port > Properties > Port Settings")
        print("   5. Try a different USB cable or USB port on your computer")
        print("\nAvailable ports:")
        ports = serial.tools.list_ports.comports()
        if ports:
            for p in ports:
                print(f"  - {p.device}: {p.description}")
        else:
            print("  - No ports found! Check USB connection.")
            
    except KeyboardInterrupt:
        print("\n\nStopping data collection...")
        
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("Serial port closed.")
        print(f"Data saved to {OUTPUT_FILE}")

if __name__ == "__main__":
    main()