#ifndef SELFVALIDATOR_H
#define SELFVALIDATOR_H

#include <QObject>
#include <QString>
#include <vector>
#include <map>
#include <random>
#include <Eigen/Dense>   // 必须引入 Eigen 否则 MatrixXcf 报错
#include "DataTypes.h"

class SelfValidator : public QObject {
    Q_OBJECT
public:
    explicit SelfValidator(QObject *parent = nullptr);

    void loadTruthData(const QString& filePath);
    void setTruthData(const std::vector<TargetTruth>& manualData);
    double calculateTheoreticalAngle(int targetId, double timeSeconds);
    const std::vector<TargetTruth>& getTruthData() const { return m_truthData; }

    // 【新增】：接收来自界面的 MFP 开关状态
    void setDepthResolveEnabled(bool enabled) { m_enableDepthResolve = enabled; }

public slots:
    void onBatchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& dspFeatures);

signals:
    void validationLogReady(const QString& logStr);
    void batchAccuracyComputed(int batchIndex, double accuracy);

private:
    std::vector<TargetTruth> m_truthData;
    void initDefaultTruthData();
    double evaluateMatch(const BatchTargetFeature& feature, const TargetTruth& truth);

    // =======================================================
    // 【核心修复】：声明 MFP 深度分辨相关的成员变量和底层函数
    // =======================================================
    bool m_enableDepthResolve = false;

    void loadReplicaFields(const QString& rawPath);
    double estimateDepthMFP(double true_depth, double true_range_km, const std::vector<double>& freqs);

    int m_N_array;
    int m_N_depth;
    int m_N_range;
    std::vector<double> m_depthCopy;
    std::vector<double> m_rangeCopy;
    std::map<double, Eigen::MatrixXcf> m_replicaDict;
    std::mt19937 m_randGen;
};

#endif // SELFVALIDATOR_H
