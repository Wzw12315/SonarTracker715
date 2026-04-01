#pragma once
#include <QThread>
#include <QString>
#include <atomic>
#include "DataTypes.h"
#include "detect_line_spectrum_from_lofar_change.h"
#include "MfpProcessor.h"
#include <QMutex>
#include <QMap>
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
    QMutex m_removeMutex;
    QList<int> m_targetsToRemove;
    QStringList m_selectedFiles;

    MfpProcessor m_mfpProcessor; // 【新增】MFP匹配场处理引擎
    QMap<int, int> m_mfpCorrectCounts;
        QMap<int, int> m_mfpTotalCounts;
};
