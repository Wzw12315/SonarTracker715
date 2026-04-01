#pragma once
#include <Eigen/Dense>
#include <vector>
#include <complex>
#include <fftw3.h>

// 将宽带波束形成的结果打包，方便在 Worker 中提取线谱和绘制空间谱
struct CBFResult {
    Eigen::VectorXd P_cbf_spatial;     // 1D 常规波束形成空间谱 (长度 257)
    Eigen::MatrixXd P_out;             // 2D LOFAR 方位-频率谱 (257 x f_num)
    Eigen::MatrixXcd signal_fft_lofar; // M x f_num 的复数频域数据 (用于直接送给DCV处理)
    Eigen::MatrixXcd signal_fft_demon; // M x demon_f_num 的复数频域数据 (用于后续DEMON解调)
};

class CBFProcessor {
public:
    // 构造函数：一次性预计算所有的时延矩阵和导向矢量频率
    CBFProcessor(int M, double d, double c, double r_scan, int fs, int NFFT_R, int NFFT_WIN,
                 const std::vector<double>& f_band_lofar, const std::vector<double>& f_band_demon);
    ~CBFProcessor();

    // 核心处理函数：传入滑动窗数据，返回所有谱
    CBFResult process(const Eigen::MatrixXd& signal_w);

    // 获取预计算的坐标轴系 (用于绘图和反卷积)
    Eigen::VectorXd getThetaScan() const { return theta_scan; }
    Eigen::VectorXd getFLofar() const { return f_lofar; }
    Eigen::VectorXd getFDemon() const { return f_demon; }
    Eigen::VectorXd getXv() const { return m_xv; } // <--- 【就是这里！必须要有这个函数】
    Eigen::MatrixXd getTauMatrix() const { return tau_matrix; }
private:
    int m_M, m_fs, m_NFFT_R, m_NFFT_WIN;
    double m_df;

    Eigen::VectorXd theta_scan;
    Eigen::VectorXd f_lofar;
    Eigen::VectorXd f_demon;
    Eigen::VectorXd m_xv; // <--- 【还有这里：保存阵元坐标的成员变量】

    std::vector<int> f_n_lofar; // lofar频点索引
    std::vector<int> f_n_demon; // demon频点索引

    Eigen::MatrixXd tau_matrix; // 时延矩阵: theta_scan_length x M

    // FFTW 资源
    double* fft_in;
    fftw_complex* fft_out;
    fftw_plan fft_plan;
};
