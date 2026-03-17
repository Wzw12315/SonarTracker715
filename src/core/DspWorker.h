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
    void setConfig(const DspConfig& config);
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

    void batchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& features);

protected:
    void run() override;

private:
    QString m_directory;
    DspConfig m_config;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isPaused;

    // 【修改】：对齐你的日志，设定每 10 帧触发一次批处理与界面重绘
    int m_batchSize = 10;
};
