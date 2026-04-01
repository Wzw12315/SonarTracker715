#include "MfpProcessor.h"
#include <QFile>
#include <QDataStream>
#include <cmath>
#include <chrono>
#include <random>
MfpProcessor::MfpProcessor()
    : N_array(0), N_depth(0), N_range(0) {
}

MfpProcessor::~MfpProcessor() {
    // 默认析构，std::map 和 std::vector 会自动清理内存
}

bool MfpProcessor::loadKrakenRaw(const QString& filepath) {
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian); // MATLAB 默认为 Little Endian

    qint32 header[4];
    for(int i = 0; i < 4; ++i) in >> header[i];
    int N_freqs = header[0];
    N_array = header[1];
    N_depth = header[2];
    N_range = header[3];

    // 初始化深度网格 (根据 MATLAB 脚本 0:1:200)
    depth_copy.resize(N_depth);
    for(int i = 0; i < N_depth; ++i) depth_copy[i] = i * 1.0;

    std::vector<double> unique_freqs(N_freqs);
    for(int i = 0; i < N_freqs; ++i) in >> unique_freqs[i];

    int num_elements = N_array * N_depth * N_range;
    for(int f = 0; f < N_freqs; ++f) {
        std::vector<double> interleaved_data(num_elements * 2);
        for(int i = 0; i < num_elements * 2; ++i) in >> interleaved_data[i];

        // 构建 [N_array x (N_depth * N_range)] 的拷贝场矩阵
        Eigen::MatrixXcd pr(N_array, N_depth * N_range);
        for(int n = 0; n < num_elements; ++n) {
            int array_idx = n % N_array;
            int grid_idx = n / N_array;
            pr(array_idx, grid_idx) = std::complex<double>(
                interleaved_data[2*n], interleaved_data[2*n+1]);
        }

        // 提前归一化：除以列的模长
        Eigen::VectorXd col_norms = pr.colwise().norm();
        for(int c = 0; c < pr.cols(); ++c) {
            if(col_norms(c) > 1e-12) pr.col(c) /= col_norms(c);
        }
        replica_dict[unique_freqs[f]] = pr;
    }
    return true;
}

double MfpProcessor::estimateDepth(const std::vector<double>& target_freqs,
                                   const std::map<double, Eigen::MatrixXcd>& target_R_matrices) {
    // 1. 在最外层初始化累加器，确保它是宽带多频点非相干叠加
    Eigen::VectorXd CMFP_broadband = Eigen::VectorXd::Zero(N_depth * N_range);
    int valid_match_count = 0; // 记录成功匹配的频点数

    for(double freq : target_freqs) {
        if(target_R_matrices.find(freq) == target_R_matrices.end()) continue;

        // 2. 邻近匹配策略（容差 5Hz）
        double best_match_freq = -1.0;
        double min_diff = 5.0;
        for(auto const& pair : replica_dict) {
            if (std::abs(pair.first - freq) < min_diff) {
                min_diff = std::abs(pair.first - freq);
                best_match_freq = pair.first;
            }
        }

        if (best_match_freq < 0) continue;
        valid_match_count++;

        const Eigen::MatrixXcd& pr_norm = replica_dict[best_match_freq]; // [N_array x (N_depth*N_range)]
        const Eigen::MatrixXcd& R_matrix = target_R_matrices.at(freq);   // [N_array x N_array]

        // 3. 核心计算：完全还原 MATLAB 的 abs(sum((pr_norm' * R_matrix).' .* pr_norm))
        // 在 C++ Eigen 中，最快且最准确的等价写法如下：

        // 步骤 a: 计算 R_matrix * pr_norm
        // R_matrix 维度 [M x M], pr_norm 维度 [M x Grid]
        // 结果 R_pr 维度 [M x Grid]
        Eigen::MatrixXcd R_pr = R_matrix * pr_norm;

        // 步骤 b: 计算 pr_norm 的共轭转置 (也就是 pr_norm') 每一列与 R_pr 每一列的点乘求和
        // 也就是相当于：对角线提取 w^H * R * w
        // 由于是复数，P = (pr_norm.conjugate().array() * R_pr.array()).colwise().sum()
        Eigen::RowVectorXcd P_complex = (pr_norm.conjugate().array() * R_pr.array()).colwise().sum();

        // 步骤 c: 取绝对值并累加到宽带能量谱中
        CMFP_broadband += P_complex.cwiseAbs().transpose();
    }

    // 4. 如果连一个频点都没匹配上，说明这帧的信号全废了，不能判 0，返回 -1
    if (valid_match_count == 0) return -1.0;

    // 5. 寻找最大峰值索引
    Eigen::Index maxRow, maxCol;
    CMFP_broadband.maxCoeff(&maxRow, &maxCol);

    // 6. 映射回深度 (Grid = range_idx * N_depth + depth_idx)
    int depth_idx = maxRow % N_depth;
    return depth_copy[depth_idx];
}




// 【新增实现】：完美复刻 SelfValidator 的仿真查表逻辑
double MfpProcessor::simulateDepthMFP(double true_depth, double true_range_km, const std::vector<double>& freqs) {
    if (replica_dict.empty() || N_depth == 0 || N_range == 0) return 10.0;

    int depth_idx = 0; double min_d = 1e9;
    for(int i = 0; i < N_depth; ++i) {
        if(std::abs(depth_copy[i] - true_depth) < min_d) { min_d = std::abs(depth_copy[i] - true_depth); depth_idx = i; }
    }

    int range_idx = 0; double min_r = 1e9;
    for(int i = 0; i < N_range; ++i) {
        double r_val = i * 0.05;
        if(std::abs(r_val - true_range_km) < min_r) { min_r = std::abs(r_val - true_range_km); range_idx = i; }
    }

    int true_grid_idx = range_idx * N_depth + depth_idx;
    Eigen::VectorXd CMFP_broadband = Eigen::VectorXd::Zero(N_depth * N_range);
    bool computed = false;

    // 锁定随机种子，保证每次跑出来的值和 SelfValidator 分毫不差
    std::mt19937 randGen(12345);

    for (double freq : freqs) {
        int best_f_key = -1;
        double min_f_diff = 1e9;
        for (auto const& pair : replica_dict) {
            double diff = std::abs(pair.first - freq);
            if (diff < min_f_diff) { min_f_diff = diff; best_f_key = pair.first; }
        }

        if (best_f_key == -1 || min_f_diff > 3.5) continue;

        const Eigen::MatrixXcd& W = replica_dict.at(best_f_key);
        Eigen::VectorXcd w_true = W.col(true_grid_idx);

        double SNR = 10.0;
        double signal_power = 1.0 / N_array;
        double noise_power = signal_power / std::pow(10.0, SNR / 10.0);
        double noise_std = std::sqrt(noise_power / 2.0);
        std::normal_distribution<double> dist(0.0, noise_std);

        Eigen::VectorXcd p_noisy(N_array);
        for(int i = 0; i < N_array; ++i) {
            p_noisy(i) = w_true(i) + std::complex<double>(dist(randGen), dist(randGen));
        }
        p_noisy.normalize();

        Eigen::VectorXd CMFP_single = (W.adjoint() * p_noisy).cwiseAbs2();
        CMFP_broadband += CMFP_single;
        computed = true;
    }

    if (!computed) return 10.0;

    Eigen::Index maxRow, maxCol;
    CMFP_broadband.maxCoeff(&maxRow, &maxCol);
    int best_depth_idx = maxRow % N_depth;

    return depth_copy[best_depth_idx];
}
