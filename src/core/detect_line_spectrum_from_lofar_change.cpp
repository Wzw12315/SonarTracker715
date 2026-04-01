#include "detect_line_spectrum_from_lofar_change.h"

#ifndef M_PI
#define M_PI 4.0 * atan(1.0)
#endif

using namespace Eigen;
using namespace std;

double prctile(const MatrixXd& data, double p)
{
    if (data.size() == 0) return 0.0;
    VectorXd vec(data.size());
    int idx = 0;
    for (int c = 0; c < data.cols(); ++c) {
        for (int r = 0; r < data.rows(); ++r) {
            vec(idx++) = data(r, c);
        }
    }
    sort(vec.begin(), vec.end());
    int n = vec.size();
    double pos = (p / 100.0) * (static_cast<double>(n) - 1.0);
    int idx_floor = static_cast<int>(floor(pos));
    int idx_ceil = static_cast<int>(ceil(pos));
    double frac = pos - static_cast<double>(idx_floor);

    if (idx_floor >= n - 1) return vec(n - 1);
    if (idx_ceil <= 0) return vec(0);
    return vec(idx_floor) + frac * (vec(idx_ceil) - vec(idx_floor));
}

MatrixXd tpsw_normalization(const MatrixXd& X, double G, double E, double C)
{
    int N = X.rows();
    int M = X.cols();
    MatrixXd Z_TPSW = MatrixXd::Zero(N, M);
    double A = G - E + 1.0;
    double r = 1.0 + C * sqrt((4.0 / M_PI - 1.0) / A);

    int g_int = static_cast<int>(G);
    int e_int = static_cast<int>(E);

    for (int idx_time = 0; idx_time < N; ++idx_time) {
        RowVectorXd x = X.row(idx_time);
        for (int k = 0; k < M; ++k) {
            int left_start = max(0, k - g_int);
            int left_end   = max(0, k - e_int);
            int right_start = min(M - 1, k + e_int);
            int right_end   = min(M - 1, k + g_int);

            std::vector<int> R_idx;
            if (left_start >= left_end) {
                for (int i = right_start; i <= right_end; ++i) R_idx.push_back(i);
            } else if (right_start >= right_end) {
                for (int i = left_start; i <= left_end; ++i) R_idx.push_back(i);
            } else {
                for (int i = left_start; i <= left_end; ++i) R_idx.push_back(i);
                for (int i = right_start; i <= right_end; ++i) R_idx.push_back(i);
            }

            if (R_idx.empty()) {
                Z_TPSW(idx_time, k) = 1.0;
                continue;
            }

            double sum_R = 0;
            for (int idx : R_idx) sum_R += x(idx);
            double X_bar_k = sum_R / R_idx.size();

            double sum_Phi = 0;
            for (int idx : R_idx) {
                if (x(idx) < r * X_bar_k) sum_Phi += x(idx);
                else sum_Phi += X_bar_k;
            }
            double u_k = sum_Phi / R_idx.size();

            Z_TPSW(idx_time, k) = x(k) / (u_k + EPS);
        }
    }
    return Z_TPSW;
}

