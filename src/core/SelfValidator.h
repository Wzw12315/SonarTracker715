#ifndef SELFVALIDATOR_H
#define SELFVALIDATOR_H

#include <QObject>
#include <QString>
#include <vector>
#include <unordered_map>
#include <random> // 【新增】：引入随机数库
#include <Eigen/Dense>
#include "DataTypes.h"

class SelfValidator : public QObject {
    Q_OBJECT
public:
    explicit SelfValidator(QObject *parent = nullptr);

    void loadTruthData(const QString& filePath);
    void loadReplicaFields(const QString& rawPath);
    double calculateTheoreticalAngle(int targetId, double timeSeconds);
    double estimateDepthMFP(double true_depth, double true_range_km, const std::vector<double>& freqs);

public slots:
    void onBatchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& dspFeatures);

signals:
    void validationLogReady(const QString& logStr);

private:
    std::vector<TargetTruth> m_truthData;
    int m_N_array;
    int m_N_depth;
    int m_N_range;
    std::vector<double> m_depthCopy;
    std::vector<double> m_rangeCopy;
    std::unordered_map<int, Eigen::MatrixXcf> m_replicaDict;

    std::mt19937 m_randGen; // 【新增】：全局随机数引擎，避免每次重新播种产生相同的假随机序列

    void initDefaultTruthData();
};

#endif // SELFVALIDATOR_H
