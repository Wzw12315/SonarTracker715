#include "Deconvolution.h"
#include <stdexcept>
#include <numeric>
#include <limits>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <QDebug>

// 辅助函数实现
Eigen::MatrixXd linspace(double start, double end, int num_points)
{
    if (num_points <= 0) {
        throw std::invalid_argument("num_points must be positive");
    }
    Eigen::MatrixXd result(1, num_points);
    if (num_points == 1) {
        result(0, 0) = start;
        return result;
    }
    double step = (end - start) / (num_points - 1);
    for (int i = 0; i < num_points; ++i) {
        result(0, i) = start + i * step;
    }
    return result;
}

Eigen::MatrixXd rot90_2(const Eigen::MatrixXd& mat)
{
    Eigen::MatrixXd flipped_updown = mat.rowwise().reverse();
    Eigen::MatrixXd flipped_leftright = flipped_updown.colwise().reverse();
    return flipped_leftright;
}

void meshgrid(const Eigen::MatrixXd& x, const Eigen::MatrixXd& y, Eigen::MatrixXd& UX, Eigen::MatrixXd& UY)
{
    if (x.rows() != 1 || y.rows() != 1) {
        throw std::invalid_argument("x and y must be row vectors");
    }
    int nx = x.cols();
    int ny = y.cols();
    UX.resize(ny, nx);
    UY.resize(ny, nx);
    for (int i = 0; i < ny; ++i) {
        UX.row(i) = x;
    }
    for (int j = 0; j < nx; ++j) {
        UY.col(j) = y.transpose();
    }
}

Eigen::MatrixXd acosd(const Eigen::MatrixXd& mat)
{
    return mat.unaryExpr([](double x) {
        double clamped = std::max(-1.0, std::min(1.0, x));
        return std::acos(clamped) * 180.0 / M_PI;
    });
}

Eigen::MatrixXd cosd(const Eigen::MatrixXd& mat)
{
    return mat.unaryExpr([](double x) {
        return std::cos(x * M_PI / 180.0);
    });
}

double bilinearInterp(
    const Eigen::MatrixXd& gridX,
    const Eigen::MatrixXd& gridY,
    const Eigen::MatrixXd& gridZ,
    double x, double y
) {
    if (gridX.rows() != 1 || gridY.rows() != 1) return 0.0;
    int M = gridX.cols();
    int N = gridY.cols();
    if (M < 2 || N < 2) return 0.0;

    // 找到x的左右索引
    int x_idx = 0;
    while (x_idx < M-1 && gridX(0, x_idx+1) < x) x_idx++;
    if (x_idx >= M-1 || x_idx < 0) return 0.0;
    double x0 = gridX(0, x_idx), x1 = gridX(0, x_idx+1);
    if (std::abs(x1 - x0) < 1e-12) return 0.0;

    // 找到y的上下索引
    int y_idx = 0;
    while (y_idx < N-1 && gridY(0, y_idx+1) < y) y_idx++;
    if (y_idx >= N-1 || y_idx < 0) return 0.0;
    double y0 = gridY(0, y_idx), y1 = gridY(0, y_idx+1);
    if (std::abs(y1 - y0) < 1e-12) return 0.0;

    // 双线性插值
    double z00 = gridZ(y_idx, x_idx);
    double z01 = gridZ(y_idx, x_idx+1);
    double z10 = gridZ(y_idx+1, x_idx);
    double z11 = gridZ(y_idx+1, x_idx+1);
    double t = (x - x0) / (x1 - x0);
    double u = (y - y0) / (y1 - y0);
    double z = (1-t)*(1-u)*z00 + t*(1-u)*z01 + (1-t)*u*z10 + t*u*z11;
    return std::max(0.0, z);
}

