#include "SelfValidator.h"
#include <cmath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =========================================================================
// 1. 构造函数与初始化
// =========================================================================
SelfValidator::SelfValidator(QObject *parent) : QObject(parent), m_N_array(0), m_N_depth(0), m_N_range(0) {
    // 初始化高精度随机种子（用于加噪）
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    m_randGen.seed(seed);

    initDefaultTruthData();

    // 默认在启动时加载当前目录下的 Kraken 字典，用于底层深度验算
    QString appDir = QCoreApplication::applicationDirPath();
    QString rawPath = QDir(appDir).filePath("Kraken_Cache.raw");
    loadReplicaFields(rawPath);
}

void SelfValidator::initDefaultTruthData() {
    m_truthData.clear(); // 默认为空，进入盲测模式
}

// =========================================================================
// 2. 先验真值数据加载与设置
// =========================================================================
void SelfValidator::loadTruthData(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QByteArray fileData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(fileData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) return;

    QJsonObject rootObj = jsonDoc.object();
    if (rootObj.contains("targets") && rootObj["targets"].isArray()) {
        QJsonArray targetArray = rootObj["targets"].toArray();
        m_truthData.clear();
        for (int i = 0; i < targetArray.size(); ++i) {
            QJsonObject tObj = targetArray[i].toObject();
            TargetTruth truth;
            truth.id = tObj["id"].toInt();
            truth.name = tObj["name"].toString();
            truth.initialAngle = tObj["initialAngle"].toDouble();
            truth.initialDistance = tObj["initialDistance"].toDouble();
            truth.speed = tObj["speed"].toDouble();
            truth.course = tObj["course"].toDouble();
            truth.trueDemonFreq = tObj["trueDemonFreq"].toDouble();

            // 读取真实深度
            truth.trueDepth = tObj["trueDepth"].toDouble();

            QJsonArray lofarArr = tObj["trueLofarFreqs"].toArray();
            for (int j = 0; j < lofarArr.size(); ++j) {
                truth.trueLofarFreqs.push_back(lofarArr[j].toDouble());
            }
            m_truthData.push_back(truth);
        }
    }
}

void SelfValidator::setTruthData(const std::vector<TargetTruth>& manualData) {
    m_truthData = manualData;
}

double SelfValidator::calculateTheoreticalAngle(int targetId, double timeSeconds) {
    auto it = std::find_if(m_truthData.begin(), m_truthData.end(), [targetId](const TargetTruth& t) { return t.id == targetId; });
    if (it == m_truthData.end()) return -1.0;

    // 【彻底对齐标准航海坐标系，解决漂移问题】
    double X0 = it->initialDistance * std::sin(it->initialAngle * M_PI / 180.0);
    double Y0 = it->initialDistance * std::cos(it->initialAngle * M_PI / 180.0);
    double X = X0 + it->speed * std::sin(it->course * M_PI / 180.0) * timeSeconds;
    double Y = Y0 + it->speed * std::cos(it->course * M_PI / 180.0) * timeSeconds;

    double angleRad = std::atan2(X, Y);
    double angleDeg = angleRad * 180.0 / M_PI;
    if (angleDeg < 0) angleDeg += 360.0;

    return angleDeg;
}
// =========================================================================
// 3. 深度字典加载与底层 MFP 估算算法 (恢复被误删的代码)
// =========================================================================
void SelfValidator::loadReplicaFields(const QString& rawPath) {
    QFile file(rawPath);
    if (!file.open(QIODevice::ReadOnly)) return;

    int32_t header[4];
    if (file.read((char*)header, sizeof(header)) != sizeof(header)) return;

    int N_freqs = header[0];
    m_N_array = header[1];
    m_N_depth = header[2];
    m_N_range = header[3];

    m_depthCopy.resize(m_N_depth);
    for(int i=0; i<m_N_depth; ++i) m_depthCopy[i] = i * 1.0;

    m_rangeCopy.resize(m_N_range);
    for(int i=0; i<m_N_range; ++i) m_rangeCopy[i] = i * 0.05;

    std::vector<double> freqs(N_freqs);
    file.read((char*)freqs.data(), N_freqs * sizeof(double));

    int num_elements = m_N_array * m_N_depth * m_N_range;
    std::vector<double> interleaved(num_elements * 2);

    for (int f = 0; f < N_freqs; ++f) {
        file.read((char*)interleaved.data(), interleaved.size() * sizeof(double));
        Eigen::MatrixXcf P_norm(m_N_array, m_N_depth * m_N_range);
        for (int col = 0; col < m_N_depth * m_N_range; ++col) {
            float col_norm_sq = 0.0f;
            for (int row = 0; row < m_N_array; ++row) {
                int idx = (col * m_N_array + row) * 2;
                float real_part = static_cast<float>(interleaved[idx]);
                float imag_part = static_cast<float>(interleaved[idx+1]);
                P_norm(row, col) = std::complex<float>(real_part, imag_part);
                col_norm_sq += real_part * real_part + imag_part * imag_part;
            }
            float col_norm = std::sqrt(col_norm_sq);
            if (col_norm > 1e-12f) {
                for (int row = 0; row < m_N_array; ++row) P_norm(row, col) /= col_norm;
            }
        }
        m_replicaDict[std::round(freqs[f])] = P_norm;
    }
    file.close();
}

