#include "UdpReceiver.h"
#include <QDebug>

UdpReceiver::UdpReceiver(quint16 port, DataBuffer* buffer, const QString& savePath, QObject* parent)
    : QThread(parent), m_port(port), m_buffer(buffer), m_savePath(savePath), m_isRunning(false) {}

void UdpReceiver::stop() {
    m_isRunning = false;
}

void UdpReceiver::run() {
    m_isRunning = true;
    QUdpSocket socket;

    // 绑定端口，并扩大接收缓冲区 (32MB) 以防高并发丢包
    socket.bind(QHostAddress::Any, m_port, QUdpSocket::ShareAddress);
    socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1024 * 1024 * 32);

    // ================== 【修改这里的逻辑】 ==================
    QFile outFile(m_savePath);
    bool isSaving = false;

    // 只有当路径不为空时，才尝试打开文件
    if (!m_savePath.isEmpty()) {
        if (!outFile.open(QIODevice::WriteOnly)) {
            qDebug() << "[Receiver] ⚠️ 无法创建输出文件:" << m_savePath << "，将仅处理数据不保存文件。";
        } else {
            isSaving = true;
        }
    } else {
        qDebug() << "[Receiver] ℹ️ 未指定保存路径，仅进行实时处理，不保存二进制文件。";
    }
    // =======================================================

    qDebug() << "[Receiver] 🎧 正在监听端口" << m_port << "...";

    uint32_t expectedSeq = 0;
    int dropCount = 0;

    // 在 processPackets 外面声明
        QByteArray currentFrameData;
        uint32_t currentFrameIdx = 0;
        uint32_t receivedChunksCount = 0;
        int expectedTotalSize = 0;

        auto processPackets = [&]() {
            while (socket.hasPendingDatagrams()) {
                QByteArray datagram;
                datagram.resize(socket.pendingDatagramSize());
                socket.readDatagram(datagram.data(), datagram.size());

                if (datagram.size() < sizeof(PacketHeader)) continue;

                PacketHeader header;
                memcpy(&header, datagram.constData(), sizeof(PacketHeader));

                // 1. 魔法校验码拦截
                if (header.magic != 0xAA55AA55) continue;

                // 2. 跨帧重置：收到新的一帧时，预分配巨大的“纯净画布”
                if (header.frameIdx != currentFrameIdx) {
                    currentFrameIdx = header.frameIdx;
                    // 预分配最大可能内存：总分片数 * 51200
                    currentFrameData.resize(header.totalChunks * 51200);
                    currentFrameData.fill('\0');
                    receivedChunksCount = 0;
                    expectedTotalSize = 0;
                }

                // 3. 【核心抗乱序】：计算当前分片在画布上的绝对偏移量
                // ⚠️ 这里的 51200 必须与 Python 端的 CHUNK_SIZE 完全一致！
                int offset = header.chunkIdx * 51200;

                if (offset + header.payloadSize <= currentFrameData.size()) {
                    // 将网络载荷精准涂抹到画布的指定位置
                    memcpy(currentFrameData.data() + offset,
                           datagram.constData() + sizeof(PacketHeader),
                           header.payloadSize);
                    receivedChunksCount++;
                }

                // 记录最后一片的真实边界，用于裁剪 padding
                if (header.chunkIdx == header.totalChunks - 1) {
                    expectedTotalSize = offset + header.payloadSize;
                }

                // 4. 判断是否拼图完成？
                if (receivedChunksCount == header.totalChunks) {
                    // 裁剪掉多余的预分配内存
                    if (expectedTotalSize > 0) {
                        currentFrameData.resize(expectedTotalSize);
                    }

                    if (m_buffer) {
                        m_buffer->pushData(currentFrameData); // 推送给 DspWorker!
                    }
                    if (isSaving) outFile.write(currentFrameData);

                    qDebug() << "[Receiver] ✅ 第" << header.frameIdx << "帧 空间快拍无损组装完毕！";

                    // 清理战场，迎接下一帧
                    currentFrameData.clear();
                    receivedChunksCount = 0;
                    expectedTotalSize = 0;
                }
            }
        };

    // 主循环
    while (m_isRunning) {
        if (socket.waitForReadyRead(100)) {
            processPackets();
        }
    }

    // 收到停止指令后，强行榨干一次网卡缓冲区残留的最后几个包
    processPackets();

    if (isSaving) {
        outFile.close();
    }
    qDebug() << "[Receiver] 🛑 监听结束。共接收包:" << expectedSeq << ", 累计丢包数:" << dropCount;
}
