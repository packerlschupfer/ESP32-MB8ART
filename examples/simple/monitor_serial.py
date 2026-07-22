#!/usr/bin/env python3
import serial
import time
import sys

# Configure serial port
port = '/dev/ttyACM0'
baudrate = 115200

try:
    # Open serial port
    ser = serial.Serial(port, baudrate, timeout=1)
    print(f"Connected to {port} at {baudrate} baud")
    print("Monitoring serial output (Ctrl+C to stop)...")
    print("-" * 50)
    
    # Read and display data
    start_time = time.time()
    while time.time() - start_time < 15:  # Run for 15 seconds
        if ser.in_waiting > 0:
            data = ser.readline()
            try:
                # Try to decode as UTF-8
                text = data.decode('utf-8').rstrip()
                print(text)
            except UnicodeDecodeError:
                # If decoding fails, print raw bytes
                print(f"Raw bytes: {data}")
                
except serial.SerialException as e:
    print(f"Error opening serial port: {e}")
except KeyboardInterrupt:
    print("\nStopped by user")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
        print("Serial port closed")