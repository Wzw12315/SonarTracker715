#pragma once
#include <QThread>
#include <QUdpSocket>
#include <QFile>
#include <cstdint> // 【新增】引入 uint32_t 的支持
#include "DataBuffer.h"

// ==========================================
// 【新增：自定义 UDP 应用层通信协议包头】
// ==========================================
#pragma pack(push, 1) // 强制 1 字节对齐，防止跨语言解析时发生内存错位
struct PacketHeader {
    uint32_t magic;       // 魔法校验码 (固定为 0xAA55AA55)
    uint32_t frameIdx;    // 帧序号 (第几帧，比如 1, 2, 3...)
    uint32_t chunkIdx;    // 当前分片号 (比如 0, 1, 2...)
    uint32_t totalChunks; // 本帧总分片数
    uint32_t payloadSize; // 本分片实际载荷字节数
    // 【新增】：来自远端发送的导航遥测数据 (3个 float，占 12 字节)
        float shipLon;        // 本舰经度
        float shipLat;        // 本舰纬度
        float shipHeading;    // 本舰艏向角
};
#pragma pack(pop)
// ==========================================
class UdpReceiver : public QThread {
    Q_OBJECT
public:
    // 【修改】：新增 const QString& address 参数
    UdpReceiver(const QString& address, quint16 port, DataBuffer* buffer, const QString& savePath, QObject* parent = nullptr);
    void stop();

protected:
    void run() override;
signals:
    // 【新增】：向主界面发送格式化后的导航报文信号
    void navInfoReceived(QString lonStr, QString latStr, QString headingStr);
private:
    QString m_address; // 【新增】绑定的IP地址
    quint16 m_port;
    DataBuffer* m_buffer;
    QString m_savePath;
    bool m_isRunning;
};
