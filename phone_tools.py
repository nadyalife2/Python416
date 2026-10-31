"""
Melvin On-Device Phone Tools
Direct low-latency hardware and OS interfaces for Xiaomi Redmi Note 4 (Helio X20 MT6797)
Running inside Alpine PRoot / Android environment.
"""

import os
import subprocess
import sqlite3
import re
import time

DB_PATH = "/melvin/data/agent_memory.db"
if not os.path.exists(os.path.dirname(DB_PATH)):
    DB_PATH = "agent_memory.db"

def get_battery():
    """Returns battery percentage, status, and temperature."""
    try:
        # Check standard Linux/Android sysfs paths
        cap_path = "/sys/class/power_supply/battery/capacity"
        temp_path = "/sys/class/power_supply/battery/temp"
        stat_path = "/sys/class/power_supply/battery/status"
        
        cap = "unknown"
        temp = "unknown"
        status = "discharging"
        
        if os.path.exists(cap_path):
            with open(cap_path, "r") as f:
                cap = f.read().strip() + "%"
        if os.path.exists(temp_path):
            with open(temp_path, "r") as f:
                val = int(f.read().strip())
                temp = f"{round(val / 10.0, 1)}°C" if val > 100 else f"{val}°C"
        if os.path.exists(stat_path):
            with open(stat_path, "r") as f:
                status = f.read().strip()
                
        if cap != "unknown":
            return {"battery": cap, "temp": temp, "status": status}
            
        # Fallback to dumpsys if inside Android shell
        out = subprocess.check_output("dumpsys battery 2>/dev/null || true", shell=True).decode()
        level = re.search(r'level:\s*(\d+)', out)
        temperature = re.search(r'temperature:\s*(\d+)', out)
        return {
            "battery": (level.group(1) + "%") if level else "N/A",
            "temp": (str(round(int(temperature.group(1))/10.0, 1)) + "°C") if temperature else "N/A",
            "status": "online"
        }
    except Exception as e:
        return {"error": str(e)}

def get_hardware_status():
    """Returns RAM usage and CPU temperature/load on Helio X20."""
    try:
        mem_info = {}
        if os.path.exists("/proc/meminfo"):
            with open("/proc/meminfo", "r") as f:
                for line in f:
                    parts = line.split(":")
                    if parts[0] in ["MemTotal", "MemAvailable", "MemFree"]:
                        mem_info[parts[0]] = parts[1].strip()
                        
        cpu_temp = "N/A"
        for tz in ["/sys/class/thermal/thermal_zone0/temp", "/sys/class/thermal/thermal_zone1/temp"]:
            if os.path.exists(tz):
                with open(tz, "r") as f:
                    raw = int(f.read().strip())
                    cpu_temp = f"{round(raw/1000.0, 1)}°C" if raw > 1000 else f"{raw}°C"
                    break

        load = "N/A"
        if os.path.exists("/proc/loadavg"):
            with open("/proc/loadavg", "r") as f:
                load = f.read().split()[:3]

        avail = mem_info.get("MemAvailable", mem_info.get("MemFree", "N/A"))
        return {
            "ram_available": avail,
            "mem_available": avail,
            "cpu_temp": cpu_temp,
            "load_avg": load
        }
    except Exception as e:
        return {"error": str(e)}

def toggle_torch(state):
    """Turns flashlight on (1) or off (0)."""
    state_str = "1" if str(state).strip() in ["1", "true", "on", "True"] else "0"
    paths = [
        "/sys/class/leds/flashlight/brightness",
        "/sys/class/leds/torch-light/brightness",
        "/sys/class/leds/led:torch_0/brightness",
        "/sys/devices/soc/1000000.pinctrl/leds/flashlight/brightness"
    ]
    done = False
    for p in paths:
        if os.path.exists(p):
            try:
                val = "255" if state_str == "1" else "0"
                with open(p, "w") as f:
                    f.write(val)
                done = True
                break
            except Exception:
                continue
                
    if not done:
        # Try Android camera service broadcast
        cmd = f"service call flashlight {1 if state_str == '1' else 0} 2>/dev/null || true"
        subprocess.call(cmd, shell=True)

    return {"torch": "ON" if state_str == "1" else "OFF"}

def set_torch(state):
    """Alias for toggle_torch for compatibility."""
    return toggle_torch(state)

def search_memory(query, limit=2):
    """Queries SQLite FTS5 database."""
    try:
        conn = sqlite3.connect(DB_PATH)
        cur = conn.cursor()
        cur.execute("""
            SELECT role, content FROM dialog_fts 
            WHERE dialog_fts MATCH ? 
            ORDER BY rank 
            LIMIT ?
        """, (query, limit))
        rows = cur.fetchall()
        conn.close()
        return [{"role": r[0], "text": r[1]} for r in rows]
    except Exception as e:
        return [{"error": str(e)}]

def dispatch_tool(tool_call_str):
    """
    Parses and executes compact calls:
    !bat()
    !sys()
    !torch(1) or !torch(0)
    !mem("query")
    """
    tool_call_str = tool_call_str.strip()
    match = re.match(r'!([a-zA-Z0-9_]+)\((.*)\)', tool_call_str)
    if not match:
        return {"error": f"Invalid tool format: {tool_call_str}"}
        
    func_name, arg = match.group(1), match.group(2).strip().strip('"\'')
    
    if func_name in ["bat", "battery"]:
        return get_battery()
    elif func_name in ["sys", "temp", "hardware"]:
        return get_hardware_status()
    elif func_name in ["torch", "light"]:
        return toggle_torch(arg)
    elif func_name in ["mem", "memory"]:
        return search_memory(arg)
    else:
        return {"error": f"Unknown tool: {func_name}"}
