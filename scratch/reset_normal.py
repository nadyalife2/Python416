import serial
import time
import sys

def reset_to_normal(port="COM5", baudrate=115200, timeout=15):
    print(f"[RESET] Opening serial port {port} at {baudrate} baud...")
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = baudrate
        ser.timeout = 1
        ser.dtr = False
        ser.rts = False
        ser.open()
        
        print("[RESET] Toggling DTR/RTS for normal boot...")
        # Correct sequence for normal boot:
        # 1. Keep IO0 high (DTR=True) to select normal boot mode
        ser.dtr = True
        # 2. Pull EN low (RTS=True) to trigger reset
        ser.rts = True
        time.sleep(0.2)
        # 3. Release EN (RTS=False) to boot the chip
        ser.rts = False
        time.sleep(0.1)
        # 4. Release IO0 (DTR=False)
        ser.dtr = False
        time.sleep(0.5)
        
        print("[RESET] Reading serial output...")
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
        print("[RESET] Done.")
    except Exception as e:
        print(f"[RESET] Error: {e}", file=sys.stderr)

if __name__ == "__main__":
    reset_to_normal()
