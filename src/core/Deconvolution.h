#ifndef DECONVOLUTION_H
#define DECONVOLUTION_H

#include <Eigen/Dense>
#include <fftw3.h>
#include <vector>

// 辅助函数声明
Eigen::MatrixXd linspace(double start, double end, int num_points);
Eigen::MatrixXd rot90_2(const Eigen::MatrixXd& mat);
void meshgrid(const Eigen::MatrixXd& x, const Eigen::MatrixXd& y, Eigen::MatrixXd& UX, Eigen::MatrixXd& UY);
Eigen::MatrixXd acosd(const Eigen::MatrixXd& mat);
Eigen::MatrixXd cosd(const Eigen::MatrixXd& mat);
double bilinearInterp(
    const Eigen::MatrixXd& gridX,
    const Eigen::MatrixXd& gridY,
    const Eigen::MatrixXd& gridZ,
    double x, double y
);

// RL反卷积函数
Eigen::MatrixXd RL(const Eigen::MatrixXd& P, const Eigen::MatrixXd& PSF, int iterations, Eigen::MatrixXd& loss_curve);

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
);

#endif // DECONVOLUTION_H