// RL反卷积函数
Eigen::MatrixXd RL(const Eigen::MatrixXd& P, const Eigen::MatrixXd& PSF, int iterations, Eigen::MatrixXd& loss_curve)
{
    Eigen::MatrixXd P_clean = P.unaryExpr([](double x) { return std::max(0.0, x); });
    Eigen::MatrixXd PSF_clean = PSF.unaryExpr([](double x) { return std::max(0.0, x); });

    double sum_P = P_clean.sum();
    Eigen::MatrixXd P_norm = sum_P > 0 ? P_clean / sum_P : P_clean;

    double sum_PSF = PSF_clean.sum();
    Eigen::MatrixXd PSF_norm = sum_PSF > 0 ? PSF_clean / sum_PSF : PSF_clean;

    Eigen::MatrixXd PSF_rot = rot90_2(PSF_norm);

    int Hp = P_norm.rows();
    int Wp = P_norm.cols();
    int Hh = PSF_norm.rows();
    int Wh = PSF_norm.cols();

    int fft_H = Hp + Hh - 1;
    int fft_W = Wp + Wh - 1;

    // FFT初始化
    fftw_complex *psf_in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
    fftw_complex *psf_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
    fftw_plan psf_fft_plan = fftw_plan_dft_2d(fft_H, fft_W, psf_in, psf_out, FFTW_FORWARD, FFTW_ESTIMATE);
    memset(psf_in, 0, sizeof(fftw_complex) * fft_H * fft_W);
    for (int i = 0; i < Hh; ++i) {
        for (int j = 0; j < Wh; ++j) {
            int idx = i * fft_W + j;
            psf_in[idx][0] = PSF_norm(i, j);
            psf_in[idx][1] = 0.0;
        }
    }
    fftw_execute(psf_fft_plan);

    fftw_complex *psf_rot_in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
    fftw_complex *psf_rot_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
    fftw_plan psf_rot_fft_plan = fftw_plan_dft_2d(fft_H, fft_W, psf_rot_in, psf_rot_out, FFTW_FORWARD, FFTW_ESTIMATE);
    memset(psf_rot_in, 0, sizeof(fftw_complex) * fft_H * fft_W);
    for (int i = 0; i < Hh; ++i) {
        for (int j = 0; j < Wh; ++j) {
            int idx = i * fft_W + j;
            psf_rot_in[idx][0] = PSF_rot(i, j);
            psf_rot_in[idx][1] = 0.0;
        }
    }
    fftw_execute(psf_rot_fft_plan);

//    int str_r = floor(Hh / 2.0);
//    int end_r = str_r + Hp - 1;
//    int str_c = floor(Wh / 2.0);
//    int end_c = str_c + Wp - 1;
    // 第一步：先对齐Matlab的1-based索引逻辑（和Matlab代码完全一致）
    int str_r = floor(Hh / 2.0) + 1;
    int end_r = str_r + Hp - 1;
    int str_c = floor(Wh / 2.0) + 1;
    int end_c = str_c + Wp - 1;

    // 第二步：转换为Eigen的0-based索引（适配C++ Eigen矩阵的索引规则）
    str_r -= 1; end_r -= 1;
    str_c -= 1; end_c -= 1;
    Eigen::MatrixXd S_est = P_norm;
    loss_curve.resize(1, iterations);

    for (int it = 0; it < iterations; ++it) {
        // 计算P_est = conv2(S_est, PSF, 'same')
        fftw_complex *s_est_in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
        fftw_complex *s_est_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
        fftw_plan s_est_fft_plan = fftw_plan_dft_2d(fft_H, fft_W, s_est_in, s_est_out, FFTW_FORWARD, FFTW_ESTIMATE);
        memset(s_est_in, 0, sizeof(fftw_complex) * fft_H * fft_W);
        for (int i = 0; i < Hp; ++i) {
            for (int j = 0; j < Wp; ++j) {
                int idx = i * fft_W + j;
                s_est_in[idx][0] = S_est(i, j);
                s_est_in[idx][1] = 0.0;
            }
        }
        fftw_execute(s_est_fft_plan);

        fftw_complex *conv_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
        for (int i = 0; i < fft_H * fft_W; ++i) {
            double re1 = s_est_out[i][0], im1 = s_est_out[i][1];
            double re2 = psf_out[i][0], im2 = psf_out[i][1];
            conv_out[i][0] = re1 * re2 - im1 * im2;
            conv_out[i][1] = re1 * im2 + im1 * re2;
        }

        fftw_plan conv_ifft_plan = fftw_plan_dft_2d(fft_H, fft_W, conv_out, conv_out, FFTW_BACKWARD, FFTW_ESTIMATE);
        fftw_execute(conv_ifft_plan);

        Eigen::MatrixXd P_est(Hp, Wp);
        for (int i = 0; i < Hp; ++i) {
            for (int j = 0; j < Wp; ++j) {
                int idx = (str_r + i) * fft_W + (str_c + j);
                P_est(i, j) = conv_out[idx][0] / (fft_H * fft_W);
            }
        }

        // 计算ratio
        Eigen::MatrixXd ratio = P_norm.array() / (P_est.array() + std::numeric_limits<double>::epsilon());

        // 计算损失
        Eigen::MatrixXd diff = P_norm - P_est;
        double sum_diff_sq = diff.array().square().sum();
        double sum_P_sq = P_norm.array().square().sum();
        loss_curve(0, it) = sum_diff_sq / (sum_P_sq + std::numeric_limits<double>::epsilon());

        // 计算correction
        fftw_complex *ratio_in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
        fftw_complex *ratio_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
        fftw_plan ratio_fft_plan = fftw_plan_dft_2d(fft_H, fft_W, ratio_in, ratio_out, FFTW_FORWARD, FFTW_ESTIMATE);
        memset(ratio_in, 0, sizeof(fftw_complex) * fft_H * fft_W);
        for (int i = 0; i < Hp; ++i) {
            for (int j = 0; j < Wp; ++j) {
                int idx = i * fft_W + j;
                ratio_in[idx][0] = ratio(i, j);
                ratio_in[idx][1] = 0.0;
            }
        }
        fftw_execute(ratio_fft_plan);

        fftw_complex *corr_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_H * fft_W);
        for (int i = 0; i < fft_H * fft_W; ++i) {
            double re1 = ratio_out[i][0], im1 = ratio_out[i][1];
            double re2 = psf_rot_out[i][0], im2 = psf_rot_out[i][1];
            corr_out[i][0] = re1 * re2 - im1 * im2;
            corr_out[i][1] = re1 * im2 + im1 * re2;
        }

        fftw_plan corr_ifft_plan = fftw_plan_dft_2d(fft_H, fft_W, corr_out, corr_out, FFTW_BACKWARD, FFTW_ESTIMATE);
        fftw_execute(corr_ifft_plan);

        Eigen::MatrixXd correction(Hp, Wp);
        for (int i = 0; i < Hp; ++i) {
            for (int j = 0; j < Wp; ++j) {
                int idx = (str_r + i) * fft_W + (str_c + j);
                correction(i, j) = corr_out[idx][0] / (fft_H * fft_W);
            }
        }

        // 更新S_est
        S_est = S_est.cwiseProduct(correction);
        S_est = S_est.unaryExpr([](double x) { return std::max(0.0, x); });
        double sum_S_est = S_est.sum();
        if (sum_S_est > 0) {
            S_est = S_est / sum_S_est;
        }

        // 释放内存
        fftw_destroy_plan(s_est_fft_plan);
        fftw_destroy_plan(conv_ifft_plan);
        fftw_destroy_plan(ratio_fft_plan);
        fftw_destroy_plan(corr_ifft_plan);
        fftw_free(s_est_in);
        fftw_free(s_est_out);
        fftw_free(conv_out);
        fftw_free(ratio_in);
        fftw_free(ratio_out);
        fftw_free(corr_out);
    }

    // 释放预计算内存
    fftw_destroy_plan(psf_fft_plan);
    fftw_destroy_plan(psf_rot_fft_plan);
    fftw_free(psf_in);
    fftw_free(psf_out);
    fftw_free(psf_rot_in);
    fftw_free(psf_rot_out);

    return S_est;
}

