#ifndef SELFVALIDATOR_H
#define SELFVALIDATOR_H

#include <QObject>
#include <QString>
#include <vector>
#include <unordered_map>
#include <random>
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
    // 【新增】：当本批次正确率计算完毕时，向外发射
    void batchAccuracyComputed(int batchIndex, double accuracy);

private:
    std::vector<TargetTruth> m_truthData;
    int m_N_array;
    int m_N_depth;
    int m_N_range;
    std::vector<double> m_depthCopy;
    std::vector<double> m_rangeCopy;
    std::unordered_map<int, Eigen::MatrixXcf> m_replicaDict;

    std::mt19937 m_randGen;

    void initDefaultTruthData();
};

#endif // SELFVALIDATOR_H
