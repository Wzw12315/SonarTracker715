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

    // 【新增】：每处理完一批数据 (默认40帧) 发射此信号，唤醒自校验模块
        void batchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& features);

protected:
    void run() override;

private:
    QString m_directory;
    DspConfig m_config; // 【新增】：保存当前配置
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isPaused;

    // 【新增】：定义批处理大小
        int m_batchSize = 55;
};
