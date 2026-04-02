#pragma once
#include <QThread>
#include <QUdpSocket>
#include <QFile>
#include "DataBuffer.h"

class UdpReceiver : public QThread {
    Q_OBJECT
public:
    UdpReceiver(quint16 port, DataBuffer* buffer, const QString& savePath, QObject* parent = nullptr);
    void stop();

protected:
    void run() override;

private:
    quint16 m_port;
    DataBuffer* m_buffer;
    QString m_savePath;
    bool m_isRunning;
};