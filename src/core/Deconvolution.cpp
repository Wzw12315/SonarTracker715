#include "Deconvolution.h"
#include <Eigen/Dense>
#include <vector>
#include <cmath>
#include <algorithm>

// ====================================================================
// 【核心优化】：纯 C++ SIMD 极速一维卷积，完美等效 conv(x, h, 'same')
// 完全移除了 FFTW 库，彻底解决了 OpenMP 多线程下的严重闪退问题！
// ====================================================================
static Eigen::VectorXd conv_same(const Eigen::VectorXd& x, const Eigen::VectorXd& h) {
    int nx = x.size();
    int nh = h.size();

    // 【防闪退保护】：如果输入突然变为空矩阵，立刻返回，防止崩溃
    if (nx == 0 || nh == 0) return x;

    Eigen::VectorXd y = Eigen::VectorXd::Zero(nx);
    int half_h = nh / 2;

    const double* x_data = x.data();
    const double* h_data = h.data();
    double* y_data = y.data();

    for (int i = 0; i < nx; ++i) {
        double sum = 0.0;
        int n_full = i + half_h;
        int k_start = std::max(0, n_full - nx + 1);
        int k_end = std::min(nh - 1, n_full);

        // 强制编译器使用 AVX/SSE 寄存器展开这个纯数学乘加运算
        #pragma omp simd reduction(+:sum)
        for (int k = k_start; k <= k_end; ++k) {
            sum += x_data[n_full - k] * h_data[k];
        }
        y_data[i] = sum;
    }
    return y;
}

Eigen::VectorXd RL_1D(const Eigen::VectorXd& P, const Eigen::VectorXd& PSF, int iterations) {
    double eps = 1e-12;
    Eigen::VectorXd P_clean = P.cwiseMax(eps);
    Eigen::VectorXd PSF_clean = PSF.cwiseMax(eps);

    double sum_P = P_clean.sum();
    Eigen::VectorXd S_est = P; // 兜底返回
    if (sum_P <= 0) return S_est;
    Eigen::VectorXd P_norm = P_clean / sum_P;

    double sum_PSF = PSF_clean.sum();
    Eigen::VectorXd PSF_norm = PSF_clean / sum_PSF;

    // 生成翻转后的 PSF_rot
    Eigen::VectorXd PSF_rot = PSF_norm.reverse();

    S_est = P_norm;

    for (int it = 0; it < iterations; ++it) {
        // 1. P_est = conv2(S_est, PSF, 'same')
        Eigen::VectorXd P_est = conv_same(S_est, PSF_norm);

        // 2. ratio = P / P_est
        Eigen::VectorXd ratio = P_norm.array() / (P_est.array() + eps);

        // 3. correction = conv2(ratio, PSF_rot, 'same')
        Eigen::VectorXd correction = conv_same(ratio, PSF_rot);

        // 4. 更新 S_est
        S_est = S_est.cwiseProduct(correction);
        S_est = S_est.cwiseMax(0.0);
        double s_sum = S_est.sum();
        if (s_sum > 0) S_est /= s_sum;
    }

    return S_est;
}
