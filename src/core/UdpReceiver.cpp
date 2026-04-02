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

    QFile outFile(m_savePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        qDebug() << "[Receiver] ❌ 无法创建输出文件:" << m_savePath;
        return;
    }

    qDebug() << "[Receiver] 🎧 正在监听端口" << m_port << "...";

    uint32_t expectedSeq = 0;
        int dropCount = 0;

        // 提取为一个 Lambda，方便复用
        auto processPackets = [&]() {
            while (socket.hasPendingDatagrams()) {
                QByteArray datagram;
                datagram.resize(socket.pendingDatagramSize());
                socket.readDatagram(datagram.data(), datagram.size());

                if (datagram.size() < sizeof(PacketHeader)) continue;

                PacketHeader header;
                memcpy(&header, datagram.constData(), sizeof(PacketHeader));

                if (header.seqNum != expectedSeq) {
                    dropCount += (header.seqNum - expectedSeq);
                    expectedSeq = header.seqNum + 1; // 修正预期包号
                } else {
                    expectedSeq++;
                }

                QByteArray payload = datagram.mid(sizeof(PacketHeader), header.payloadSize);
                if (m_buffer) m_buffer->pushData(payload);
                outFile.write(payload);
            }
        };

        // 主循环
        while (m_isRunning) {
            if (socket.waitForReadyRead(100)) {
                processPackets();
            }
        }

        // 【关键修复】：收到停止指令后，再强行榨干一次网卡缓冲区残留的最后几个包！
        processPackets();

        outFile.close();
        qDebug() << "[Receiver] 🛑 监听结束。共接收包:" << expectedSeq << ", 累计丢包数:" << dropCount;
    }
