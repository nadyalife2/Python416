import serial
import time
import sys

def test_sequence(name, dtr_before, rts_before, dtr_active, rts_active, dtr_after, rts_after):
    print(f"\n--- Testing sequence: {name} ---")
    try:
        ser = serial.Serial()
        ser.port = "COM5"
        ser.baudrate = 115200
        ser.timeout = 0.5
        ser.open()
        
        # Set initial states
        ser.dtr = dtr_before
        ser.rts = rts_before
        time.sleep(0.2)
        
        # Assert reset
        ser.dtr = dtr_active
        ser.rts = rts_active
        time.sleep(0.2)
        
        # Release reset
        ser.dtr = dtr_after
        ser.rts = rts_after
        time.sleep(0.5)
        
        # Read output for 3 seconds
        start_time = time.time()
        while time.time() - start_time < 3:
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
    except Exception as e:
        print(f"  Error: {e}")

if __name__ == "__main__":
    # Test 1: Standard esptool hard reset
    test_sequence("Esptool Hard Reset (DTR=False, toggle RTS)", 
                  dtr_before=False, rts_before=False,
                  dtr_active=False, rts_active=True,
                  dtr_after=False, rts_after=False)
                  
    # Test 2: Toggle DTR (RTS=False)
    test_sequence("Toggle DTR (RTS=False)", 
                  dtr_before=False, rts_before=False,
                  dtr_active=True, rts_active=False,
                  dtr_after=False, rts_after=False)

    # Test 3: Toggle both DTR and RTS
    test_sequence("Toggle both DTR and RTS", 
                  dtr_before=False, rts_before=False,
                  dtr_active=True, rts_active=True,
                  dtr_after=False, rts_after=False)
                  
    # Test 4: DTR=True, toggle RTS
    test_sequence("DTR=True, toggle RTS", 
                  dtr_before=True, rts_before=False,
                  dtr_active=True, rts_active=True,
                  dtr_after=True, rts_after=False)

    # Test 5: RTS=True, toggle DTR
    test_sequence("RTS=True, toggle DTR", 
                  dtr_before=False, rts_before=True,
                  dtr_active=True, rts_active=True,
                  dtr_after=False, rts_after=True)
