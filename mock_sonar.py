import socket
import struct
import time
import os
import glob
import re
import numpy as np  # 必须引入 numpy 用于多目标信号矩阵相加

UDP_IP = "127.0.0.1"
UDP_PORT = 8888
CHUNK_SIZE = 51200
MAGIC_WORD = 0xAA55AA55

# 【修改1：指向父文件夹】
parent_folder = "/home/wzw/SonarTracker715/DATA/ship_DATA_snap600"

# 【修改2：递归搜索父文件夹下的所有 .raw 文件】
# recursive=True 会自动钻进 shipA_raw_files, shipB_raw_files 等所有子目录去找
raw_files = glob.glob(os.path.join(parent_folder, "**/*.raw"), recursive=True)

if not raw_files:
    print("❌ 未找到任何 raw 文件，请检查路径。")
    exit()

# 【修改3：利用正则提取时间戳，将同一个时间的多个目标文件分到同一组】
time_to_files = {}
# 匹配 _0s.raw, _3.5s.raw 等格式，提取前面的数字
time_pattern = re.compile(r'_(\d+(?:\.\d+)?)s\.raw$')

for fpath in raw_files:
    match = time_pattern.search(fpath)
    if match:
        time_val = float(match.group(1))
        if time_val not in time_to_files:
            time_to_files[time_val] = []
        time_to_files[time_val].append(fpath)

# 按时间先后顺序排序帧 (0s, 3s, 6s ...)
sorted_times = sorted(time_to_files.keys())

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024 * 1024 * 32)

try:
    for frame_idx, t_val in enumerate(sorted_times, start=1):
        files_for_this_frame = time_to_files[t_val]
        print(f"▶ 正在发送 第 {frame_idx} 帧 (对应时间: {t_val}s)")
        print(f"  [本帧包含 {len(files_for_this_frame)} 个目标信号源进行声学混合]")

        summed_data = None

        # 【修改4：将本帧所有目标的数据读取出来并物理叠加】
        for file_path in files_for_this_frame:
            print(f"    - 加载: {os.path.basename(file_path)}")
            # 按 32位单精度浮点数 读取二进制文件（与你的 RawReader 完全一致！）
            file_arr = np.fromfile(file_path, dtype=np.float32)

            if summed_data is None:
                summed_data = file_arr.copy()
            else:
                summed_data += file_arr  # 矩阵相加，模拟声音在水下的物理叠加

        # 将叠加后的浮点数数组转回二进制字节流
        frame_bytes = summed_data.tobytes()

        # 将这帧混合后的巨型数据切片发送
        chunks = [frame_bytes[i:i + CHUNK_SIZE] for i in range(0, len(frame_bytes), CHUNK_SIZE)]
        total_chunks = len(chunks)

        for chunk_idx, chunk in enumerate(chunks):
            header = struct.pack('<IIIII', MAGIC_WORD, frame_idx, chunk_idx, total_chunks, len(chunk))
            sock.sendto(header + chunk, (UDP_IP, UDP_PORT))
            time.sleep(0.002)

        print(f"  └─ 📦 第 {frame_idx} 帧混合发送完毕 (共 {total_chunks} 个分片)")
        time.sleep(1)

    print("\n🎉 所有多目标混合帧发送完毕！")
except KeyboardInterrupt:
    print("\n🛑 已终止。")
finally:
    sock.close()