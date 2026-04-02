import socket
import struct
import time
import numpy as np

# 目标地址和端口（与 UdpReceiver 中默认的 8888 对应）
UDP_IP = "127.0.0.1"
UDP_PORT = 8888

# 【注意】为了不超过 UDP 的 64KB 限制，测试时请将参数改小
M = 16  # 阵元数
NFFT_R = 1000  # 短窗快拍数

# 16位 short 占 2 字节
payload_size = M * NFFT_R * 2  # 16 * 1000 * 2 = 32000 Bytes (符合 UDP 限制)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
seq_num = 0

print(f"🚀 模拟声呐启动！准备向 {UDP_IP}:{UDP_PORT} 发送数据...")

try:
    while True:
        # 1. 构造自定义协议头 (小端序: seq_num, payloadSize)
        header = struct.pack('<II', seq_num, payload_size)

        # 2. 生成模拟载荷 (带有一点正弦波特征的随机噪声)
        # 这里用 numpy 生成随机 short 数据
        data = np.random.randint(-10000, 10000, size=M * NFFT_R, dtype=np.int16)
        payload = data.tobytes()

        # 3. 组装数据包并发送
        packet = header + payload
        sock.sendto(packet, (UDP_IP, UDP_PORT))

        print(f"📦 已发送数据包: 序号={seq_num}, 载荷大小={payload_size} Bytes")
        seq_num += 1

        # 模拟 1 秒钟发一帧数据
        time.sleep(1)
except KeyboardInterrupt:
    print("\n🛑 模拟声呐停止发送。")
finally:
    sock.close()