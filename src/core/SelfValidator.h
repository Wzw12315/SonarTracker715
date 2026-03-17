#ifndef SELFVALIDATOR_H
#define SELFVALIDATOR_H

#include <QObject>
#include <QString>
#include <vector>
#include <unordered_map>
#include <Eigen/Dense>
#include "DataTypes.h"

class SelfValidator : public QObject {
    Q_OBJECT
public:
    explicit SelfValidator(QObject *parent = nullptr);

    // 从 JSON 加载真值
    void loadTruthData(const QString& filePath);

    // 从本地直接加载 Kraken_Cache.raw 二进制字典
    void loadReplicaFields(const QString& rawPath);

    // 运动学计算：推算真实方位
    double calculateTheoreticalAngle(int targetId, double timeSeconds);

    // MFP匹配场计算：模拟接收信号并评估计算深度
    double estimateDepthMFP(double true_depth, double true_range_km, const std::vector<double>& freqs);

public slots:
    void onBatchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& dspFeatures);

signals:
    void validationLogReady(const QString& logStr);

private:
    std::vector<TargetTruth> m_truthData;

    // 场库维度信息
    int m_N_array;
    int m_N_depth;
    int m_N_range;
    std::vector<double> m_depthCopy;
    std::vector<double> m_rangeCopy;

    // 拷贝场字典：Key 为频率，Value 为 [阵元数 x (深度点*距离点)] 的归一化大型复数矩阵
    std::unordered_map<int, Eigen::MatrixXcf> m_replicaDict;

    void initDefaultTruthData();
};

#endif // SELFVALIDATOR_H
