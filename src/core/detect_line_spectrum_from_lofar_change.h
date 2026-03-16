#ifndef DETECT_LINE_SPECTRUM_FROM_LOFAR_CHANGE_H
#define DETECT_LINE_SPECTRUM_FROM_LOFAR_CHANGE_H

#include <Eigen/Dense>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>

#define EPS 2.220446049250313e-16

void detect_line_spectrum_from_lofar_change(
    const Eigen::MatrixXd& lofar_mat,
    double fs,
    int NFFT,
    Eigen::RowVectorXd& line_spectrum_center_freq,
    Eigen::MatrixXd& Z_TPSW,
    Eigen::MatrixXi& counter,
    Eigen::RowVectorXd& f_stft,
    Eigen::RowVectorXd& t_stft,
    double G = 45.0,
    double E = 2.0,
    double C = 1.15,
    int L = 5,
    double alpha = 1.5,
    double beta = 1.0,
    double gamma = 0.1
);

Eigen::MatrixXd tpsw_normalization(const Eigen::MatrixXd& X, double G, double E, double C);

void calc_spectrum_feature(
    const Eigen::MatrixXd& Pxx_linear,
    const Eigen::RowVectorXd& f_stft,
    int N_stft,
    int win_freq_len,
    double thresh_break,
    double alpha,
    double beta,
    double gamma,
    double fs,
    Eigen::RowVectorXd& phi_f,
    Eigen::RowVectorXd& f_window_start,
    int& num_windows,
    Eigen::MatrixXd& path_m_all,
    Eigen::RowVectorXd& max_phi_window_all
);

double prctile(const Eigen::MatrixXd& data, double p);

#endif // DETECT_LINE_SPECTRUM_FROM_LOFAR_CHANGE_H