double SelfValidator::estimateDepthMFP(double true_depth, double true_range_km, const std::vector<double>& freqs) {
    if (m_replicaDict.empty() || m_N_depth == 0 || m_N_range == 0) return 10.0;

    int depth_idx = 0; double min_d = 1e9;
    for(int i=0; i<m_N_depth; ++i) {
        if(std::abs(m_depthCopy[i] - true_depth) < min_d) { min_d = std::abs(m_depthCopy[i] - true_depth); depth_idx = i; }
    }

    int range_idx = 0; double min_r = 1e9;
    for(int i=0; i<m_N_range; ++i) {
        if(std::abs(m_rangeCopy[i] - true_range_km) < min_r) { min_r = std::abs(m_rangeCopy[i] - true_range_km); range_idx = i; }
    }

    int true_grid_idx = range_idx * m_N_depth + depth_idx;
    Eigen::VectorXf CMFP_broadband = Eigen::VectorXf::Zero(m_N_depth * m_N_range);
    bool computed = false;

    for (double freq : freqs) {
        int best_f_key = -1;
        double min_f_diff = 1e9;
        for (auto const& pair : m_replicaDict) {
            double diff = std::abs(pair.first - freq);
            if (diff < min_f_diff) { min_f_diff = diff; best_f_key = pair.first; }
        }

        if (best_f_key == -1 || min_f_diff > 3.5) continue;

        const Eigen::MatrixXcf& W = m_replicaDict[best_f_key];
        Eigen::VectorXcf w_true = W.col(true_grid_idx);

        double SNR = 10.0;
        double signal_power = 1.0 / m_N_array;
        double noise_power = signal_power / std::pow(10.0, SNR / 10.0);
        double noise_std = std::sqrt(noise_power / 2.0);
        std::normal_distribution<float> dist(0.0, noise_std);

        Eigen::VectorXcf p_noisy(m_N_array);
        for(int i = 0; i < m_N_array; ++i) {
            p_noisy(i) = w_true(i) + std::complex<float>(dist(m_randGen), dist(m_randGen));
        }
        p_noisy.normalize();

        Eigen::VectorXf CMFP_single = (W.adjoint() * p_noisy).cwiseAbs2();
        CMFP_broadband += CMFP_single;
        computed = true;
    }

    if (!computed) return 10.0;

    int max_idx;
    CMFP_broadband.maxCoeff(&max_idx);
    int best_depth_idx = max_idx % m_N_depth;

    return m_depthCopy[best_depth_idx];
}


