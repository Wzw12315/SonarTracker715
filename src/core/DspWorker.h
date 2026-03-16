#pragma once
#include <QThread>
#include <QString>
#include <atomic>
#include "DataTypes.h"
#include "detect_line_spectrum_from_lofar_change.h"

class DspWorker : public QThread {
    Q_OBJECT
public:
    explicit DspWorker(QObject *parent = nullptr);
    ~DspWorker();

    void setDirectory(const QString& dirPath);
    void setConfig(const DspConfig& config); // 【新增】：设置动态配置
    void stop();
    void pause();
    void resume();
    bool isPaused() const { return m_isPaused; }

signals:
    void frameProcessed(const FrameResult& result);
    void logReady(const QString& log);
    void reportReady(const QString& report);
    void offlineResultsReady(const QList<OfflineTargetResult>& results);
    void processingFinished();

protected:
    void run() override;

private:
    QString m_directory;
    DspConfig m_config; // 【新增】：保存当前配置
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isPaused;
};
