#!/usr/bin/env python3
import subprocess
import time
import re
import os
import sys

ADB = "/home/alex/android-sdk/platform-tools/adb"

def run_adb(cmd):
    res = subprocess.run(f"{ADB} {cmd}", shell=True, capture_output=True, text=True)
    return res.stdout.strip()

def get_gpu_info():
    clk = run_adb("shell 'cat /sys/class/kgsl/kgsl-3d0/gpuclk 2>/dev/null || cat /sys/class/kgsl/kgsl-3d0/max_gpuclk 2>/dev/null'")
    temp = run_adb("shell 'dumpsys thermalservice | grep -E \"mValue=.*skin\" | head -n 1'")
    status = run_adb("shell 'dumpsys thermalservice | grep -E \"mStatus=\" | head -n 1'")
    return clk, temp, status

def set_debug_cfg(cfg_dict):
    lines = ["safe_mode=0\n", "prewarm=0\n", "log_pipelines=0\n", "no_atest_depth=0\n"]
    for k, v in cfg_dict.items():
        lines.append(f"{k}={v}\n")
    content = "".join(lines)
    with open("/tmp/debug_test.cfg", "w") as f:
        f.write(content)
    run_adb("push /tmp/debug_test.cfg /sdcard/Gothic2/debug.cfg")

def run_test(name, cfg_dict):
    print(f"\n=======================================================")
    print(f"RUNNING TEST: {name} (cfg={cfg_dict})")
    print(f"=======================================================")
    set_debug_cfg(cfg_dict)
    
    # Restart app and clear log
    run_adb("shell 'echo -n \"\" > /sdcard/Gothic2/opengothic_log.txt'")
    run_adb("shell am force-stop com.opengothic")
    run_adb("shell am start -n com.opengothic/.GameActivity --es extra_game_dir \"/sdcard/Gothic2\"")
    
    # Wait for game to reach main menu
    time.sleep(3.5)
    # Select Новая игра -> press ENTER
    run_adb("shell input keyevent 66")
    time.sleep(1.5)
    # Skip cutscene: press ESC twice
    run_adb("shell input keyevent 111")
    time.sleep(0.5)
    run_adb("shell input keyevent 111")
    time.sleep(1.0)
    # If exit dialog popped up, press ENTER to dismiss "Нет"
    run_adb("shell input keyevent 66")
    time.sleep(4.0)
    
    # Finish Xardas dialogue: tap SKIP multiple times, select option, exit dialog
    for _ in range(4):
        run_adb("shell input tap 2335 680")
        time.sleep(0.4)
    run_adb("shell input keyevent 66")
    time.sleep(0.8)
    run_adb("shell input keyevent 111")
    time.sleep(0.4)
    run_adb("shell input keyevent 111")
    time.sleep(2.5)
    
    # Standing still in Xardas's tower! Collect log lines
    logs = run_adb("shell 'tail -n 60 /sdcard/Gothic2/opengothic_log.txt'")
    perf_lines = [l for l in logs.split("\n") if "[PERF] frame=" in l]
    pass_lines = [l for l in logs.split("\n") if "[PERF_PASS]" in l]
    
    fps_vals = []
    gpu_vals = []
    for l in perf_lines[-5:]:
        m_fps = re.search(r'fps=([\d\.]+)', l)
        m_gpu = re.search(r'gpu=(\d+)', l)
        if m_fps: fps_vals.append(float(m_fps.group(1)))
        if m_gpu: gpu_vals.append(float(m_gpu.group(1)))
        
    avg_fps = sum(fps_vals)/len(fps_vals) if fps_vals else 0.0
    avg_gpu = sum(gpu_vals)/len(gpu_vals) if gpu_vals else 0.0
    raw_perf = perf_lines[-1] if perf_lines else "NO [PERF] DATA"
    raw_pass = pass_lines[-1] if pass_lines else "NO [PERF_PASS] DATA"
    
    clk, temp, status = get_gpu_info()
    
    print(f"FPS: {avg_fps:.2f}, GPU CPU-Wait: {avg_gpu:.1f}ms")
    print(f"Raw PERF: {raw_perf}")
    print(f"Raw PASS: {raw_pass}")
    print(f"GPU Clock: {clk}, Thermal: {temp}, Status: {status}")
    
    return {
        "name": name,
        "cfg": cfg_dict,
        "fps": avg_fps,
        "gpu": avg_gpu,
        "raw_perf": raw_perf,
        "raw_pass": raw_pass,
        "clk": clk,
        "temp": temp,
        "status": status
    }

if __name__ == "__main__":
    tests = [
        ("render_scale 100% (Baseline)", {"render_scale": 100}),
        ("render_scale 75%", {"render_scale": 75}),
        ("render_scale 40%", {"render_scale": 40}),
        ("skip_hiz=1 (100%)", {"render_scale": 100, "skip_hiz": 1}),
    ]
    
    results = []
    for name, cfg in tests:
        res = run_test(name, cfg)
        results.append(res)
        
    print("\n\nAll tests completed successfully!")
