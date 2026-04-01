#include "fir2.h"
#include <cstring>
#include <fstream>

// ====================================================================
// PCHIP 样条插值实现部分
// ====================================================================
#define PCHIP_ABS(a) ((a) > 0 ? (a) : -(a))

static double exteriorSlope(double d1, double d2, double h1, double h2) {
    double s = ((2.0 * h1 + h2) * d1 - h1 * d2) / (h1 + h2);
    int sign_d1 = (d1 > 0) ? 1 : (d1 < 0) ? -1 : 0;
    int sign_s = (s > 0) ? 1 : (s < 0) ? -1 : 0;

    if (sign_s != sign_d1) s = 0.0;
    else {
        int sign_d2 = (d2 > 0) ? 1 : (d2 < 0) ? -1 : 0;
        if (sign_d1 != sign_d2 && PCHIP_ABS(s) > 3.0 * PCHIP_ABS(d1))
            s = 3.0 * d1;
    }
    return s;
}

void pchip(const double *x, const double *y, int x_len, const double *new_x, int new_x_len, double *new_y) {
    if (x_len < 2 || new_x_len < 1) return;

    double* del = new double[x_len - 1];
    double* h = new double[x_len - 1];
    double* slopes = new double[x_len];

    for (int i = 0; i < x_len - 1; ++i) {
        h[i] = x[i+1] - x[i];
        del[i] = (y[i+1] - y[i]) / h[i];
    }

    slopes[0] = exteriorSlope(del[0], del[1], h[0], h[1]);
    slopes[x_len-1] = exteriorSlope(del[x_len-2], del[x_len-3], h[x_len-2], h[x_len-3]);

    for (int i = 1; i < x_len - 1; ++i) {
        if (del[i-1] * del[i] <= 0) slopes[i] = 0;
        else {
            double w1 = h[i], w2 = h[i-1];
            slopes[i] = 3 * (w1 + w2) / ( (w1 + 2*w2)/del[i-1] + (2*w1 + w2)/del[i] );
        }
    }

    double* c1 = new double[x_len-1];
    double* c2 = new double[x_len-1];
    double* c3 = new double[x_len-1];

    for (int i = 0; i < x_len - 1; ++i) {
        double hh = h[i];
        double s = (y[i+1] - y[i])/hh;
        c1[i] = slopes[i];
        c2[i] = 3*s - 2*slopes[i] - slopes[i+1];
        c3[i] = 2*(slopes[i] + slopes[i+1] - 2*s)/hh;
    }

    for (int i = 0; i < new_x_len; ++i) {
        double xq = new_x[i];
        int idx = 0;
        if (xq >= x[x_len-1]) idx = x_len-2;
        else if (xq <= x[0]) idx = 0;
        else {
            int low = 0, high = x_len-1;
            while (high - low > 1) {
                int mid = (low + high)/2;
                if (xq < x[mid]) high = mid;
                else low = mid;
            }
            idx = low;
        }
        double dx = xq - x[idx];
        new_y[i] = y[idx] + dx*(c1[idx] + dx*(c2[idx] + dx*c3[idx]));
    }
    delete[] del; delete[] h; delete[] slopes; delete[] c1; delete[] c2; delete[] c3;
}

void pchip_new(const double* x, const double* y, int x_len, double new_x, double* new_y) {
    double* new_x_arr = &new_x;
    pchip(x, y, x_len, new_x_arr, 1, new_y);
}

double Boundary_point0(double h0, double h1, double delta0, double delta1) { return exteriorSlope(delta0, delta1, h0, h1); }
double Boundary_pointn(double hn_1, double hn_2, double deltan_1, double deltan_2) { return exteriorSlope(deltan_1, deltan_2, hn_1, hn_2); }

// ====================================================================
// FIR 滤波器设计与实现部分
// ====================================================================
double sinc(double x) {
    if (std::abs(x) < 1e-12) return 1.0;
    double pi_x = PI * x;
    return std::sin(pi_x) / pi_x;
}

