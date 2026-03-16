#ifndef FIR2_H
#define FIR2_H

#include <iostream>
#include <cmath>
#include "fftw3.h"
#include <Eigen/Dense>
#include <chrono>
#include <random>
#include <vector>

#define PI  4.0 * atan(1.0)

#define Rectangle  1
#define Triangle   2
#define Hanning    3
#define Hamming    4
#define Blackman   5

#define LOWPASSFILTER     1
#define HIGHPASSFILTER    2
#define BANDPASSFILTER    3
#define BANDSTOPFILTER    4

struct stFiltCoefPara {
    int freqNum;
    Eigen::VectorXd freq;
    Eigen::VectorXd level;
    float fs;
    int nFilter;
};

struct stFiltCoefRtn {
    int filtCoef_num;
    float filtCoef[1000];
};

struct SinGroupPara {
    int freqNum;
    Eigen::VectorXd freq;
    Eigen::VectorXd level;
    float fs;
    int num_data;
    Eigen::VectorXd c;
};

struct SinGroupRtn {
    Eigen::VectorXd data_xn;
    Eigen::VectorXd data_dn;
};

struct FirWinPara {
    int n;          // 滤波器阶数（对应MATLAB fir1的N）
    int band;
    int type;
    double fln;
    double fhn;
    double fs;
};

struct FirWinRtn {
    Eigen::VectorXd h;
};

struct WindowPara {
    int type;
    int n;          // 窗长度（=滤波器阶数+1）
};

struct WindowRtn {
    Eigen::VectorXd h;
};

// ==================== FIR 相关函数 ====================
int FiltCoefCmp(stFiltCoefPara* para, stFiltCoefRtn* rtn);
int SinGroup(SinGroupPara* para, SinGroupRtn* rtn);
Eigen::VectorXd generateGaussNoise(int size, double mean, double stdDev);
Eigen::MatrixXd FFT(double *in, int length, int fs, int fft_num);

int FirWin(FirWinPara* para, FirWinRtn* rtn);
int Window(WindowPara* para, WindowRtn* rtn);

class FIR {
public:
    FIR(double *coefficients, unsigned number_of_taps);
    ~FIR();
    double filter(double input);
    void reset();
    unsigned getTaps() {return taps;};

private:
    double *coefficients;
    double *buffer;
    unsigned taps, offset;
};

// ==================== PCHIP 样条插值相关函数 ====================
void pchip(const double *x, const double *y, int x_len, const double *new_x, int new_x_len, double *new_y);
void pchip_new(const double* x, const double* y, int x_len, double new_x, double* new_y);
double Boundary_point0(double h0, double h1, double delta0, double delta1);
double Boundary_pointn(double hn_1, double hn_2, double deltan_1, double deltan_2);

#endif // FIR2_H
