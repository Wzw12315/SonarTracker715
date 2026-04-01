#include "RawReader.h"
#include <fstream>
#include <stdexcept>
#include <vector>
#include <complex>

Eigen::MatrixXd RawReader::read_raw_file(const std::string& filename, int rows, int cols) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open raw file: " + filename);
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    size_t num_elements = rows * cols;
    Eigen::MatrixXd mat(rows, cols);

    if (file_size >= num_elements * sizeof(std::complex<double>)) {
        // 1. 双精度复数 (16 bytes)
        std::vector<std::complex<double>> buffer(num_elements);
        file.read(reinterpret_cast<char*>(buffer.data()), num_elements * sizeof(std::complex<double>));
        Eigen::Map<Eigen::MatrixXcd> cmat(buffer.data(), rows, cols);
        mat = cmat.real();

    } else if (file_size >= num_elements * sizeof(double)) {
        // 2. 双精度实数 (8 bytes)
        std::vector<double> buffer(num_elements);
        file.read(reinterpret_cast<char*>(buffer.data()), num_elements * sizeof(double));
        Eigen::Map<Eigen::MatrixXd> dmat(buffer.data(), rows, cols);
        mat = dmat;

    } else if (file_size >= num_elements * sizeof(float)) {
        // 3. 【新增】：单精度实数 (4 bytes) - 完美对接 MATLAB 的 fwrite(fid, data, 'single')
        std::vector<float> buffer(num_elements);
        file.read(reinterpret_cast<char*>(buffer.data()), num_elements * sizeof(float));
        Eigen::Map<Eigen::MatrixXf> fmat(buffer.data(), rows, cols);
        mat = fmat.cast<double>(); // 自动转换为 double 类型供后续高精度处理

    } else {
        throw std::runtime_error("Raw file size is too small for the requested dimensions: " + filename);
    }

    return mat;
}