void calc_spectrum_feature(
    const MatrixXd& Pxx_linear,
    const RowVectorXd& f_stft,
    int N_stft,
    int win_freq_len,
    double thresh_break,
    double alpha,
    double beta,
    double gamma,
    double fs,
    RowVectorXd& phi_f,
    RowVectorXd& f_window_start,
    int& num_windows,
    MatrixXd& path_m_all,
    RowVectorXd& max_phi_window_all
)
{
    int M_stft = Pxx_linear.rows();
    num_windows = M_stft - win_freq_len + 1;
    phi_f = RowVectorXd::Zero(num_windows);
    f_window_start = f_stft.head(num_windows);
    path_m_all = MatrixXd::Zero(num_windows, N_stft);
    max_phi_window_all = RowVectorXd::Zero(num_windows);

    vector<vector<Vector4d>> dp_state(win_freq_len, vector<Vector4d>(N_stft, Vector4d::Zero()));
    double freq_res = (fs / 2.0) / (static_cast<double>(f_stft.size()) - 1.0);

    for (int win_idx_cpp = 0; win_idx_cpp < num_windows; ++win_idx_cpp) {
        int win_start_idx_cpp = win_idx_cpp;
        MatrixXd window_data = Pxx_linear.block(win_start_idx_cpp, 0, win_freq_len, N_stft);

        for (int m_cpp = 0; m_cpp < win_freq_len; ++m_cpp) {
            double a = window_data(m_cpp, 0);
            double g = (a < thresh_break) ? 1.0 : 0.0;
            dp_state[m_cpp][0] = Vector4d(0.0, 0.0, g, 0.0);
        }

        for (int t_cpp = 1; t_cpp < N_stft; ++t_cpp) {
            for (int m_cpp = 0; m_cpp < win_freq_len; ++m_cpp) {
                int m_min_cpp = max(0, m_cpp - 1);
                int m_max_cpp = min(win_freq_len - 1, m_cpp + 1);
                int num_prev = m_max_cpp - m_min_cpp + 1;

                VectorXd phi_candidates = VectorXd::Zero(num_prev);
                MatrixXd acg_candidates = MatrixXd::Zero(num_prev, 4);

                for (int p = 0; p < num_prev; ++p) {
                    int prev_m_cpp = m_min_cpp + p;
                    double prev_A = dp_state[prev_m_cpp][t_cpp-1](0);
                    double prev_C = dp_state[prev_m_cpp][t_cpp-1](1);
                    double prev_G = dp_state[prev_m_cpp][t_cpp-1](2);
                    double prev_prev_m_cpp = dp_state[prev_m_cpp][t_cpp-1](3);

                    double curr_a = window_data(m_cpp, t_cpp);
                    double curr_g = (curr_a < thresh_break) ? 1.0 : 0.0;
                    double curr_A = prev_A + curr_a;
                    double curr_C = 0.0;

                    if (t_cpp >= 2) {
                        int f_prev_prev_idx_cpp = win_start_idx_cpp + static_cast<int>(prev_prev_m_cpp);
                        int f_prev_idx_cpp = win_start_idx_cpp + prev_m_cpp;
                        int f_curr_idx_cpp = win_start_idx_cpp + m_cpp;

                        double f_prev_prev = static_cast<double>(f_prev_prev_idx_cpp) * freq_res;
                        double f_prev = static_cast<double>(f_prev_idx_cpp) * freq_res;
                        double f_curr = static_cast<double>(f_curr_idx_cpp) * freq_res;

                        double d_prev = f_prev - f_prev_prev;
                        double d_curr = f_curr - f_prev;
                        curr_C = prev_C + fabs(d_prev - d_curr);
                    }

                    double curr_G = prev_G + curr_g;
                    double curr_phi = curr_A / (alpha * curr_G + beta * curr_C + gamma);
                    phi_candidates(p) = curr_phi;
                    acg_candidates.row(p) = Vector4d(curr_A, curr_C, curr_G, static_cast<double>(prev_m_cpp));
                }

                int max_p_idx;
                phi_candidates.maxCoeff(&max_p_idx);
                dp_state[m_cpp][t_cpp] = acg_candidates.row(max_p_idx);
            }
        }

        double max_phi_window = 0.0;
        int m_opt_cpp = 0;
        for (int m_cpp = 0; m_cpp < win_freq_len; ++m_cpp) {
            double final_A = dp_state[m_cpp][N_stft-1](0);
            double final_C = dp_state[m_cpp][N_stft-1](1);
            double final_G = dp_state[m_cpp][N_stft-1](2);
            double final_phi = final_A / (alpha * final_G + beta * final_C + gamma);
            if (final_phi > max_phi_window) {
                max_phi_window = final_phi;
                m_opt_cpp = m_cpp;
            }
        }
        phi_f(win_idx_cpp) = max_phi_window;
        max_phi_window_all(win_idx_cpp) = max_phi_window;

        RowVectorXd path_m_mat(N_stft);
        path_m_mat(N_stft-1) = static_cast<double>(m_opt_cpp + 1);
        double m_prev_cpp = dp_state[m_opt_cpp][N_stft-1](3);
        for (int t_cpp = N_stft-2; t_cpp >= 0; --t_cpp) {
            path_m_mat(t_cpp) = m_prev_cpp + 1.0;
            m_prev_cpp = dp_state[static_cast<int>(m_prev_cpp)][t_cpp](3);
        }
        path_m_all.row(win_idx_cpp) = path_m_mat;
    }
}