int FiltCoefCmp(stFiltCoefPara *para, stFiltCoefRtn *rtn) {
    int cycle = 5;
    Eigen::VectorXd filter_inputFreq(para->freqNum);
    Eigen::VectorXd filter_inputAmp(para->freqNum);
    for (int i = 0; i < para->freqNum; ++i) {
        filter_inputFreq(i) = para->freq(i);
        filter_inputAmp(i) = para->level(i);
    }
    Eigen::MatrixXd cn(cycle, para->freqNum);
    cn.row(0).setOnes();
    SinGroupPara para_sin; SinGroupRtn rtn_sin;
    para_sin.freqNum = filter_inputFreq.size();
    para_sin.fs = para->fs;
    para_sin.num_data = 1000;
    para_sin.freq = filter_inputFreq;
    para_sin.level = filter_inputAmp;

    Eigen::MatrixXd W(para_sin.num_data + 1, para->nFilter); W.setZero();
    Eigen::VectorXd xn(para->nFilter);
    Eigen::VectorXd yn(para_sin.num_data);
    Eigen::VectorXd pchipoutputx = filter_inputFreq;
    Eigen::MatrixXd output_fft;

    double temp, mu, en, y;
    for (int i = 0; i < cycle; ++i) {
        para_sin.c = cn.row(i);
        SinGroup(&para_sin, &rtn_sin);
        if (i == 0) { temp = rtn_sin.data_xn.squaredNorm(); mu = 0.5 / temp; }
        W.row(para_sin.num_data).setZero();
        yn.head(para->nFilter).setZero();

        for (int j = para->nFilter - 1; j < para_sin.num_data; ++j) {
            xn = rtn_sin.data_xn.segment(j - para->nFilter + 1, para->nFilter).reverse();
            y = W.row(j).dot(xn);
            en = rtn_sin.data_dn(j) - y;
            W.row(j + 1) = W.row(j) + 2 * mu * en * xn;
        }

        if (i != cycle - 1) {
            for (int j = para->nFilter - 1; j < para_sin.num_data; ++j) {
                xn = rtn_sin.data_xn.segment(j - para->nFilter + 1, para->nFilter).reverse();
                yn(j) = W.row(para_sin.num_data).dot(xn);
            }
            output_fft = FFT(yn.data(), yn.size(), para->fs, para->fs);
            Eigen::VectorXd pchipoutputy(pchipoutputx.size());
            pchip(output_fft.row(0).data(), output_fft.row(1).data(), output_fft.cols(), pchipoutputx.data(), pchipoutputx.size(), pchipoutputy.data());

            double km = 10;
            for (int var = 0; var < pchipoutputy.size(); ++var) {
                cn(i + 1, var) = cn(i, var) + km * std::abs(filter_inputAmp(var) - pchipoutputy(var)) / filter_inputAmp(var);
            }
            double input_xn = cn.row(i).squaredNorm();
            double output_xn = cn.row(i + 1).squaredNorm();
            mu = (input_xn / output_xn) * mu;
        }
    }
    rtn->filtCoef_num = para->nFilter;
    for (int var = 0; var < rtn->filtCoef_num; ++var) rtn->filtCoef[var] = W(para_sin.num_data, var);
    return 0;
}

int SinGroup(SinGroupPara *para, SinGroupRtn *rtn) {
    rtn->data_xn.resize(para->num_data); rtn->data_dn.resize(para->num_data);
    rtn->data_xn.setZero(); rtn->data_dn.setZero();
    for (int i = 0; i < para->num_data; ++i) {
        for (int j = 0; j < para->freqNum; ++j) {
            double phase = 2 * PI * para->freq(j) * i / para->fs;
            rtn->data_xn(i) += para->c(j) * std::sin(phase);
            rtn->data_dn(i) += para->level(j) * para->c(j) * std::sin(phase);
        }
    }
    return 0;
}

Eigen::VectorXd generateGaussNoise(int size, double mean, double stdDev) {
    std::random_device rd; std::mt19937 gen(rd()); std::normal_distribution<> dis(mean, stdDev);
    Eigen::VectorXd noise(size);
    for (int i = 0; i < size; ++i) noise(i) = dis(gen);
    return noise;
}

Eigen::MatrixXd FFT(double *in, int length, int fs, int fft_num) {
    int fft_size = std::max(length, fft_num);
    double *fft_in = new double[fft_size]();
    std::memcpy(fft_in, in, length * sizeof(double));
    fftw_complex *out1 = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (fft_size / 2 + 1));
    fftw_plan s = fftw_plan_dft_r2c_1d(fft_size, fft_in, out1, FFTW_ESTIMATE);
    fftw_execute(s);
    int NN = fft_size / 2 + 1;
    Eigen::MatrixXd data(2, NN);
    for (int i = 0; i < NN; ++i) {
        double mag = std::sqrt(out1[i][0] * out1[i][0] + out1[i][1] * out1[i][1]);
        if (i == 0 || i == NN - 1) data(1, i) = mag / length;
        else data(1, i) = 2 * mag / length;
        data(0, i) = i * (double)fs / fft_size;
    }
    fftw_destroy_plan(s); fftw_free(out1); delete[] fft_in;
    return data;
}