// 反卷积主函数
void deconv_processor(
    const Eigen::MatrixXcd& signal_fft,
    int M_x,
    const Eigen::MatrixXd& x_v,
    int M_start,
    const Eigen::MatrixXd& f,
    const Eigen::MatrixXd& theta_scan,
    int M,
    double d,
    double c,
    int fs,
    int N_fft,
    int rl_iter,
    Eigen::MatrixXd& S_uxuy,
    Eigen::MatrixXd& P_uxuy_norm,
    Eigen::MatrixXd& P,
    Eigen::MatrixXd& ux_bins,
    Eigen::MatrixXd& uy_bins
) {
    Eigen::MatrixXd f_row = (f.rows() == 1) ? f : f.transpose();
    Eigen::MatrixXd theta_scan_row = (theta_scan.rows() == 1) ? theta_scan : theta_scan.transpose();

    double ux_max = f_row.maxCoeff();
    double ux_min = f_row.minCoeff();
    int f_length = f_row.cols();
    int theta_length = theta_scan_row.cols();

    ux_bins = linspace(ux_min, ux_max, f_length);
    uy_bins = linspace(-ux_max, ux_max, theta_length);

    Eigen::MatrixXd UX, UY;
    meshgrid(ux_bins, uy_bins, UX, UY);
    Eigen::MatrixXd THETA = acosd(UY.cwiseQuotient(UX));

    Eigen::MatrixXd xpos = x_v.block(0, M_start, 1, M_x).transpose();
    int uy_len = uy_bins.cols();
    Eigen::MatrixXcd aa(M_x, uy_len);
    for (int i = 0; i < M_x; ++i) {
        for (int j = 0; j < uy_len; ++j) {
            double xpos_val = xpos(i, 0);
            double uy_bins_val = uy_bins(0, j);
            double arg = -2 * M_PI * (xpos_val * uy_bins_val) / c;
            arg = fmod(arg, 2 * M_PI);
            aa(i, j) = std::complex<double>(std::cos(arg), std::sin(arg));
        }
    }

    Eigen::MatrixXcd bf = aa.adjoint() * signal_fft;
    bf /= static_cast<double>(M_x);
    Eigen::MatrixXd P_uxuy = bf.array().abs2();

    Eigen::MatrixXd cos_theta = UY.cwiseQuotient(UX);
    Eigen::Matrix<bool, Eigen::Dynamic, Eigen::Dynamic> mask_bool = (UX.array() > 0) && (cos_theta.array().abs() <= 1.0);
    Eigen::MatrixXd mask = mask_bool.cast<double>();
    P_uxuy = P_uxuy.cwiseProduct(mask);

    double P_uxuy_max = P_uxuy.maxCoeff();
    P_uxuy_norm = P_uxuy_max > 0 ? P_uxuy / P_uxuy_max : P_uxuy;

    // 生成PSF
    double ux0 = (ux_min + ux_max) / 2.0;
    double uy0 = 0.0;
    Eigen::MatrixXd uy_bins_col = uy_bins.transpose();
    Eigen::MatrixXd dy = uy_bins_col.unaryExpr([uy0](double val) { return val - uy0; });
    Eigen::MatrixXd dx = ux_bins.unaryExpr([ux0](double val) { return val - ux0; });

    Eigen::MatrixXd aa_psf(dy.rows(), 1);
    for (int i = 0; i < dy.rows(); ++i) {
        double arg = M_PI * d * dy(i, 0) / c;
        if (std::abs(arg) < 1e-9) {
            aa_psf(i, 0) = static_cast<double>(M);
        } else {
            aa_psf(i, 0) = sin(M * arg) / sin(arg);
        }
    }

    Eigen::MatrixXd bb(1, dx.cols());
    for (int j = 0; j < dx.cols(); ++j) {
        double arg = M_PI * dx(0, j) / fs;
        if (std::abs(arg) < 1e-9) {
            bb(0, j) = static_cast<double>(N_fft);
        } else {
            bb(0, j) = sin(N_fft * arg) / sin(arg);
        }
    }

//    Eigen::MatrixXd Yp = (aa_psf * bb).array().square();
//    double Yp_max = Yp.maxCoeff();
//    Eigen::MatrixXd PSF = Yp_max > 0 ? Yp / Yp_max : Yp;
//    PSF /= PSF.sum();
    // 修正为（添加mask过滤）
    Eigen::MatrixXd Yp = (aa_psf * bb).array().square();
    // 与P_uxuy使用相同的mask，确保PSF在无效区域为0
    Yp = Yp.cwiseProduct(mask);
    double Yp_max = Yp.maxCoeff();
    Eigen::MatrixXd PSF = Yp_max > 0 ? Yp / Yp_max : Yp;
    PSF /= PSF.sum();

    // RL反卷积
    Eigen::MatrixXd loss_curve;
    Eigen::MatrixXd P_uxuy_normalized = P_uxuy;
    double P_uxuy_sum = P_uxuy.sum();
    if (P_uxuy_sum > 0) {
        P_uxuy_normalized /= P_uxuy_sum;
    }
    S_uxuy = RL(P_uxuy_normalized, PSF, rl_iter, loss_curve);

//    // 对齐Matlab：S_uxuy归一化
//    double S_uxuy_max = S_uxuy.maxCoeff();
//    if (S_uxuy_max > 0) {
//        S_uxuy = S_uxuy / S_uxuy_max;
//    }

    // 双线性插值
    Eigen::MatrixXd F_grid, TH_grid;
    meshgrid(f_row, theta_scan_row, F_grid, TH_grid);
    Eigen::MatrixXd Q_ux = F_grid;
    Eigen::MatrixXd Q_uy = F_grid.cwiseProduct(cosd(TH_grid));

    P.resize(Q_ux.rows(), Q_ux.cols());
    for (int i = 0; i < Q_ux.rows(); ++i) {
        for (int j = 0; j < Q_ux.cols(); ++j) {
            P(i, j) = bilinearInterp(ux_bins, uy_bins, S_uxuy, Q_ux(i,j), Q_uy(i,j));
        }
    }
}
