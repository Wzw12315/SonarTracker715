#pragma once
#include <Eigen/Dense>

// 精简为与 Matlab 完全对应的 1D RL 解卷积
Eigen::VectorXd RL_1D(const Eigen::VectorXd& P, const Eigen::VectorXd& PSF, int iterations);