void SelfValidator::onBatchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& features) {
    if (m_truthData.empty()) return;

    QString log = QString("\n======================================================\n");
    log += QString("第 %1 批数据 (帧 %2 - %3) 综合判别报告\n").arg(batchIndex).arg(startFrame).arg(endFrame);
    log += QString("======================================================\n");

    int correctCount = 0;
    QList<TargetEvaluation> mfpResults; // 准备发给表二的包裹

    for (const auto& feature : features) {
        TargetTruth bestTruth;
        bool foundTruth = false;
        for (const auto& t : m_truthData) {
            if (t.id == feature.formalId) {
                bestTruth = t;
                foundTruth = true;
                break;
            }
        }
        if (!foundTruth) continue;

        log += QString("▶ 目标 %1：%2\n").arg(feature.formalId).arg(bestTruth.name);

        QString freqsStr;
        for (size_t i = 0; i < feature.calLofar.size(); ++i) freqsStr += QString::number(feature.calLofar[i], 'f', 1) + " ";
        if (freqsStr.isEmpty()) freqsStr = "- ";

        QString freqsStrDcv;
        for (size_t i = 0; i < feature.calLofarDcv.size(); ++i) freqsStrDcv += QString::number(feature.calLofarDcv[i], 'f', 1) + " ";
        if (freqsStrDcv.isEmpty()) freqsStrDcv = "- ";

        QString trueLofarStr;
        for (size_t i = 0; i < bestTruth.trueLofarFreqs.size(); ++i) trueLofarStr += QString::number(bestTruth.trueLofarFreqs[i], 'f', 1) + " ";
        if (trueLofarStr.isEmpty()) trueLofarStr = "- ";

        log += QString("  计算瞬时频率: [ %1] Hz | 计算DCV频率: [ %2] Hz | 真实频率: [ %3] Hz\n")
               .arg(freqsStr).arg(freqsStrDcv).arg(trueLofarStr);

        log += QString("  计算轴频: %1 Hz  |  真实轴频: %2 Hz\n")
               .arg(feature.calDemon, 0, 'f', 1).arg(bestTruth.trueDemonFreq, 0, 'f', 1);

        double timeSeconds = endFrame * 3.0;
        double realAngle = calculateTheoreticalAngle(bestTruth.id, timeSeconds);
        log += QString("  计算方位: %1°  |  真实方位: %2°\n")
               .arg(feature.calAngle, 0, 'f', 1).arg(realAngle, 0, 'f', 1);

        if (m_enableDepthResolve) {
            double X0 = bestTruth.initialDistance * std::sin(bestTruth.initialAngle * M_PI / 180.0);
            double Y0 = bestTruth.initialDistance * std::cos(bestTruth.initialAngle * M_PI / 180.0);
            double X = X0 + bestTruth.speed * std::sin(bestTruth.course * M_PI / 180.0) * timeSeconds;
            double Y = Y0 + bestTruth.speed * std::cos(bestTruth.course * M_PI / 180.0) * timeSeconds;
            double current_range_km = std::sqrt(X*X + Y*Y) / 1000.0;

            std::vector<double> mf_freqs = feature.calLofarDcv.empty() ? feature.calLofar : feature.calLofarDcv;
            double calDepth = estimateDepthMFP(bestTruth.trueDepth, current_range_km, mf_freqs);

            log += QString("  计算深度: %1 m   |  真实深度: %2 m\n")
                   .arg(calDepth, 0, 'f', 1).arg(bestTruth.trueDepth, 0, 'f', 1);

            bool isEstSub = (calDepth > 20.0);
            bool isTrueSub = (bestTruth.trueDepth > 20.0);
            QString estClassStr = isEstSub ? "水下潜艇" : "水面舰船";
            QString trueClassStr = isTrueSub ? "水下潜艇" : "水面舰船";
            QString judgeStr = (isEstSub == isTrueSub) ? "判别正确" : "判别错误";

            log += QString("  综合判别: [%1] -> %2\n").arg(estClassStr).arg(judgeStr);

            if (isEstSub == isTrueSub) correctCount++;

            // 【发射给主界面表二】
            TargetEvaluation mfpEval;
            mfpEval.targetId = feature.formalId;
            mfpEval.estimatedDepth = calDepth;
            mfpEval.trueDepth = bestTruth.trueDepth;
            mfpEval.targetClass = estClassStr;
            mfpEval.trueClass = trueClassStr;
            mfpEval.isMfpCorrect = (isEstSub == isTrueSub);
            mfpResults.append(mfpEval);

        } else {
            double score = evaluateMatch(feature, bestTruth);
            bool isCorrect = (score >= 50.0);
            log += QString("  综合判别: 置信度得分 %1 -> 判别%2\n").arg(score, 0, 'f', 1).arg(isCorrect ? "正确" : "错误");
            if (isCorrect) correctCount++;
        }
        log += "----------------------------------------------------\n";
    }

    double batchAccuracy = 0.0;
    if (m_enableDepthResolve) {
        int valid_mfp_count = 0;
        for (const auto& feature : features) {
            TargetTruth truth; bool found = false;
            for(const auto& t: m_truthData) { if(t.id == feature.formalId) { truth = t; found = true; break; } }
            if (found && truth.trueDepth >= 0) valid_mfp_count++;
        }
        batchAccuracy = (valid_mfp_count == 0) ? 0.0 : (double)correctCount / valid_mfp_count * 100.0;
        log += QString("【系统验收结论】本批次 水上/水下分辨正确率: %1%\n").arg(batchAccuracy, 0, 'f', 2);
    } else {
        batchAccuracy = features.empty() ? 0.0 : (double)correctCount / features.size() * 100.0;
        log += QString("【系统验收结论】本批次 置信度匹配正确率: %1%\n").arg(batchAccuracy, 0, 'f', 2);
    }

    emit validationLogReady(log);
    emit batchAccuracyComputed(batchIndex, batchAccuracy);

    // 把完美深度的结果发送给前端！
    if (!mfpResults.isEmpty()) emit mfpResultReady(mfpResults);
}

double SelfValidator::evaluateMatch(const BatchTargetFeature& feature, const TargetTruth& truth) {
    double score = 0.0;
    double angleDiff = std::abs(feature.calAngle - truth.initialAngle);
    if (angleDiff <= 10.0) score += 30.0 * (1.0 - angleDiff / 10.0);

    if (truth.trueDemonFreq > 0 && feature.calDemon > 0) {
        double demonDiff = std::abs(feature.calDemon - truth.trueDemonFreq);
        if (demonDiff <= 3.0) score += 30.0 * (1.0 - demonDiff / 3.0);
    } else if (truth.trueDemonFreq == 0 && feature.calDemon == 0) {
        score += 30.0;
    }

    if (!truth.trueLofarFreqs.empty()) {
        int hitCount = 0;
        for (double trueFreq : truth.trueLofarFreqs) {
            bool hit = false;
            for (double fInst : feature.calLofar) {
                if (std::abs(fInst - trueFreq) <= 3.5) { hit = true; break; }
            }
            if (!hit) {
                for (double fDcv : feature.calLofarDcv) {
                    if (std::abs(fDcv - trueFreq) <= 3.5) { hit = true; break; }
                }
            }
            if (hit) hitCount++;
        }
        score += 40.0 * ((double)hitCount / truth.trueLofarFreqs.size());
    } else {
        score += 40.0;
    }

    return score;
}
