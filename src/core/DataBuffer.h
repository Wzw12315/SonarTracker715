#pragma once
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QByteArray>

// 定义 UDP 传输的自定义协议头 (1字节对齐防止跨平台错位)
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t seqNum;        // 数据包序号，用于防丢包和乱序
    uint32_t payloadSize;   // 有效载荷大小
};
#pragma pack(pop)

class DataBuffer {
public:
    explicit DataBuffer(int maxPackets = 10000) : m_maxPackets(maxPackets) {}

    void pushData(const QByteArray& data) {
        QMutexLocker locker(&m_mutex);
        if (m_queue.size() >= m_maxPackets) {
            m_queue.dequeue(); // 背压机制：撑爆时丢弃老数据，保证实时性
        }
        m_queue.enqueue(data);
        m_cond.wakeOne();
    }

    bool popData(QByteArray& outData, unsigned long timeoutMs = 100) {
        QMutexLocker locker(&m_mutex);
        if (m_queue.isEmpty()) {
            if (!m_cond.wait(&m_mutex, timeoutMs)) {
                return false;
            }
        }
        if (!m_queue.isEmpty()) {
            outData = m_queue.dequeue();
            return true;
        }
        return false;
    }
    // Add this new clear method
        void clear() {
            QMutexLocker locker(&m_mutex);
            m_queue.clear();
        }
private:
    QQueue<QByteArray> m_queue;
    QMutex m_mutex;
    QWaitCondition m_cond;
    int m_maxPackets;
};
