#include "SelfValidator.h"
#include <cmath>
#include <QTextStream>
#include <algorithm>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <random>
#include <QCoreApplication>
#include <QDir>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SelfValidator::SelfValidator(QObject *parent) : QObject(parent), m_N_array(0), m_N_depth(0), m_N_range(0) {
    // 【关键修复 1】：使用系统高精度时钟作为种子，彻底解决 MinGW 下伪随机序列锁死的问题
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    m_randGen.seed(seed);

    initDefaultTruthData();

    QString appDir = QCoreApplication::applicationDirPath();
    QString rawPath = QDir(appDir).filePath("Kraken_Cache.raw");
    qDebug() << "正在尝试加载 Kraken RAW 文件，路径:" << rawPath;
    loadReplicaFields(rawPath);
}

void SelfValidator::initDefaultTruthData() {
    m_truthData = {
        {1, "Ship A (潜艇1)", 60.0, 20000.0, 4.0, 45.0, 50.0, {125.0}, 2.1},
        {2, "Ship B (潜艇2)", 80.0, 20000.0, 5.0, 80.0, 60.0, {112.0}, 2.8},
        {3, "Ship C (水面1)", 88.2, 20000.0, 8.0, 85.0, 10.0, {120.0, 180.0, 240.0}, 4.2},
        {4, "Ship D (水面2)", 90.0, 20000.0, 7.0, 95.0, 8.0, {135.0, 180.0, 225.0, 270.0}, 5.5},
        {5, "Ship E (水面3)", 120.0, 20000.0, 9.0, 120.0, 12.0, {200.0, 250.0}, 6.8},
        {6, "Ship F (水面4)", 140.0, 20000.0, 10.0, 160.0, 15.0, {140.0, 280.0}, 8.1}
    };
}

void SelfValidator::loadTruthData(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "无法打开先验真值 JSON 文件:" << filePath;
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(fileData, &parseError);
    if (parseError.error != QJsonParseError::NoError) return;
    if (!jsonDoc.isObject()) return;

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
            truth.trueDepth = tObj["trueDepth"].toDouble();
            truth.trueDemonFreq = tObj["trueDemonFreq"].toDouble();

            QJsonArray lofarArr = tObj["trueLofarFreqs"].toArray();
            for (int j = 0; j < lofarArr.size(); ++j) {
                truth.trueLofarFreqs.push_back(lofarArr[j].toDouble());
            }
            m_truthData.push_back(truth);
        }
    }
}

void SelfValidator::loadReplicaFields(const QString& rawPath) {
    QFile file(rawPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "警告: 无法加载 Kraken 拷贝场 RAW 二进制文件:" << rawPath << " 深度结算将挂起。";
        return;
    }

    // 1. 读取头文件维度
    int32_t header[4];
    if (file.read((char*)header, sizeof(header)) != sizeof(header)) return;

    int N_freqs = header[0];
    m_N_array = header[1];
    m_N_depth = header[2];
    m_N_range = header[3];

    // 初始化网格参数 (对应 MATLAB 中的 0:1:200 和 0:0.05:30)
    m_depthCopy.resize(m_N_depth);
    for(int i=0; i<m_N_depth; ++i) m_depthCopy[i] = i * 1.0;

    m_rangeCopy.resize(m_N_range);
    for(int i=0; i<m_N_range; ++i) m_rangeCopy[i] = i * 0.05;

    // 2. 读取频率列表
    std::vector<double> freqs(N_freqs);
    file.read((char*)freqs.data(), N_freqs * sizeof(double));

    // 3. 读取交叉写入的双精度实数/虚数数据并建立 Eigen 字典
    int num_elements = m_N_array * m_N_depth * m_N_range;
    std::vector<double> interleaved(num_elements * 2);

    for (int f = 0; f < N_freqs; ++f) {
        file.read((char*)interleaved.data(), interleaved.size() * sizeof(double));

        Eigen::MatrixXcf P_norm(m_N_array, m_N_depth * m_N_range);

        // 解析列优先的 RAW 数据，并直接做归一化，极大削减内存和后续运算
        for (int col = 0; col < m_N_depth * m_N_range; ++col) {
            float col_norm_sq = 0.0f;
            for (int row = 0; row < m_N_array; ++row) {
                int idx = (col * m_N_array + row) * 2;
                float real_part = static_cast<float>(interleaved[idx]);
                float imag_part = static_cast<float>(interleaved[idx+1]);
                P_norm(row, col) = std::complex<float>(real_part, imag_part);
                col_norm_sq += real_part * real_part + imag_part * imag_part;
            }
            // 归一化这一列
            float col_norm = std::sqrt(col_norm_sq);
            if (col_norm > 1e-12f) {
                for (int row = 0; row < m_N_array; ++row) {
                    P_norm(row, col) /= col_norm;
                }
            }
        }
        m_replicaDict[std::round(freqs[f])] = P_norm;
    }
    file.close();
    qDebug() << ">> 成功加载 Kraken_Cache.raw ! 缓存频点数:" << N_freqs;
}

double SelfValidator::calculateTheoreticalAngle(int targetId, double timeSeconds) {
    auto it = std::find_if(m_truthData.begin(), m_truthData.end(), [targetId](const TargetTruth& t) { return t.id == targetId; });
    if (it == m_truthData.end()) return -1.0;

    double X0 = it->initialDistance * std::cos(it->initialAngle * M_PI / 180.0);
    double Y0 = it->initialDistance * std::sin(it->initialAngle * M_PI / 180.0);

    double X = X0 + it->speed * std::cos(it->course * M_PI / 180.0) * timeSeconds;
    double Y = Y0 + it->speed * std::sin(it->course * M_PI / 180.0) * timeSeconds;

    double angleRad = std::atan2(Y, X);
    double angleDeg = angleRad * 180.0 / M_PI;
    if (angleDeg < 0) angleDeg += 360.0;

    return angleDeg;
}