void detect_line_spectrum_from_lofar_change(
    const MatrixXd& lofar_mat,
    double fs,
    int NFFT,
    RowVectorXd& line_spectrum_center_freq,
    MatrixXd& Z_TPSW,
    MatrixXi& counter,
    RowVectorXd& f_stft,
    RowVectorXd& t_stft,
    double G,
    double E,
    double C,
    int L,
    double alpha,
    double beta,
    double gamma,
    double prctile_thresh,
    double peak_std_mult
)
{
    int M_time = lofar_mat.rows();
    int N_freq = lofar_mat.cols();

    f_stft = RowVectorXd::LinSpaced(N_freq, 1, N_freq);
    t_stft = RowVectorXd::LinSpaced(M_time, 1, M_time);
    MatrixXd PX = lofar_mat;

    Z_TPSW = tpsw_normalization(PX, G, E, C);
    MatrixXd Pxx_linear = Z_TPSW.transpose();
    int M_stft = Pxx_linear.rows();
    int N_stft = Pxx_linear.cols();

    double prctile_val = prctile(Pxx_linear, prctile_thresh);
    // 【强制底线】：即便 UI 乱传 80%，也要保证背景起伏不被误认为免罚分的信号
    double thresh_break = std::max(prctile_val, 1.8);
    int win_freq_len = L;

    RowVectorXd phi_f, f_window_start, max_phi_window_all;
    int num_windows;
    MatrixXd path_m_all;

    calc_spectrum_feature(Pxx_linear, f_stft, N_stft, win_freq_len, thresh_break,
                          alpha, beta, gamma, fs,
                          phi_f, f_window_start, num_windows, path_m_all, max_phi_window_all);

    double mean_phi = phi_f.mean();
    double std_phi = std::sqrt((phi_f.array() - mean_phi).square().sum() / (phi_f.size() - 1.0));
    double thresh_phi = mean_phi + peak_std_mult * std_phi;

    std::vector<std::pair<double, int>> all_peaks;
        for (int i = 1; i < phi_f.size() - 1; ++i) {
            // 【核心修复】：右侧条件改为 >=，允许进入平顶检测逻辑
            if (phi_f(i) > phi_f(i - 1) && phi_f(i) >= phi_f(i + 1) && phi_f(i) > thresh_phi) {
                int j = i;
                // 顺藤摸瓜，寻找整个平顶的右侧边界 (容忍微小的浮点数误差)
                while (j < phi_f.size() - 1 && std::abs(phi_f(j) - phi_f(j + 1)) < 1e-9) {
                    j++;
                }
                // 只有当平顶的右侧发生真实的物理下降时，才认定这是一个合法的峰
                // (防止把爬坡的阶梯当成峰)
                if (j == phi_f.size() - 1 || phi_f(j) > phi_f(j + 1)) {
                    int center = (i + j) / 2; // 取平顶的正中心作为目标的精确频率
                    all_peaks.push_back({phi_f(center), center});
                }
                i = j; // 直接跳过这个已经处理完的平顶，继续往后找
            }
        }

    std::sort(all_peaks.begin(), all_peaks.end(), [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
        return a.first > b.first;
    });

    std::vector<int> valid_win_idx;
    int minPeakDist = 18; // 加大抑制距离，防止宽带噪声爆出密集假红线
    for (const auto& p : all_peaks) {
        bool ok = true;
        for (int vp : valid_win_idx) {
            if (std::abs(p.second - vp) < minPeakDist) { ok = false; break; }
        }
        if (ok) valid_win_idx.push_back(p.second);
    }
    std::sort(valid_win_idx.begin(), valid_win_idx.end());

    counter = MatrixXi::Zero(M_stft, N_stft);

    // ==========================================================
    // 【核心修复】：彻底移除中值滤波的错误偏移，恢复直接路径映射。
    // 展宽至 ±3 (7像素宽度)，绝对防止 QCustomPlot 大跨度降采样吃掉线条
    // ==========================================================
    for (int win_idx_cpp : valid_win_idx) {
        RowVectorXd path_m_mat = path_m_all.row(win_idx_cpp);
        for (int t_cpp = 0; t_cpp < N_stft; ++t_cpp) {
            int global_freq_idx_cpp = win_idx_cpp + static_cast<int>(path_m_mat(t_cpp)) - 1;
            for (int w = -3; w <= 3; ++w) {
                int safe_idx = std::max(0, std::min(M_stft - 1, global_freq_idx_cpp + w));
                counter(safe_idx, t_cpp) = 1;
            }
        }
    }

    std::vector<std::pair<int, int>> candidate_idx;
    for (int freq_idx_cpp = 0; freq_idx_cpp < M_stft; ++freq_idx_cpp) {
        for (int time_idx_cpp = 0; time_idx_cpp < N_stft; ++time_idx_cpp) {
            if (counter(freq_idx_cpp, time_idx_cpp) > 0) {
                candidate_idx.emplace_back(freq_idx_cpp, time_idx_cpp);
            }
        }
    }

    line_spectrum_center_freq = RowVectorXd();
    if (candidate_idx.empty()) return;

    double freq_res = (fs / 2.0) / (static_cast<double>(N_freq) - 1.0);
    VectorXd f_candidate(candidate_idx.size());
    for (size_t i = 0; i < candidate_idx.size(); ++i) {
        f_candidate(i) = candidate_idx[i].first * freq_res;
    }

    std::vector<double> valid_lines;
    std::vector<double> current_group;
    double current_val = f_candidate(0);
    current_group.push_back(current_val);

    for (size_t i = 1; i < f_candidate.size(); ++i) {
        if (f_candidate(i) - current_val < 3.0) {
            current_group.push_back(f_candidate(i));
        } else {
            std::sort(current_group.begin(), current_group.end());
            double median_val = current_group[current_group.size() / 2];
            valid_lines.push_back(median_val);
            current_group.clear();
            current_group.push_back(f_candidate(i));
        }
        current_val = f_candidate(i);
    }
    if (!current_group.empty()) {
        std::sort(current_group.begin(), current_group.end());
        double median_val = current_group[current_group.size() / 2];
        valid_lines.push_back(median_val);
    }

    line_spectrum_center_freq.resize(valid_lines.size());
    for (size_t i = 0; i < valid_lines.size(); ++i) {
        line_spectrum_center_freq(i) = valid_lines[i];
    }
}
