import serial
import time
import sys

def check_serial(port="COM5", baudrate=115200, timeout=30):
    print(f"[CHECK] Opening serial port {port} at {baudrate} baud...")
    try:
        # Open port
        ser = serial.Serial(port, baudrate, timeout=1)
        
        # Trigger reset via DTR/RTS
        print("[CHECK] Resetting ESP32 via DTR/RTS...")
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        time.sleep(0.5)
        
        print("[CHECK] Reading serial output for 5 seconds...")
        start_time = time.time()
        while time.time() - start_time < timeout:
            if ser.in_waiting > 0:
                line = ser.readline()
                try:
                    decoded = line.decode('utf-8', errors='ignore').strip()
                    if decoded:
                        print(f"  {decoded}")
                except Exception as e:
                    print(f"  [RAW]: {line}")
            else:
                time.sleep(0.01)
                
        ser.close()
        print("[CHECK] Done.")
    except Exception as e:
        print(f"[CHECK] Error: {e}", file=sys.stderr)

if __name__ == "__main__":
    check_serial()