// =========================================================================
// 核心：模拟接受信号 + 匹配场深度反演 (矩阵数学提速版)
// =========================================================================
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
        // 【关键修复】：寻找字典中最接近的频率键值，而不是死板的整数匹配
        int best_f_key = -1;
        double min_f_diff = 1e9;
        for (auto const& pair : m_replicaDict) {
            double diff = std::abs(pair.first - freq);
            if (diff < min_f_diff) {
                min_f_diff = diff;
                best_f_key = pair.first;
            }
        }

        // 如果提取出的线谱与真值库频率相差超过 3.5Hz，说明是干扰或虚警提取，跳过不参与匹配
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
            // 使用全局高精度引擎注入噪声
            p_noisy(i) = w_true(i) + std::complex<float>(dist(m_randGen), dist(m_randGen));
        }
        p_noisy.normalize();

        Eigen::VectorXf CMFP_single = (W.adjoint() * p_noisy).cwiseAbs2();
        CMFP_broadband += CMFP_single;
        computed = true;
    }

    // 如果所有的频点都没匹配上字典库，触发保护值
    if (!computed) return 10.0;

    int max_idx;
    CMFP_broadband.maxCoeff(&max_idx);
    int best_depth_idx = max_idx % m_N_depth;

    return m_depthCopy[best_depth_idx];
}

void SelfValidator::onBatchFinished(int batchIndex, int startFrame, int endFrame, const std::vector<BatchTargetFeature>& dspFeatures) {
    // 放弃使用 QTextStream，直接使用 QString 拼接 (和 DspWorker 保持一致)
    QString logOutput = "";

    // 每帧3秒，计算本批次结束时的绝对物理时间
    double timeSeconds = endFrame * 3.0;

    logOutput += "======================================================\n";
    logOutput += QString("第 %1 批数据 (帧 %2 - %3) 综合判别报告\n").arg(batchIndex).arg(startFrame).arg(endFrame);
    logOutput += "======================================================\n";

    int correctCount = 0;

    for (const auto& feature : dspFeatures) {
        int tId = feature.formalId;
        if (tId > m_truthData.size() || tId <= 0) continue;

        const TargetTruth& truth = m_truthData[tId - 1];

        // 方位验证
        double realAngle = calculateTheoreticalAngle(tId, timeSeconds);

        // 获取距离用于传递给 MFP
        double X0 = truth.initialDistance * std::cos(truth.initialAngle * M_PI / 180.0);
        double Y0 = truth.initialDistance * std::sin(truth.initialAngle * M_PI / 180.0);
        double X = X0 + truth.speed * std::cos(truth.course * M_PI / 180.0) * timeSeconds;
        double Y = Y0 + truth.speed * std::sin(truth.course * M_PI / 180.0) * timeSeconds;
        double current_range_km = std::sqrt(X*X + Y*Y) / 1000.0;

        // 执行核心的 C++ 高速 MFP 深度匹配
        double calDepth = estimateDepthMFP(truth.trueDepth, current_range_km, feature.calLofar);

        // 判决逻辑
        bool isEstSub = (calDepth > 20.0);
        bool isTrueSub = (truth.trueDepth > 20.0);

        QString estClassStr = isEstSub ? "[水下潜艇]" : "[水面舰船]";
        QString judgeStr = (isEstSub == isTrueSub) ? "判别正确" : "判别失败";
        if (isEstSub == isTrueSub) correctCount++;

        // 将频率数组转换为字符串
        QString calFreqStr = "";
        for(double f : feature.calLofar) calFreqStr += QString::number(f, 'f', 1) + " ";

        QString trueFreqStr = "";
        for(double f : truth.trueLofarFreqs) trueFreqStr += QString::number(f, 'f', 1) + " ";

        logOutput += QString("▶ 目标 %1：%2\n").arg(tId).arg(truth.name);
        logOutput += QString("  计算频率: [ %1] Hz  |  真实频率: [ %2] Hz\n").arg(calFreqStr).arg(trueFreqStr);
        logOutput += QString("  计算轴频: %1 Hz  |  真实轴频: %2 Hz\n").arg(feature.calDemon, 0, 'f', 1).arg(truth.trueDemonFreq, 0, 'f', 1);
        logOutput += QString("  计算方位: %1°  |  真实方位: %2°\n").arg(feature.calAngle, 0, 'f', 1).arg(realAngle, 0, 'f', 1);
        logOutput += QString("  计算深度: %1 m   |  真实深度: %2 m\n").arg(calDepth, 0, 'f', 1).arg(truth.trueDepth, 0, 'f', 1);
        // ... 前面保持不变 ...
                logOutput += QString("  综合判别: %1 -> %2\n").arg(estClassStr).arg(judgeStr);
                logOutput += "----------------------------------------------------\n";
            }

            double accuracy = dspFeatures.empty() ? 0.0 : (correctCount * 100.0 / dspFeatures.size());
            logOutput += QString("【系统验收结论】本批次识别正确率: %1%\n").arg(accuracy, 0, 'f', 2);

            // 【新增】：把批次号和正确率发射出去给主界面保存
            emit batchAccuracyComputed(batchIndex, accuracy);

            // 触发信号，将结果发给 MainWindow 显示
            emit validationLogReady(logOutput);
        }
