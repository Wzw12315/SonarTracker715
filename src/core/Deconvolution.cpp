#include "Deconvolution.h"
#include <fftw3.h>
#include <vector>
#include <cmath>
#include <algorithm>

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
    Eigen::VectorXd PSF_rot = PSF_norm.reverse();

    int N = P_norm.size();
    int Nh = PSF_norm.size();
    int N_fft = N + Nh - 1;

    // FFTW 内存分配与计划
    fftw_complex *in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N_fft);
    fftw_complex *out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N_fft);
    fftw_plan plan_fwd = fftw_plan_dft_1d(N_fft, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_plan plan_bwd = fftw_plan_dft_1d(N_fft, in, out, FFTW_BACKWARD, FFTW_ESTIMATE);

    // 预计算正向 PSF 频谱 (OTF)
    std::vector<std::complex<double>> OTF(N_fft);
    memset(in, 0, sizeof(fftw_complex) * N_fft);
    for (int i = 0; i < Nh; ++i) in[i][0] = PSF_norm(i);
    fftw_execute(plan_fwd);
    for (int i = 0; i < N_fft; ++i) OTF[i] = std::complex<double>(out[i][0], out[i][1]);

    // 预计算翻转 PSF 频谱 (OTF_rot)
    std::vector<std::complex<double>> OTF_rot(N_fft);
    memset(in, 0, sizeof(fftw_complex) * N_fft);
    for (int i = 0; i < Nh; ++i) in[i][0] = PSF_rot(i);
    fftw_execute(plan_fwd);
    for (int i = 0; i < N_fft; ++i) OTF_rot[i] = std::complex<double>(out[i][0], out[i][1]);

    // 'same' 卷积截取窗口
    int str_idx = std::floor(Nh / 2.0);

    S_est = P_norm;

    for (int it = 0; it < iterations; ++it) {
        // 1. P_est = conv2(S_est, PSF, 'same')
        memset(in, 0, sizeof(fftw_complex) * N_fft);
        for (int i = 0; i < N; ++i) in[i][0] = S_est(i);
        fftw_execute(plan_fwd);

        for (int i = 0; i < N_fft; ++i) {
            std::complex<double> val = std::complex<double>(out[i][0], out[i][1]) * OTF[i];
            in[i][0] = val.real();
            in[i][1] = val.imag();
        }
        fftw_execute(plan_bwd);

        Eigen::VectorXd P_est(N);
        for (int i = 0; i < N; ++i) {
            P_est(i) = out[str_idx + i][0] / N_fft; // ✅ 修复：必须从 out 数组读取 IFFT 结果
        }

        // 2. ratio = P / P_est
        Eigen::VectorXd ratio = P_norm.array() / (P_est.array() + eps);

        // 3. correction = conv2(ratio, PSF_rot, 'same')
        memset(in, 0, sizeof(fftw_complex) * N_fft);
        for (int i = 0; i < N; ++i) in[i][0] = ratio(i);
        fftw_execute(plan_fwd);

        for (int i = 0; i < N_fft; ++i) {
            std::complex<double> val = std::complex<double>(out[i][0], out[i][1]) * OTF_rot[i];
            in[i][0] = val.real();
            in[i][1] = val.imag();
        }
        fftw_execute(plan_bwd);

        Eigen::VectorXd correction(N);
        for (int i = 0; i < N; ++i) {
            correction(i) = out[str_idx + i][0] / N_fft; // ✅ 修复：必须从 out 数组读取 IFFT 结果
        }

        // 4. 更新 S_est
        S_est = S_est.cwiseProduct(correction);
        S_est = S_est.cwiseMax(0.0);
        double s_sum = S_est.sum();
        if (s_sum > 0) S_est /= s_sum;
    }

    fftw_destroy_plan(plan_fwd); fftw_destroy_plan(plan_bwd);
    fftw_free(in); fftw_free(out);

    return S_est;
}
