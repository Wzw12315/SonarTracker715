#pragma once
#include <Eigen/Dense>
#include <string>

class RawReader {
public:
    /**
     * @brief 读取 .raw 文件，并等效执行 MATLAB 的 real(read_raw_file(...))
     * @param filename raw 文件绝对路径
     * @param rows 矩阵行数 (对应 MATLAB 的 data_N, 例如 512)
     * @param cols 矩阵列数 (对应 MATLAB 的 data_M, 例如 15000)
     * @return 返回纯实部的 Eigen::MatrixXd 矩阵
     */
    static Eigen::MatrixXd read_raw_file(const std::string& filename, int rows, int cols);
};
