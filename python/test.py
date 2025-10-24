import serial
import serial.tools.list_ports
import csv
from datetime import datetime
import time

# Configuration
SERIAL_PORT = '/dev/ttyUSB0'
BAUD_RATE = 115200

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
    print("\nThis tool helps you verify your sensor behavior.\n")
    print("INSTRUCTIONS:")
    print("1. Start with your finger FULLY EXTENDED (straight)")
    print("2. Slowly CLOSE your hand into a fist")
    print("3. Watch if voltage goes UP or DOWN")
    print("4. Press Ctrl+C when done\n")
    print("=" * 60)
    
    input("\nPress Enter when your finger is FULLY EXTENDED...")
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print("\nSerial port opened. Starting monitoring...\n")
        
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
        
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()

if __name__ == "__main__":
    main()https://www.amazon.ca/spr/returns/cart?orderId=701-1722588-0958607&ref=ppx_yo2ov_dt_b_return_items