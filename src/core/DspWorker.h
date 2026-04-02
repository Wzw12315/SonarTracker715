#pragma once
#include <QThread>
#include <QString>
#include <atomic>
#include "DataTypes.h"
#include "detect_line_spectrum_from_lofar_change.h"
#include <QMutex>
#include <QMap>
#include "DataBuffer.h"



// 增加一个枚举，区分是跑离线文件还是跑实时UDP
enum class WorkMode { MODE_FILE, MODE_UDP };

class DspWorker : public QThread {
    Q_OBJECT
public:
    explicit DspWorker(QObject *parent = nullptr);
    ~DspWorker();

    void setTargetFiles(const QStringList& files);
    void setConfig(const DspConfig& config);
    void setGroundTruths(const std::vector<TargetTruth>& truths) { m_groundTruths = truths; }
    void stop();
    void pause();
    void resume();
    bool isPaused() const { return m_isPaused; }
    void requestRemoveTarget(int targetId);
    // 【新增】：设置工作模式和绑定缓冲池的方法
        void setWorkMode(WorkMode mode) { m_mode = mode; }
        void setDataBuffer(DataBuffer* buffer) { m_dataBuffer = buffer; }
signals:
    void frameProcessed(const FrameResult& result);
    void logReady(const QString& log);
    void reportReady(const QString& report);
    void offlineResultsReady(const QList<OfflineTargetResult>& results);
    void processingFinished();
    void batchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& features);
    void evaluationResultReady(const SystemEvaluationResult& result);

protected:
    void run() override;

private:
    QString m_directory;
    DspConfig m_config;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isPaused;
    std::vector<TargetTruth> m_groundTruths;
    QStringList m_selectedFiles;
    QList<int> m_targetsToRemove;
    QMutex m_removeMutex;

    WorkMode m_mode = WorkMode::MODE_FILE; // 默认依然是离线文件模式
        DataBuffer* m_dataBuffer = nullptr;
};
