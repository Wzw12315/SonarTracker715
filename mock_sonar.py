import socket
import struct
import time
import os
import glob
import re
import numpy as np
import threading

# ==================== 配置区 ====================
UDP_IP = "127.0.0.1"
UDP_PORT = 8888
CHUNK_SIZE = 51200
MAGIC_WORD = 0xAA55AA55

LISTEN_IP = "0.0.0.0"
LISTEN_PORT = 8889

is_running = False
is_paused = False
should_exit = False
# ===============================================

parent_folder = "/home/wzw/SonarTracker715/DATA/ship_DATA_snap600"
raw_files = glob.glob(os.path.join(parent_folder, "**/*.raw"), recursive=True)

if not raw_files:
    print("❌ 未找到任何 raw 文件，请检查路径。")
    exit()

time_to_files = {}
time_pattern = re.compile(r'_(\d+(?:\.\d+)?)s\.raw$')
for fpath in raw_files:
    match = time_pattern.search(fpath)
    if match:
        time_val = float(match.group(1))
        if time_val not in time_to_files:
            time_to_files[time_val] = []
        time_to_files[time_val].append(fpath)

sorted_times = sorted(time_to_files.keys())


def command_listener():
    global is_running, is_paused, should_exit
    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listen_sock.bind((LISTEN_IP, LISTEN_PORT))
    print(f"🎧 [控制端] 正在后台监听控制指令 (端口: {LISTEN_PORT})...")

    while not should_exit:
        try:
            data, addr = listen_sock.recvfrom(1024)
            cmd = data.decode('utf-8').strip()
            print(f"📩 [总控台] 收到远端指令: {cmd}")
            if cmd == "CMD:START":
                is_running = True
                is_paused = False
            elif cmd == "CMD:PAUSE":
                is_paused = True
            elif cmd == "CMD:RESUME":
                is_paused = False
            elif cmd == "CMD:STOP":
                is_running = False
        except:
            pass


threading.Thread(target=command_listener, daemon=True).start()

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024 * 1024 * 32)

try:
    print("\n⏳ 引擎就绪！等待 C++ 总控台下发 [开始] 指令...")
    while not should_exit:
        if not is_running:
            time.sleep(0.5)
            continue

        for frame_idx, t_val in enumerate(sorted_times, start=1):
            if not is_running or should_exit:
                break
            while is_paused and not should_exit and is_running:
                time.sleep(0.1)
            if not is_running: break

            files_for_this_frame = time_to_files[t_val]
            print(f"▶ 正在发送 第 {frame_idx} 帧 (对应时间: {t_val}s)")
            summed_data = None

            for file_path in files_for_this_frame:
                file_arr = np.fromfile(file_path, dtype=np.float32)
                if summed_data is None:
                    summed_data = file_arr.copy()
                else:
                    summed_data += file_arr

            frame_bytes = summed_data.tobytes()
            chunks = [frame_bytes[i:i + CHUNK_SIZE] for i in range(0, len(frame_bytes), CHUNK_SIZE)]
            total_chunks = len(chunks)

            # ========================================================
            # 【新增】：模拟本舰传感器数据，随着帧数增加产生逼真的物理微动
            # ========================================================
            sim_lon = 118.395833 + frame_idx * 0.0001  # 约 118°23'45" E (缓慢向东移动)
            sim_lat = 32.253333 + frame_idx * 0.00005  # 约 32°15'12" N (缓慢向北移动)
            sim_hdg = 45.5 + frame_idx * 0.05  # 艏向角缓慢右转

            for chunk_idx, chunk in enumerate(chunks):
                # 格式变为 <IIIIIfff，即 5个无符号整型 + 3个浮点数
                header = struct.pack('<IIIIIfff', MAGIC_WORD, frame_idx, chunk_idx, total_chunks, len(chunk), sim_lon,
                                     sim_lat, sim_hdg)
                sock.sendto(header + chunk, (UDP_IP, UDP_PORT))
                time.sleep(0.002)

        if is_running:
            print("\n🎉 当前所有数据帧发送完毕！等待下一轮指令...")
            is_running = False

except KeyboardInterrupt:
    print("\n🛑 手动关闭。")
    should_exit = True
finally:
    sock.close()