#include "detect_line_spectrum_from_lofar_change.h"

#ifndef M_PI
#define M_PI 4.0 * atan(1.0)
#endif

using namespace Eigen;
using namespace std;

double prctile(const MatrixXd& data, double p)
{
    if (data.size() == 0) return 0.0;
    VectorXd vec = data.reshaped();
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

    for (int idx_time = 0; idx_time < N; ++idx_time) {
        RowVectorXd x = X.row(idx_time);
        for (int k = 0; k < M; ++k) {
            int k_mat = k + 1;
            int left_start = k_mat - static_cast<int>(G);
            int left_end = k_mat - static_cast<int>(E);
            int right_start = k_mat + static_cast<int>(E);
            int right_end = k_mat + static_cast<int>(G);

            left_start = max(left_start, 1);
            left_end = max(left_end, 1);
            right_start = min(right_start, M);
            right_end = min(right_end, M);

            VectorXi R_idx;
            if (left_start >= left_end) {
                R_idx = VectorXi::LinSpaced(right_end - right_start + 1, right_start - 1, right_end - 1);
            } else if (right_start >= right_end) {
                R_idx = VectorXi::LinSpaced(left_end - left_start + 1, left_start - 1, left_end - 1);
            } else {
                VectorXi R1 = VectorXi::LinSpaced(left_end - left_start + 1, left_start - 1, left_end - 1);
                VectorXi R2 = VectorXi::LinSpaced(right_end - right_start + 1, right_start - 1, right_end - 1);
                R_idx.resize(R1.size() + R2.size());
                R_idx << R1, R2;
            }

            RowVectorXd x_R = x(R_idx);
            double X_bar_k = x_R.mean();

            RowVectorXd Phi_k = RowVectorXd::Zero(M);
            for (int k_prime = 0; k_prime < M; ++k_prime) {
                if (x(k_prime) < r * X_bar_k) {
                    Phi_k(k_prime) = x(k_prime);
                } else {
                    Phi_k(k_prime) = X_bar_k;
                }
            }

            RowVectorXd Phi_k_R = Phi_k(R_idx);
            double u_k = Phi_k_R.mean();

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
        int win_idx_mat = win_idx_cpp + 1;
        int win_start_idx_mat = win_idx_mat;
        int win_start_idx_cpp = win_start_idx_mat - 1;
        int win_end_idx_cpp = win_start_idx_cpp + win_freq_len - 1;
        MatrixXd window_data = Pxx_linear.block(win_start_idx_cpp, 0, win_freq_len, N_stft);

        for (int m_cpp = 0; m_cpp < win_freq_len; ++m_cpp) {
            double a = window_data(m_cpp, 0);
            double g = (a < thresh_break) ? 1.0 : 0.0;
            dp_state[m_cpp][0] = Vector4d(0.0, 0.0, g, 0.0);
        }

        for (int t_cpp = 1; t_cpp < N_stft; ++t_cpp) {
            int t_mat = t_cpp + 1;
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
                    double prev_prev_m_mat = dp_state[prev_m_cpp][t_cpp-1](3);

                    double curr_a = window_data(m_cpp, t_cpp);
                    double curr_g = (curr_a < thresh_break) ? 1.0 : 0.0;
                    double curr_A = prev_A + curr_a;
                    double curr_C = 0.0;

                    if (t_mat >= 3) {
                        int f_prev_prev_idx_mat = win_start_idx_mat + static_cast<int>(prev_prev_m_mat) - 1;
                        int f_prev_idx_mat = win_start_idx_mat + (prev_m_cpp + 1) - 1;
                        int f_curr_idx_mat = win_start_idx_mat + (m_cpp + 1) - 1;
                        double f_prev_prev = (static_cast<double>(f_prev_prev_idx_mat) - 1.0) * freq_res;
                        double f_prev = (static_cast<double>(f_prev_idx_mat) - 1.0) * freq_res;
                        double f_curr = (static_cast<double>(f_curr_idx_mat) - 1.0) * freq_res;
                        double d_prev = f_prev - f_prev_prev;
                        double d_curr = f_curr - f_prev;
                        curr_C = prev_C + fabs(d_prev - d_curr);
                    }

                    double curr_G = prev_G + curr_g;
                    double curr_phi = curr_A / (alpha * curr_G + beta * curr_C + gamma);
                    phi_candidates(p) = curr_phi;
                    acg_candidates.row(p) = Vector4d(curr_A, curr_C, curr_G, static_cast<double>(prev_m_cpp + 1));
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
        double m_prev_mat = dp_state[m_opt_cpp][N_stft-1](3);
        for (int t_cpp = N_stft-2; t_cpp >= 0; --t_cpp) {
            path_m_mat(t_cpp) = m_prev_mat;
            int m_prev_cpp = static_cast<int>(m_prev_mat) - 1;
            m_prev_mat = dp_state[m_prev_cpp][t_cpp](3);
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
    double gamma
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

    double thresh_break = prctile(Pxx_linear, 99);
    int win_freq_len = L;

    RowVectorXd phi_f, f_window_start, max_phi_window_all;
    int num_windows;
    MatrixXd path_m_all;
    calc_spectrum_feature(Pxx_linear, f_stft, N_stft, win_freq_len, thresh_break,
                          alpha, beta, gamma, fs,
                          phi_f, f_window_start, num_windows, path_m_all, max_phi_window_all);

    // 核心修复：完全复刻MATLAB 1基计数逻辑
    double d0 = prctile(phi_f.reshaped(), 99);
    counter = MatrixXi::Zero(M_stft, N_stft);
    const int counter_thresh = 3;

    for (int win_idx_mat = 1; win_idx_mat <= num_windows; ++win_idx_mat) {
        double max_phi_window = max_phi_window_all(win_idx_mat - 1);
        if (max_phi_window > d0) {
            RowVectorXd path_m_mat = path_m_all.row(win_idx_mat - 1);
            VectorXd global_freq_idx_mat = win_idx_mat + path_m_mat.array() - 1.0;
            global_freq_idx_mat = global_freq_idx_mat.cwiseMax(1.0).cwiseMin(static_cast<double>(M_stft));

            for (int t_mat = 1; t_mat <= N_stft; ++t_mat) {
                int freq_idx_cpp = static_cast<int>(global_freq_idx_mat(t_mat - 1)) - 1;
                int time_idx_cpp = t_mat - 1;
                freq_idx_cpp = max(0, min(M_stft - 1, freq_idx_cpp));
                time_idx_cpp = max(0, min(N_stft - 1, time_idx_cpp));
                counter(freq_idx_cpp, time_idx_cpp) += 1;
            }
        }
    }

    // 候选点提取：完全匹配MATLAB find(counter>3)
    vector<pair<int, int>> candidate_idx;
    for (int freq_idx_cpp = 0; freq_idx_cpp < M_stft; ++freq_idx_cpp) {
        for (int time_idx_cpp = 0; time_idx_cpp < N_stft; ++time_idx_cpp) {
            if (counter(freq_idx_cpp, time_idx_cpp) > counter_thresh) {
                candidate_idx.emplace_back(freq_idx_cpp + 1, time_idx_cpp + 1);
            }
        }
    }

    // 初始化空行向量
    line_spectrum_center_freq = RowVectorXd();
    if (candidate_idx.empty()) {
        cout << "[线谱检测] 未检测到计数超过" << counter_thresh << "的候选点" << endl;
        return;
    }

    // 候选点转换为物理频率
    double freq_res = (fs / 2.0) / (static_cast<double>(N_freq) - 1.0);
    VectorXd f_candidate(candidate_idx.size());
    for (int i = 0; i < candidate_idx.size(); ++i) {
        int freq_idx_mat = candidate_idx[i].first;
        f_candidate(i) = (static_cast<double>(freq_idx_mat) - 1.0) * freq_res;
    }

    // 频率分组
    sort(f_candidate.begin(), f_candidate.end());
    vector<vector<double>> groups;
    vector<double> current_group;
    if (f_candidate.size() > 0) {
        current_group.push_back(f_candidate(0));
    }
    double group_thresh = freq_res * 3.0;
    for (int i = 1; i < f_candidate.size(); ++i) {
        if (f_candidate(i) - f_candidate(i-1) < group_thresh) {
            current_group.push_back(f_candidate(i));
        } else {
            groups.push_back(current_group);
            current_group.clear();
            current_group.push_back(f_candidate(i));
        }
    }
    if (current_group.size() > 0) {
        groups.push_back(current_group);
    }

    // 核心修复：显式初始化为行向量
    line_spectrum_center_freq = RowVectorXd::Zero(groups.size());
    for (int g = 0; g < groups.size(); ++g) {
        double group_mean = 0.0;
        for (double freq : groups[g]) {
            group_mean += freq;
        }
        group_mean /= groups[g].size();
        line_spectrum_center_freq(g) = group_mean;
    }
    sort(line_spectrum_center_freq.data(), line_spectrum_center_freq.data() + line_spectrum_center_freq.size());

//    // 调试输出
//    cout << "==================================== 线谱检测结果 ====================================" << endl;
//    cout << "LOFAR矩阵维度：" << M_time << "（时间）×" << N_freq << "（频率）" << endl;
//    cout << "检测到线谱数量：" << line_spectrum_center_freq.size() << " 个" << endl;
//    cout << "物理频率分辨率：" << fixed << setprecision(6) << freq_res << " Hz" << endl;
//    cout << "计数阈值：" << counter_thresh << "，分组阈值：" << fixed << setprecision(6) << group_thresh << " Hz" << endl;
//    cout << "线谱中心频率（Hz）：";
//    for (int g = 0; g < line_spectrum_center_freq.size(); ++g) {
//        cout << fixed << setprecision(3) << line_spectrum_center_freq(g) << "  ";
//    }
//    cout << endl << "=====================================================================================" << endl;
}