int Window(WindowPara *para, WindowRtn *rtn) {
    int win_len = para->n;
    rtn->h.resize(win_len);
    double L_minus_1 = win_len - 1.0;
    switch (para->type) {
    case Rectangle: rtn->h.setOnes(); break;
    case Triangle:
        for (int n = 0; n < win_len; ++n) {
            double t = 2.0 * std::abs(n - L_minus_1 / 2.0) / L_minus_1;
            rtn->h(n) = 1.0 - t;
        }
        break;
    case Hanning:
        for (int n = 0; n < win_len; ++n) {
            double theta = 2 * PI * n / L_minus_1;
            rtn->h(n) = 0.5 * (1.0 - std::cos(theta));
        }
        break;
    case Hamming: {
        int N = win_len;
        if (N == 1) { rtn->h(0) = 1.0; break; }
        double L_m_1 = N - 1.0;
        for (int n = 0; n < N; ++n) {
            double theta = 2 * PI * n / L_m_1;
            rtn->h(n) = 0.54 - 0.46 * std::cos(theta);
        }
        break;
    }
    case Blackman:
        for (int n = 0; n < win_len; ++n) {
            double theta1 = 2 * PI * n / L_minus_1;
            double theta2 = 4 * PI * n / L_minus_1;
            rtn->h(n) = 0.42 - 0.5 * std::cos(theta1) + 0.08 * std::cos(theta2);
        }
        break;
    }
    return 0;
}

int FirWin(FirWinPara *para, FirWinRtn *rtn) {
    int filter_order = para->n;
    int filter_len = filter_order + 1;
    double delay = filter_order / 2.0;
    Eigen::VectorXd temp(filter_len);
    switch (para->band) {
    case LOWPASSFILTER: {
        double Wp = para->fln / (para->fs / 2.0);
        for (int i = 0; i < filter_len; ++i) {
            double t = i - delay;
            temp(i) = Wp * sinc(Wp * t);
        }
        break;
    }
    case HIGHPASSFILTER: {
        double Wp = para->fln / (para->fs / 2.0);
        for (int i = 0; i < filter_len; ++i) {
            double t = i - delay;
            temp(i) = sinc(t) - Wp * sinc(Wp * t);
        }
        break;
    }
    case BANDPASSFILTER: {
        double Wp1 = para->fln / (para->fs / 2.0);
        double Wp2 = para->fhn / (para->fs / 2.0);
        for (int i = 0; i < filter_len; ++i) {
            double t = i - delay;
            temp(i) = Wp2 * sinc(Wp2 * t) - Wp1 * sinc(Wp1 * t);
        }
        break;
    }
    case BANDSTOPFILTER: {
        double Wp1 = para->fln / (para->fs / 2.0);
        double Wp2 = para->fhn / (para->fs / 2.0);
        for (int i = 0; i < filter_len; ++i) {
            double t = i - delay;
            temp(i) = sinc(t) - (Wp2 * sinc(Wp2 * t) - Wp1 * sinc(Wp1 * t));
        }
        break;
    }
    }
    WindowPara win_para; WindowRtn win_rtn;
    win_para.n = filter_len;
    win_para.type = para->type;
    Window(&win_para, &win_rtn);
    rtn->h = temp.cwiseProduct(win_rtn.h);
    return 0;
}

FIR::FIR(double *coefficients, unsigned number_of_taps) : coefficients(coefficients),
    buffer(new double[number_of_taps]()),
    taps(number_of_taps),
    offset(0)
{
}
FIR::~FIR() { delete[] buffer; }
double FIR::filter(double input) {
    buffer[offset] = input;
    double output = 0;
    int idx = offset;
    for (unsigned i = 0; i < taps; ++i) {
        output += coefficients[i] * buffer[idx];
        idx = (idx == 0) ? taps - 1 : idx - 1;
    }
    offset = (offset + 1) % taps;
    return output;
}
void FIR::reset() {
    std::memset(buffer, 0, sizeof(double) * taps);
    offset = 0;
}
