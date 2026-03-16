#include "CBFProcessor.h"
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

CBFProcessor::CBFProcessor(int M, double d, double c, double r_scan, int fs, int NFFT_R, int NFFT_WIN,
                           const std::vector<double>& f_band_lofar, const std::vector<double>& f_band_demon)
    : m_M(M), m_fs(fs), m_NFFT_R(NFFT_R), m_NFFT_WIN(NFFT_WIN)
{
    m_df = (double)m_fs / m_NFFT_WIN;

    // 1. 生成阵元位置 x_v (完美等效 MATLAB mod(M,2) 的判断)
    m_xv.resize(m_M); // <--- 【修改处：赋值给成员变量 m_xv】
    for (int i = 0; i < m_M; ++i) {
        double m_idx = i + 1; // 转为 1-based
        if (m_M % 2 == 1) m_xv(i) = (m_idx - std::ceil(m_M / 2.0)) * d;
        else              m_xv(i) = (m_idx - (m_M / 2.0 + 0.5)) * d;
    }

    // 2. 生成 theta_scan (严丝合缝对齐 MATLAB 生成 257 个点的逻辑)
    double dete_cos = 2.0 / 256.0;
    theta_scan.resize(257);
    theta_scan(0) = 0.0;
    for (int k = 1; k <= 256; ++k) {
        theta_scan(k) = std::acos(1.0 - k * dete_cos) * 180.0 / M_PI;
    }

    // 3. 计算球面波时延 tau_matrix (257 x M)
    tau_matrix.resize(theta_scan.size(), m_M);
    for (int i = 0; i < theta_scan.size(); ++i) {
        double theta_rad = theta_scan(i) * M_PI / 180.0;
        for (int m = 0; m < m_M; ++m) {
            // <--- 【修改处：调用 m_xv(m)】
            double Rm = std::sqrt(r_scan * r_scan + m_xv(m) * m_xv(m) - 2.0 * r_scan * m_xv(m) * std::cos(theta_rad));
            tau_matrix(i, m) = (Rm - r_scan) / c;
        }
    }

    // 4. 提取频带索引
    int half_fft_size = m_NFFT_WIN / 2 + 1;
    for (int i = 0; i < half_fft_size; ++i) {
        double f = i * m_df;
        if (f >= f_band_lofar[0] && f <= f_band_lofar[1]) f_n_lofar.push_back(i);
        if (f >= f_band_demon[0] && f <= f_band_demon[1]) f_n_demon.push_back(i);
    }

    f_lofar.resize(f_n_lofar.size());
    for (size_t i = 0; i < f_n_lofar.size(); ++i) f_lofar(i) = f_n_lofar[i] * m_df;

    f_demon.resize(f_n_demon.size());
    for (size_t i = 0; i < f_n_demon.size(); ++i) f_demon(i) = f_n_demon[i] * m_df;

    // 5. 初始化 FFTW 引擎
    fft_in = fftw_alloc_real(m_NFFT_WIN);
    fft_out = fftw_alloc_complex(m_NFFT_WIN);
    fft_plan = fftw_plan_dft_r2c_1d(m_NFFT_WIN, fft_in, fft_out, FFTW_MEASURE);
}

CBFProcessor::~CBFProcessor() {
    fftw_destroy_plan(fft_plan);
    fftw_free(fft_in);
    fftw_free(fft_out);
}

CBFResult CBFProcessor::process(const Eigen::MatrixXd& signal_w) {
    CBFResult res;
    int f_num = f_lofar.size();
    int demon_num = f_demon.size();
    int theta_len = theta_scan.size();

    res.signal_fft_lofar.resize(m_M, f_num);
    res.signal_fft_demon.resize(m_M, demon_num);

    // 1. 逐阵元通道进行 FFT
    for (int m = 0; m < m_M; ++m) {
        for (int i = 0; i < m_NFFT_WIN; ++i) {
            fft_in[i] = signal_w(m, i);
        }
        fftw_execute(fft_plan);

        for (size_t i = 0; i < f_n_lofar.size(); ++i) {
            int idx = f_n_lofar[i];
            res.signal_fft_lofar(m, i) = std::complex<double>(fft_out[idx][0], fft_out[idx][1]) / (double)m_NFFT_R;
        }

        for (size_t i = 0; i < f_n_demon.size(); ++i) {
            int idx = f_n_demon[i];
            res.signal_fft_demon(m, i) = std::complex<double>(fft_out[idx][0], fft_out[idx][1]) / (double)m_NFFT_R;
        }
    }

    // 2. 高阶宽带波束形成 (极速并行实现)
    res.P_out = Eigen::MatrixXd::Zero(theta_len, f_num);
    res.P_cbf_spatial = Eigen::VectorXd::Zero(theta_len);
    std::complex<double> J(0, 1);

    for (int ii = 0; ii < theta_len; ++ii) {
        Eigen::MatrixXd Phase = 2.0 * M_PI * tau_matrix.row(ii).transpose() * f_lofar.transpose();
        Eigen::MatrixXcd W_steer = (J * Phase).array().exp();

        Eigen::RowVectorXcd beam_f = (res.signal_fft_lofar.array() * W_steer.array()).colwise().sum();
        Eigen::RowVectorXd p_out_row = 2.0 * beam_f.array().abs2() / m_df;

        res.P_out.row(ii) = p_out_row;
        res.P_cbf_spatial(ii) = p_out_row.sum();
    }

    return res;
}
