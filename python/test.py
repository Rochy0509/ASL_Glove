import serial
import serial.tools.list_ports
import csv
from datetime import datetime
import time

# Configuration - will auto-detect if this doesn't work
BAUD_RATE = 115200

def find_arduino_port():
    """Automatically find Arduino/ESP32 port"""
    ports = serial.tools.list_ports.comports()
    
    # Look for common Arduino/ESP32 identifiers
    arduino_keywords = ['Arduino', 'CH340', 'CP2102', 'USB Serial', 'USB-SERIAL', 'UART']
    
    print("Available serial ports:")
    for port in ports:
        print(f"  {port.device}: {port.description}")
        # Check if this looks like an Arduino
        for keyword in arduino_keywords:
            if keyword.lower() in port.description.lower():
                return port.device
    
    # If no auto-detection, return first available port or ask user
    if len(ports) > 0:
        return ports[0].device
    
    return None

def parse_data_line(line):
    """Parse ESP32 output"""
    if '|' not in line or '---' in line or 'Raw V' in line:
        return None
    
    try:
        parts = [p.strip() for p in line.split('|')]
        if len(parts) >= 3:
            raw_voltage = float(parts[0])
            filtered_voltage = float(parts[1])
            angle_str = parts[2].replace('°', '').strip()
            angle = float(angle_str)
            return {
                'raw_voltage': raw_voltage,
                'filtered_voltage': filtered_voltage,
                'angle': angle
            }
    except (ValueError, IndexError):
        return None
    return None

def main():
    print("=" * 60)
    print("FSR DIAGNOSTIC TOOL")
    print("=" * 60)
    
    # Auto-detect serial port
    print("\nDetecting Arduino/ESP32...")
    serial_port = find_arduino_port()
    
    if serial_port is None:
        print("\n❌ ERROR: No serial ports found!")
        print("   Make sure your Arduino/ESP32 is connected via USB.")
        return
    
    print(f"\n✓ Found device on: {serial_port}")
    
    # Allow user to override
    user_input = input(f"\nUse {serial_port}? (Press Enter or type different port): ").strip()
    if user_input:
        serial_port = user_input
    
    print("\nThis tool helps you verify your sensor behavior.\n")
    print("INSTRUCTIONS:")
    print("1. Start with your finger FULLY EXTENDED (straight)")
    print("2. Slowly CLOSE your hand into a fist")
    print("3. Watch if voltage goes UP or DOWN")
    print("4. Press Ctrl+C when done\n")
    print("=" * 60)
    
    input("\nPress Enter when your finger is FULLY EXTENDED...")
    
    ser = None
    
    try:
        # Try to open serial port
        print(f"\nOpening {serial_port}...")
        ser = serial.Serial(serial_port, BAUD_RATE, timeout=1)
        time.sleep(2)  # Give Arduino time to reset
        print("Serial port opened. Starting monitoring...\n")
        
        readings = []
        max_voltage = 0
        min_voltage = 5.0
        start_time = time.time()
        
        print("TIME | RAW VOLTAGE | TREND")
        print("-----|-------------|-------")
        
        last_voltage = None
        
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                data = parse_data_line(line)
                
                if data is not None:
                    voltage = data['raw_voltage']
                    elapsed = time.time() - start_time
                    
                    # Track min/max
                    if voltage > max_voltage:
                        max_voltage = voltage
                    if voltage < min_voltage:
                        min_voltage = voltage
                    
                    # Determine trend
                    if last_voltage is not None:
                        diff = voltage - last_voltage
                        if abs(diff) < 0.01:
                            trend = "→ Stable"
                        elif diff > 0:
                            trend = "↑ Increasing"
                        else:
                            trend = "↓ Decreasing"
                    else:
                        trend = "  Starting"
                    
                    # Display
                    print(f"{elapsed:4.1f}s | {voltage:6.3f}V     | {trend}")
                    
                    readings.append({
                        'time': elapsed,
                        'voltage': voltage
                    })
                    
                    last_voltage = voltage
                    
    except KeyboardInterrupt:
        print("\n\n" + "=" * 60)
        print("DIAGNOSTIC RESULTS")
        print("=" * 60)
        print(f"\nTotal readings: {len(readings)}")
        print(f"Voltage range: {min_voltage:.3f}V to {max_voltage:.3f}V")
        print(f"Voltage span: {max_voltage - min_voltage:.3f}V")
        
        if len(readings) > 10:
            first_avg = sum(r['voltage'] for r in readings[:5]) / 5
            last_avg = sum(r['voltage'] for r in readings[-5:]) / 5
            
            print(f"\nStarting voltage (extended): {first_avg:.3f}V")
            print(f"Ending voltage (bent): {last_avg:.3f}V")
            
            if last_avg < first_avg:
                print("\n✓ VOLTAGE DECREASES when bending (CORRECT)")
                print("  Your calibration should be fine.")
            elif last_avg > first_avg:
                print("\n✗ VOLTAGE INCREASES when bending (INVERTED!)")
                print("  Your sensor is mounted backwards or")
                print("  pressure decreases when you bend.")
                print("\n  SOLUTION: Flip your calibration values")
            else:
                print("\n⚠ NO CHANGE detected")
                print("  The sensor may not be making proper contact.")
        
        print("\n" + "=" * 60)
        print("MOUNTING TIPS:")
        print("=" * 60)
        print("For FSRs to work as bend sensors, you need:")
        print("1. Mount FSR on the BACK of your finger (over the knuckle)")
        print("2. Add a FOAM PAD or soft dome on top of the FSR")
        print("3. Use an elastic strap that goes around the finger")
        print("4. As finger bends, the strap pulls the foam into the FSR")
        print("\nAlternatively:")
        print("- Mount FSR between two finger segments with foam")
        print("- Bending squeezes the foam against the FSR")
        print("=" * 60)
    
    except serial.SerialException as e:
        print(f"\n❌ ERROR: Could not open serial port {serial_port}")
        print(f"   Error: {e}")
        print("\nTroubleshooting:")
        print("1. Make sure your device is connected via USB")
        print("2. Close any other programs using the serial port (Arduino IDE, etc.)")
        print("3. Try unplugging and replugging the device")
        print("4. Check Device Manager (Windows) to see the COM port")
        print("\nTry one of these ports instead:")
        ports = serial.tools.list_ports.comports()
        for port in ports:
            print(f"   {port.device}")
    
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")
        
    finally:
        if ser is not None and ser.is_open:
            ser.close()
            print("\nSerial port closed.")

if __name__ == "__main__":
    main()