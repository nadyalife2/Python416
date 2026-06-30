import serial
import time
import sys

def monitor(port="COM5", baudrate=115200, duration=45):
    print(f"[MONITOR] Opening serial port {port} (no reset) for {duration} seconds...")
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        # Disable DTR/RTS to avoid auto-reset on open
        ser.dtr = False
        ser.rts = False
        time.sleep(0.1)
        
        print("[MONITOR] Listening to serial data... Please speak to Melvin or press the boot button and speak.")
        start_time = time.time()
        while time.time() - start_time < duration:
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
        print("[MONITOR] Stopped.")
    except Exception as e:
        print(f"[MONITOR] Error: {e}", file=sys.stderr)

if __name__ == "__main__":
    monitor()
