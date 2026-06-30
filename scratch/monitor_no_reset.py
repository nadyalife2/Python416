import serial
import time
import sys

def monitor(port="COM5", baudrate=115200, duration=60):
    print(f"[MONITOR] Opening serial port {port} (strictly NO reset) for {duration} seconds...")
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = baudrate
        ser.timeout = 1
        # Prevent reset on open
        ser.dtr = False
        ser.rts = False
        ser.open()
        
        print("[MONITOR] Connected successfully. Please speak to Melvin now.")
        start_time = time.time()
        while time.time() - start_time < duration:
            if ser.in_waiting > 0:
                line = ser.readline()
                try:
                    decoded = line.decode('utf-8', errors='ignore').strip()
                    if decoded:
                        elapsed = time.time() - start_time
                        print(f"[{elapsed:6.2f}s] {decoded}")
                except Exception as e:
                    print(f"  [RAW]: {line}")
            else:
                time.sleep(0.01)
        ser.close()
        print("[MONITOR] Stopped.")
    except Exception as e:
        print(f"[MONITOR] Error: {e}", file=sys.stderr)

if __name__ == "__main__":
    import sys
    dur = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    monitor(duration=dur)
