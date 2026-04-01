#pragma once

#include <QString>
#include <Eigen/Dense>
#include <map>
#include <vector>
#include <complex>

class MfpProcessor {
public:
    MfpProcessor();
    ~MfpProcessor();

    // 加载 Kraken RAW 格式拷贝场数据
    bool loadKrakenRaw(const QString& filepath);

    // 执行 CMFP 宽带匹配并返回最大深度峰值
    double estimateDepth(const std::vector<double>& target_freqs,
                         const std::map<double, Eigen::MatrixXcd>& target_R_matrices);
    // 【新增】：根据真值距离，直接生成等效模拟信号并查表，确保与批次日志输出绝对一致
        double simulateDepthMFP(double true_depth, double true_range_km, const std::vector<double>& freqs);
    // 拷贝场缓存：频点 -> 归一化复数矩阵 [N_array x (N_depth * N_range)]
    std::map<double, Eigen::MatrixXcd> replica_dict;

    int N_array;
    int N_depth;
    int N_range;
    std::vector<double> depth_copy; // 深度网格
};
