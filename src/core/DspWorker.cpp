#include "DspWorker.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QMap>
#include <QElapsedTimer>
#include "RawReader.h"
#include "CBFProcessor.h"
#include "Deconvolution.h"
#include "TrackManager.h"
#include "fir2.h"
#include <fftw3.h>
#include <cmath>

static double calculateMedian(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

static std::vector<int> findPeaks(const Eigen::VectorXd& data, int minPeakDistance) {
    std::vector<std::pair<double, int>> all_peaks;
    for (int i = 1; i < data.size() - 1; ++i) {
        if (data[i] > data[i - 1] && data[i] > data[i + 1] && data[i] > 0) {
            all_peaks.push_back({data[i], i});
        }
    }
    std::sort(all_peaks.begin(), all_peaks.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<int> valid_peaks;
    for (const auto& p : all_peaks) {
        bool ok = true;
        for (int vp : valid_peaks) { if (std::abs(p.second - vp) < minPeakDistance) { ok = false; break; } }
        if (ok) valid_peaks.push_back(p.second);
    }
    std::sort(valid_peaks.begin(), valid_peaks.end());
    return valid_peaks;
}

static Eigen::VectorXd medfilt1(const Eigen::VectorXd& x, int w) {
    Eigen::VectorXd res(x.size());
    int half_w = w / 2;

    std::vector<double> window;
    window.reserve(w + 1);

    const double* x_data = x.data();
    for (int i = 0; i < x.size(); ++i) {
        int start = std::max(0, i - half_w);
        int end = std::min((int)x.size() - 1, i + half_w);

        window.assign(x_data + start, x_data + end + 1);
        std::nth_element(window.begin(), window.begin() + window.size() / 2, window.end());
        res(i) = window[window.size() / 2];
    }
    return res;
}

DspWorker::DspWorker(QObject *parent) : QThread(parent), m_isRunning(false), m_isPaused(false) {
    qRegisterMetaType<FrameResult>("FrameResult");
    qRegisterMetaType<QList<OfflineTargetResult>>("QList<OfflineTargetResult>");
    qRegisterMetaType<std::vector<BatchTargetFeature>>("std::vector<BatchTargetFeature>");
}

DspWorker::~DspWorker() { stop(); wait(); }
void DspWorker::setTargetFiles(const QStringList& files) {
    m_selectedFiles = files;
}
void DspWorker::setConfig(const DspConfig& config) { m_config = config; }
void DspWorker::stop() { m_isRunning = false; }
void DspWorker::pause() { m_isPaused = true; }
void DspWorker::resume() { m_isPaused = false; }

// 【新增函数】：供主线程调用的线程安全接口
void DspWorker::requestRemoveTarget(int targetId) {
    QMutexLocker locker(&m_removeMutex);
    if (!m_targetsToRemove.contains(targetId)) {
        m_targetsToRemove.append(targetId);
    }
}

void DspWorker::run() {
    m_isRunning = true;
    m_isPaused = false;

    QElapsedTimer globalTimer;
    globalTimer.start();

    qint64 totalRealtimeMs = 0;
    qint64 totalBatchMs = 0;
    qint64 total_paused_ms = 0;
    QElapsedTimer sectionTimer;

    QMap<double, QStringList> timeToFilesMap;
    QRegularExpression re("_(\\d+(\\.\\d+)?)s\\.raw$");
    for (const QString& filePath : m_selectedFiles) {
        QRegularExpressionMatch match = re.match(filePath);
        if(match.hasMatch()) {
            timeToFilesMap[match.captured(1).toDouble()].append(filePath);
        }
    }

    const int M = m_config.M;
    const double d = m_config.d;
    const double c = m_config.c;
    const int fs = m_config.fs;
    const int NFFT_R = m_config.nfftR;
    const int NFFT_WIN = m_config.nfftWin;
    const double r_scan = m_config.r_scan;
    const int f_show = 100;
    int half_fft = NFFT_WIN / 2 + 1;

    CBFProcessor cbf_engine(M, d, c, r_scan, fs, NFFT_R, NFFT_WIN,
                            {m_config.lofarMin, m_config.lofarMax},
                            {m_config.demonMin, m_config.demonMax});

    Eigen::MatrixXd theta_scan = cbf_engine.getThetaScan().transpose();
    Eigen::MatrixXd f_lofar = cbf_engine.getFLofar().transpose();
    Eigen::MatrixXd f_demon = cbf_engine.getFDemon().transpose();
    Eigen::MatrixXd x_v = cbf_engine.getXv().transpose();
    Eigen::MatrixXd tau_mat = cbf_engine.getTauMatrix();

    fftw_complex* demon_ifft_in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * NFFT_WIN);
    double* demon_ifft_out = (double*)fftw_malloc(sizeof(double) * NFFT_WIN);
    fftw_plan plan_ifft = fftw_plan_dft_c2r_1d(NFFT_WIN, demon_ifft_in, demon_ifft_out, FFTW_ESTIMATE);

    double* demon_fft_in = (double*)fftw_malloc(sizeof(double) * NFFT_WIN);
    fftw_complex* demon_fft_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * NFFT_WIN);
    fftw_plan plan_fft = fftw_plan_dft_r2c_1d(NFFT_WIN, demon_fft_in, demon_fft_out, FFTW_ESTIMATE);

    FirWinPara demon_fir_para;
    FirWinRtn demon_fir_rtn;
    demon_fir_para.band = LOWPASSFILTER;
    demon_fir_para.fln = m_config.firCutoff * (fs / 2.0);
    demon_fir_para.n = m_config.firOrder;
    demon_fir_para.type = Hamming;
    demon_fir_para.fs = fs;
    FirWin(&demon_fir_para, &demon_fir_rtn);

    std::vector<double> demon_lpf_coefs(demon_fir_rtn.h.size());
    for (int i = 0; i < demon_fir_rtn.h.size(); ++i) demon_lpf_coefs[i] = demon_fir_rtn.h(i);

    Eigen::MatrixXd signal_w = Eigen::MatrixXd::Zero(M, NFFT_WIN);
    TrackManager trackManager;
    trackManager.setParameters(m_config.trackAssocGate, m_config.trackMHits);

    std::vector<FrameResult> history_frames;
    std::vector<FrameResult> batch_frames;

    QMap<int, QVector<double>> lastLofar;
    QMap<int, QVector<double>> lastDemon;
    QMap<int, QVector<double>> lastLineAmp;
    QMap<int, QVector<double>> lastLofarFull;
    QMap<int, QVector<double>> lastCbfLofarFull;

    QMap<int, QVector<double>> accumDcvSum;
    QMap<int, int> accumDcvCount;

    int frameIndex = 1;
    int batchIndex = 1;
    int batchStartFrame = 1;

    int f_num = f_lofar.size();
    int theta_len = theta_scan.size();

    Eigen::MatrixXd precomputed_PSF(theta_len, f_num);
    Eigen::VectorXd cos_th(theta_len);
    for(int i = 0; i < theta_len; ++i) cos_th(i) = std::cos(theta_scan(i) * M_PI / 180.0);

    for (int k = 0; k < f_num; ++k) {
        Eigen::VectorXd u_y = f_lofar(k) * cos_th;
        Eigen::VectorXd PSF_f(theta_len);
        for (int i = 0; i < theta_len; ++i) {
            double arg = M_PI * d * u_y(i) / c;
            if (std::abs(arg) < 1e-9) PSF_f(i) = M * M;
            else {
                double val = std::sin(M * arg) / std::sin(arg);
                PSF_f(i) = val * val;
            }
        }
        double max_psf = PSF_f.maxCoeff();
        if (max_psf > 0) PSF_f /= max_psf;
        precomputed_PSF.col(k) = PSF_f;
    }

    // =========================================================================
    // 【全新干净的 generateAndEmitEvaluation】
    // 彻底去除了深度的重复计算，交由 MainWindow 通过 SelfValidator 组装终极报告！
    // =========================================================================
    auto generateAndEmitEvaluation = [&](bool isFinal = false) {
        double total_time_sec = (globalTimer.elapsed() - total_paused_ms) / 1000.0;
        double realtime_time_sec = totalRealtimeMs / 1000.0;
        double batch_time_sec = totalBatchMs / 1000.0;

        QSet<int> unique_tids_set;
        for (const auto& frame : history_frames) {
            for (const auto& tr : frame.tracks) {
                if (tr.isConfirmed) unique_tids_set.insert(tr.id);
            }
        }
        QList<int> valid_tids = unique_tids_set.values();
        std::sort(valid_tids.begin(), valid_tids.end());

        SystemEvaluationResult evalRes;
        evalRes.totalTimeSec = total_time_sec;
        evalRes.realtimeTimeSec = realtime_time_sec;
        evalRes.batchTimeSec = batch_time_sec;
        evalRes.confirmedTargetCount = valid_tids.size();
        evalRes.isMfpEnabled = m_config.enableDepthResolve;
        evalRes.isFinal = isFinal;

        for (int tid : valid_tids) {
            std::vector<double> freqs, freqsDcv, shafts;
            int active_frames = 0;
            double initCalcAngle = -1.0;
            double currCalcAngle = -1.0;
            double firstTime = -1.0;
            double lastTime = -1.0;

            for (const auto& frame : history_frames) {
                for (const auto& t : frame.tracks) {
                    if (t.id == tid && t.isActive) {
                        active_frames++;
                        if (t.shaftFreq > 0) shafts.push_back(t.shaftFreq);
                        for (double f : t.lineSpectra) freqs.push_back(f);
                        for (double f : t.lineSpectraDcv) freqsDcv.push_back(f);

                        if (initCalcAngle < 0) {
                            initCalcAngle = t.currentAngle;
                            firstTime = frame.timestamp;
                        }
                        currCalcAngle = t.currentAngle;
                        lastTime = frame.timestamp;
                    }
                }
            }
            if (active_frames == 0) continue;

            TargetTruth currentTruth;
            bool hasTruth = false;
            for (const auto& gt : m_groundTruths) {
                if (gt.id == tid) {
                    currentTruth = gt;
                    hasTruth = true;
                    break;
                }
            }

            double initTrueAngle = 0.0;
            double currTrueAngle = 0.0;
            if (hasTruth) {
                initTrueAngle = currentTruth.initialAngle;
                double dt = lastTime;
                if (dt > 0) {
                    double x0 = currentTruth.initialDistance * std::sin(currentTruth.initialAngle * M_PI / 180.0);
                    double y0 = currentTruth.initialDistance * std::cos(currentTruth.initialAngle * M_PI / 180.0);
                    double vx = currentTruth.speed * std::sin(currentTruth.course * M_PI / 180.0);
                    double vy = currentTruth.speed * std::cos(currentTruth.course * M_PI / 180.0);
                    double xt = x0 + vx * dt;
                    double yt = y0 + vy * dt;
                    currTrueAngle = std::atan2(xt, yt) * 180.0 / M_PI;
                    if (currTrueAngle < 0) currTrueAngle += 360.0;
                } else {
                    currTrueAngle = initTrueAngle;
                }
            }

            std::sort(freqs.begin(), freqs.end());
            QString freqStr = "未检测到有效线谱";
            int total_hits = 0, num_valid_lines = 0;
            std::vector<double> valid_f; std::vector<int> valid_c;

            if (!freqs.empty()) {
                std::vector<double> final_f; std::vector<int> final_c;
                std::vector<double> cur_cluster; cur_cluster.push_back(freqs[0]);
                for (size_t i = 1; i < freqs.size(); ++i) {
                    if (freqs[i] - freqs[i-1] > 3.5) {
                        final_f.push_back(calculateMedian(cur_cluster)); final_c.push_back(cur_cluster.size()); cur_cluster.clear();
                    }
                    cur_cluster.push_back(freqs[i]);
                }
                final_f.push_back(calculateMedian(cur_cluster)); final_c.push_back(cur_cluster.size());

                int min_hit = std::max(2, (int)std::floor(0.20 * active_frames));
                if (active_frames <= 2) min_hit = 1;

                for (size_t i = 0; i < final_f.size(); ++i) {
                    if (final_c[i] >= min_hit) {
                        valid_f.push_back(final_f[i]); valid_c.push_back(std::min(final_c[i], active_frames));
                    }
                }
                if (!valid_f.empty()) {
                    freqStr = "";
                    num_valid_lines = valid_f.size();
                    for (size_t i = 0; i < valid_f.size(); ++i) {
                        freqStr += QString("%1Hz(%2/%3)").arg(valid_f[i], 0, 'f', 1).arg(valid_c[i]).arg(active_frames);
                        if (i != valid_f.size() - 1) freqStr += ", ";
                        total_hits += valid_c[i];
                    }
                }
            }

            std::sort(freqsDcv.begin(), freqsDcv.end());
            QString freqStrDcv = "未检测到DCV累积线谱";
            int total_hits_dcv = 0, num_valid_lines_dcv = 0;
            std::vector<double> valid_f_dcv; std::vector<int> valid_c_dcv;

            if (!freqsDcv.empty()) {
                std::vector<double> final_f_dcv; std::vector<int> final_c_dcv;
                std::vector<double> cur_cluster_dcv; cur_cluster_dcv.push_back(freqsDcv[0]);
                for (size_t i = 1; i < freqsDcv.size(); ++i) {
                    if (freqsDcv[i] - freqsDcv[i-1] > 3.5) {
                        final_f_dcv.push_back(calculateMedian(cur_cluster_dcv)); final_c_dcv.push_back(cur_cluster_dcv.size()); cur_cluster_dcv.clear();
                    }
                    cur_cluster_dcv.push_back(freqsDcv[i]);
                }
                final_f_dcv.push_back(calculateMedian(cur_cluster_dcv)); final_c_dcv.push_back(cur_cluster_dcv.size());

                int min_hit_dcv = std::max(2, (int)std::floor(0.20 * active_frames));
                if (active_frames <= 2) min_hit_dcv = 1;

                for (size_t i = 0; i < final_f_dcv.size(); ++i) {
                    if (final_c_dcv[i] >= min_hit_dcv) {
                        valid_f_dcv.push_back(final_f_dcv[i]); valid_c_dcv.push_back(std::min(final_c_dcv[i], active_frames));
                    }
                }
                if (!valid_f_dcv.empty()) {
                    freqStrDcv = "";
                    num_valid_lines_dcv = valid_f_dcv.size();
                    for (size_t i = 0; i < valid_f_dcv.size(); ++i) {
                        freqStrDcv += QString("%1Hz(%2/%3)").arg(valid_f_dcv[i], 0, 'f', 1).arg(valid_c_dcv[i]).arg(active_frames);
                        if (i != valid_f_dcv.size() - 1) freqStrDcv += ", ";
                        total_hits_dcv += valid_c_dcv[i];
                    }
                }
            }

            double current_accuracy = 0.0;
            double current_accuracy_dcv = 0.0;

            if (hasTruth && !currentTruth.trueLofarFreqs.empty() && active_frames > 0) {
                int trueLineCount = currentTruth.trueLofarFreqs.size();
                int total_possible = trueLineCount * active_frames;

                int effective_hits = 0, false_alarms = 0;
                for (size_t i = 0; i < valid_f.size(); ++i) {
                    bool matched = false;
                    for (double trueF : currentTruth.trueLofarFreqs) {
                        if (std::abs(valid_f[i] - trueF) <= 3.5) { matched = true; effective_hits += valid_c[i]; break; }
                    }
                    if (!matched) false_alarms++;
                }
                double score_inst = (double)effective_hits * 100.0 / total_possible - false_alarms * 10.0;
                current_accuracy = std::max(0.0, std::min(100.0, score_inst));

                int effective_hits_dcv = 0, false_alarms_dcv = 0;
                for (size_t i = 0; i < valid_f_dcv.size(); ++i) {
                    bool matched = false;
                    for (double trueF : currentTruth.trueLofarFreqs) {
                        if (std::abs(valid_f_dcv[i] - trueF) <= 3.5) { matched = true; effective_hits_dcv += valid_c_dcv[i]; break; }
                    }
                    if (!matched) false_alarms_dcv++;
                }
                double score_dcv = (double)effective_hits_dcv * 100.0 / total_possible - false_alarms_dcv * 10.0;
                current_accuracy_dcv = std::max(0.0, std::min(100.0, score_dcv));
            } else {
                if (num_valid_lines > 0 && active_frames > 0) current_accuracy = std::min(100.0, (double)total_hits * 100.0 / (num_valid_lines * active_frames));
                if (num_valid_lines_dcv > 0 && active_frames > 0) current_accuracy_dcv = std::min(100.0, (double)total_hits_dcv * 100.0 / (num_valid_lines_dcv * active_frames));
            }

            double median_shaft = shafts.empty() ? 0.0 : calculateMedian(shafts);

            TargetEvaluation tEval;
            tEval.targetId = tid;
            tEval.name = hasTruth ? currentTruth.name : QString("Target %1").arg(tid);
            tEval.lineSpectraStr = freqStr;
            tEval.lineSpectraStrDcv = freqStrDcv;
            tEval.accuracy = current_accuracy;
            tEval.accuracyDcv = current_accuracy_dcv;
            tEval.shaftFreq = median_shaft;
            tEval.hasTruth = hasTruth;
            tEval.initialTrueAngle = initTrueAngle;
            tEval.currentTrueAngle = currTrueAngle;
            tEval.initialCalcAngle = initCalcAngle;
            tEval.currentCalcAngle = currCalcAngle;

            // 剥离MFP深度模块：所有深度默认置空
            tEval.estimatedDepth = -1.0;
            tEval.targetClass = "未知";
            tEval.isMfpCorrect = false;

            evalRes.targetEvals.append(tEval);
        }

        // 发送给 MainWindow，MainWindow 会负责结合表二的真实深度打出终极报告
        emit evaluationResultReady(evalRes);

        // 如果到达结束，仅输出跟踪对比表
        if (isFinal) {
            QString report = "=================================================================================\n";
            report += "                 目标每帧方位角动态跟踪表 (DCV vs CBF 精度对比)              \n";
            report += "=================================================================================\n";
            QString h1 = "| 帧号   | 时间(s)  ";
            for (int tid : valid_tids) h1 += QString("|   目标%1(DCV/CBF)   ").arg(tid, -6);
            report += h1 + "|\n|--------|----------";
            for (int tid : valid_tids) report += "|--------------------------";
            report += "|\n";

            for (const auto& f : history_frames) {
                QString row = QString("| %1 | %2 ").arg(f.frameIndex, -6).arg(f.timestamp, -8, 'f', 1);
                for (int tid : valid_tids) {
                    double ang_dcv = -1, ang_cbf = -1;
                    for(auto& tr : f.tracks) { if(tr.id == tid) { ang_dcv = tr.currentAngle; ang_cbf = tr.currentAngleCbf; } }
                    if(ang_dcv >= 0) row += QString("|  %1° / %2°       ").arg(ang_dcv, 5, 'f', 1).arg(ang_cbf, 5, 'f', 1);
                    else row += "| -                        ";
                }
                report += row + "|\n";
            }
            report += "=================================================================================\n";
            emit reportReady(report);
            emit logReady("\n>> 所有数据处理完毕。");
        }
    };

    for (double current_time : timeToFilesMap.keys()) {
        while (m_isPaused && m_isRunning) {
            QElapsedTimer pauseTimer; pauseTimer.start();
            QThread::msleep(100);
            total_paused_ms += pauseTimer.elapsed();
        }
        if (!m_isRunning) break;

        {
            QMutexLocker locker(&m_removeMutex);
            if (!m_targetsToRemove.isEmpty()) {
                for (int tid : m_targetsToRemove) {
                    trackManager.removeTrackById(tid);
                    lastLofar.remove(tid); lastDemon.remove(tid); lastLineAmp.remove(tid);
                    lastLofarFull.remove(tid); lastCbfLofarFull.remove(tid);
                    accumDcvSum.remove(tid); accumDcvCount.remove(tid);
                    for (auto& frame : history_frames) {
                        for (int i = frame.tracks.size() - 1; i >= 0; --i) {
                            if (frame.tracks[i].id == tid) frame.tracks.removeAt(i);
                        }
                    }
                    for (auto& frame : batch_frames) {
                        for (int i = frame.tracks.size() - 1; i >= 0; --i) {
                            if (frame.tracks[i].id == tid) frame.tracks.removeAt(i);
                        }
                    }
                }
                m_targetsToRemove.clear();
            }
        }

        FrameResult result;
        result.frameIndex = frameIndex;
        result.timestamp = current_time;
        const QStringList& targetFiles = timeToFilesMap[current_time];

        sectionTimer.restart();

        try {
            Eigen::MatrixXd signal_raw = Eigen::MatrixXd::Zero(M, NFFT_R);
            for(const QString& file : targetFiles) signal_raw += RawReader::read_raw_file(file.toStdString(), M, NFFT_R);

            signal_w.leftCols(NFFT_WIN - NFFT_R) = signal_w.rightCols(NFFT_WIN - NFFT_R);
            signal_w.rightCols(NFFT_R) = signal_raw;

            CBFResult cbf_res = cbf_engine.process(signal_w);
            Eigen::VectorXd p_cbf_spatial = cbf_res.P_cbf_spatial;

            Eigen::MatrixXd P_dcv_out = Eigen::MatrixXd::Zero(theta_len, f_num);
            Eigen::MatrixXd P_dcv_out_energy = Eigen::MatrixXd::Zero(theta_len, f_num);

#pragma omp parallel for
            for (int k = 0; k < f_num; ++k) {
                Eigen::VectorXd P_f = cbf_res.P_out.col(k);
                Eigen::VectorXd PSF_f = precomputed_PSF.col(k);
                Eigen::VectorXd dcv_norm = RL_1D(P_f, PSF_f, m_config.dcvRlIter);
                P_dcv_out.col(k) = dcv_norm;
                P_dcv_out_energy.col(k) = dcv_norm * P_f.sum();
            }

            Eigen::VectorXd p_dcv_1d = P_dcv_out.rowwise().sum();
            Eigen::VectorXd Beamout_tmp = p_dcv_1d;

            std::vector<double> v_beam(Beamout_tmp.data(), Beamout_tmp.data() + Beamout_tmp.size());
            double mid = calculateMedian(v_beam);
            std::vector<double> v_diff(v_beam.size());
            for(size_t i=0; i<v_beam.size(); ++i) v_diff[i] = std::abs(v_beam[i] - mid);
            double upmid = calculateMedian(v_diff);

            double sum_bg = 0, sq_sum_bg = 0; int count_bg = 0;
            for(size_t i=0; i<v_beam.size(); ++i) if(std::abs(v_beam[i] - mid) <= 3 * upmid) { sum_bg += v_beam[i]; count_bg++; }
            double mean_bg = count_bg > 0 ? sum_bg / count_bg : 0.0;
            for(size_t i=0; i<v_beam.size(); ++i) if(std::abs(v_beam[i] - mid) <= 3 * upmid) sq_sum_bg += (v_beam[i] - mean_bg) * (v_beam[i] - mean_bg);
            double std_bg = count_bg > 0 ? std::sqrt(sq_sum_bg / count_bg) : 0.0;

            double threshold2_az_bg = mean_bg + m_config.azDetBgMult * std_bg;
            double threshold2_az_sidelobe = m_config.azDetSidelobeRatio * Beamout_tmp.maxCoeff();
            double threshold2_az = std::max(threshold2_az_bg, threshold2_az_sidelobe);

            for(int i = 0; i < Beamout_tmp.size(); ++i) {
                if(Beamout_tmp[i] < threshold2_az) Beamout_tmp[i] = 0.0;
            }

            std::vector<int> theta_index_current = findPeaks(Beamout_tmp, m_config.azDetPeakMinDist);
            std::vector<double> detected_angles;
            for(int idx : theta_index_current) detected_angles.push_back(cbf_engine.getThetaScan()(idx));

            std::vector<int> locs_cbf_all = findPeaks(p_cbf_spatial, m_config.azDetPeakMinDist);
            if (locs_cbf_all.empty()) {
                int max_idx; p_cbf_spatial.maxCoeff(&max_idx);
                locs_cbf_all.push_back(max_idx);
            }
            std::vector<double> angles_cbf_all;
            for (int idx : locs_cbf_all) angles_cbf_all.push_back(cbf_engine.getThetaScan()(idx));

            result.detectedAngles = QVector<double>(detected_angles.begin(), detected_angles.end());

            QList<TargetTrack> all_tracks = trackManager.updateTracks(detected_angles, theta_index_current, angles_cbf_all);
            QList<TargetTrack> valid_confirmed_tracks;

            QString frameLog = QString("\n---- [第%1帧 实时检测汇报] ----\n").arg(result.frameIndex);
            int valid_clusters = 0;
            QString clusterLog;

            for(auto& t : all_tracks) {
                if (!t.isConfirmed) continue;
                if (t.currentLoc < 0) continue;

                if (t.isActive) {
                    Eigen::VectorXd p_dcv_single = P_dcv_out_energy.row(t.currentLoc);
                    Eigen::VectorXd spectrum_db = (p_dcv_single.array() + 1e-12).log10() * 10.0;
                    spectrum_db = (spectrum_db.array() < -80.0).select(-80.0, spectrum_db);
                    t.lofarSpectrum = QVector<double>(spectrum_db.data(), spectrum_db.data() + spectrum_db.size());

                    Eigen::VectorXd background_db = medfilt1(spectrum_db, m_config.lofarBgMedWindow);
                    Eigen::VectorXd snr_db = spectrum_db - background_db;

                    int edge_cut = 30;
                    for (int i = 0; i < edge_cut && i < snr_db.size(); ++i) snr_db(i) = 0.0;
                    for (int i = std::max(0, (int)snr_db.size() - edge_cut); i < snr_db.size(); ++i) snr_db(i) = 0.0;

                    double mean_snr = snr_db.mean();
                    double std_snr = std::sqrt((snr_db.array() - mean_snr).square().sum() / (snr_db.size() - 1.0));
                    double threshold_ls = mean_snr + m_config.lofarSnrThreshMult * std_snr;
                    for(int i=0; i<snr_db.size(); ++i) {
                        if(snr_db(i) < threshold_ls || snr_db(i) < 0) snr_db(i) = 0.0;
                    }

                    std::vector<int> temp_locs = findPeaks(snr_db, m_config.lofarPeakMinDist);
                    std::vector<int> locs_ls;
                    for(int idx : temp_locs) {
                        if (snr_db(idx) >= 2.0) locs_ls.push_back(idx);
                    }

                    t.lineSpectrumAmp = QVector<double>(spectrum_db.size(), -150.0);
                    for(int idx : locs_ls) {
                        t.lineSpectra.push_back(cbf_engine.getFLofar()(idx));
                        t.lineSpectrumAmp[idx] = spectrum_db(idx);
                    }

                    Eigen::VectorXd full_lofar_linear = Eigen::VectorXd::Constant(half_fft, 1e-12);
                    double df_calc = fs / (double)NFFT_WIN;
                    auto f_lofar_vec = cbf_engine.getFLofar();
                    for(int k = 0; k < f_lofar_vec.size(); ++k) {
                        int bin = std::round(f_lofar_vec(k) / df_calc);
                        if(bin >= 0 && bin < half_fft) full_lofar_linear(bin) = p_dcv_single(k);
                    }
                    t.lofarFullLinear = QVector<double>(full_lofar_linear.data(), full_lofar_linear.data() + full_lofar_linear.size());

                    Eigen::VectorXd p_cbf_single = cbf_res.P_out.row(t.currentLoc);
                    Eigen::VectorXd full_cbf_linear = Eigen::VectorXd::Constant(half_fft, 1e-12);
                    for(int k = 0; k < f_lofar_vec.size(); ++k) {
                        int bin = std::round(f_lofar_vec(k) / df_calc);
                        if(bin >= 0 && bin < half_fft) full_cbf_linear(bin) = p_cbf_single(k);
                    }
                    t.cbfLofarFullLinear = QVector<double>(full_cbf_linear.data(), full_cbf_linear.data() + full_cbf_linear.size());

                    if (!accumDcvSum.contains(t.id)) {
                        accumDcvSum[t.id] = t.lofarFullLinear;
                        accumDcvCount[t.id] = 1;
                    } else {
                        for(int i=0; i<t.lofarFullLinear.size(); ++i) accumDcvSum[t.id][i] += t.lofarFullLinear[i];
                        accumDcvCount[t.id]++;
                    }

                    Eigen::VectorXd accum_dcv_db(f_lofar_vec.size());
                    for(int k=0; k<f_lofar_vec.size(); ++k) {
                        int bin = std::round(f_lofar_vec(k) / df_calc);
                        double val = (bin>=0 && bin<half_fft) ? (accumDcvSum[t.id][bin] / accumDcvCount[t.id]) : 1e-12;
                        accum_dcv_db(k) = 10.0 * std::log10(val + 1e-12);
                    }
                    accum_dcv_db = (accum_dcv_db.array() < -80.0).select(-80.0, accum_dcv_db);
                    t.accumulatedDcvSpectrum = QVector<double>(accum_dcv_db.data(), accum_dcv_db.data() + accum_dcv_db.size());

                    Eigen::VectorXd bg_dcv_db = medfilt1(accum_dcv_db, m_config.dcvLofarBgMedWindow);
                    Eigen::VectorXd snr_dcv_db = accum_dcv_db - bg_dcv_db;

                    for (int i = 0; i < edge_cut && i < snr_dcv_db.size(); ++i) snr_dcv_db(i) = 0.0;
                    for (int i = std::max(0, (int)snr_dcv_db.size() - edge_cut); i < snr_dcv_db.size(); ++i) snr_dcv_db(i) = 0.0;

                    double mean_snr_dcv = snr_dcv_db.mean();
                    double std_snr_dcv = std::sqrt((snr_dcv_db.array() - mean_snr_dcv).square().sum() / (snr_dcv_db.size() - 1.0));
                    double threshold_dcv = mean_snr_dcv + m_config.dcvLofarSnrThreshMult * std_snr_dcv;

                    for(int i=0; i<snr_dcv_db.size(); ++i) {
                        if(snr_dcv_db(i) < threshold_dcv || snr_dcv_db(i) < 0) snr_dcv_db(i) = 0.0;
                    }

                    int min_dist_30hz = std::round(30.0 * NFFT_WIN / fs);
                    int actual_peak_dist = std::max(m_config.dcvLofarPeakMinDist, min_dist_30hz);
                    std::vector<int> locs_dcv = findPeaks(snr_dcv_db, actual_peak_dist);

                    t.lineSpectrumAmpDcv = QVector<double>(accum_dcv_db.size(), -150.0);

                    // ==== 【MFP 深度分辨相关的代码已彻底删除，完全交由 MainWindow 和 SelfValidator 接管】 ====
                    for(int idx : locs_dcv) {
                        if (snr_dcv_db(idx) >= 3.0) {
                            double freq_val = f_lofar_vec(idx);
                            t.lineSpectraDcv.push_back(freq_val);
                            t.lineSpectrumAmpDcv[idx] = accum_dcv_db(idx);
                        }
                    }

                    std::complex<double> J(0, 1);
                    Eigen::MatrixXd Phase_demon = 2.0 * M_PI * tau_mat.row(t.currentLoc).transpose() * f_demon;
                    Eigen::MatrixXcd W_steer_demon = (J * Phase_demon).array().exp();
                    Eigen::RowVectorXcd beam_f_demon = (cbf_res.signal_fft_demon.array() * W_steer_demon.array()).colwise().sum();

                    memset(demon_ifft_in, 0, sizeof(fftw_complex) * NFFT_WIN);
                    int demon_start_idx = std::round(m_config.demonMin * NFFT_WIN / fs);
                    for(int i = 0; i < beam_f_demon.size(); ++i) {
                        int target_idx = demon_start_idx + i;
                        if (target_idx >= 0 && target_idx < NFFT_WIN) {
                            demon_ifft_in[target_idx][0] = beam_f_demon(i).real();
                            demon_ifft_in[target_idx][1] = beam_f_demon(i).imag();
                        }
                    }
                    fftw_execute(plan_ifft);

                    Eigen::VectorXd rs_square(NFFT_WIN);
                    for(int i = 0; i < NFFT_WIN; ++i) rs_square(i) = demon_ifft_out[i] * demon_ifft_out[i];

                    FIR fir_demon(demon_lpf_coefs.data(), demon_lpf_coefs.size());
                    Eigen::VectorXd envlf(NFFT_WIN);
                    for(int i = 0; i < NFFT_WIN; ++i) envlf(i) = fir_demon.filter(rs_square(i));
                    Eigen::VectorXd s = envlf.array() - envlf.mean();

                    for(int i = 0; i < NFFT_WIN; ++i) demon_fft_in[i] = s(i);
                    fftw_execute(plan_fft);

                    int f_end_demon = (f_show * NFFT_WIN) / fs;
                    f_end_demon = std::min(f_end_demon, NFFT_WIN - 1);

                    Eigen::VectorXd data_freq_amp(f_end_demon);
                    for(int i=1; i<=f_end_demon; ++i) {
                        data_freq_amp(i-1) = std::sqrt(demon_fft_out[i][0]*demon_fft_out[i][0] + demon_fft_out[i][1]*demon_fft_out[i][1]);
                    }
                    data_freq_amp /= (data_freq_amp.maxCoeff() + 1e-12);
                    t.demonSpectrum = QVector<double>(data_freq_amp.data(), data_freq_amp.data() + data_freq_amp.size());

                    int max_idx = 0; double max_val = 0;
                    for(int i = 2*NFFT_WIN/fs; i <= std::min(30*NFFT_WIN/fs, f_end_demon); ++i) {
                        if(data_freq_amp(i-1) > max_val) { max_val = data_freq_amp(i-1); max_idx = i; }
                    }
                    t.shaftFreq = max_idx * ((double)fs / NFFT_WIN);

                    lastLofar[t.id] = t.lofarSpectrum;
                    lastDemon[t.id] = t.demonSpectrum;
                    lastLineAmp[t.id] = t.lineSpectrumAmp;
                    lastLofarFull[t.id] = t.lofarFullLinear;
                    lastCbfLofarFull[t.id] = t.cbfLofarFullLinear;

                    frameLog += QString("  ▶ 目标%1 (DCV:%2°, CBF:%3°) 实时轴频检测: %4 Hz\n")
                            .arg(t.id).arg(t.currentAngle, 0, 'f', 1).arg(t.currentAngleCbf, 0, 'f', 1).arg(t.shaftFreq, 0, 'f', 1);

                    if (!t.lineSpectra.empty() || !t.lineSpectraDcv.empty()) {
                        valid_clusters++;
                        clusterLog += QString("    -> 波束 %1° 瞬时线谱: [ ").arg(t.currentAngle, 0, 'f', 1);
                        std::vector<double> freqs1 = t.lineSpectra; std::sort(freqs1.begin(), freqs1.end());
                        for(double f : freqs1) clusterLog += QString("%1 ").arg(f, 0, 'f', 1);
                        clusterLog += "] Hz\n";

                        clusterLog += QString("    -> 波束 %1° DCV累积线谱: [ ").arg(t.currentAngle, 0, 'f', 1);
                        std::vector<double> freqs2 = t.lineSpectraDcv; std::sort(freqs2.begin(), freqs2.end());
                        for(double f : freqs2) clusterLog += QString("%1 ").arg(f, 0, 'f', 1);
                        clusterLog += "] Hz\n";
                    }
                } else {
                    t.lofarSpectrum = lastLofar.value(t.id);
                    t.demonSpectrum = lastDemon.value(t.id);
                    t.lineSpectrumAmp = lastLineAmp.value(t.id);
                    t.lofarFullLinear = lastLofarFull.value(t.id);
                    t.cbfLofarFullLinear = lastCbfLofarFull.value(t.id);
                }
                valid_confirmed_tracks.append(t);
            }

            if (valid_clusters > 0) frameLog += QString("  [本帧全局线谱聚合] 检测到 %1 个特征波束簇:\n").arg(valid_clusters) + clusterLog;
            else frameLog += "  [本帧全局线谱聚合] 未检测到任何波束上的连续频点！\n";
            emit logReady(frameLog);

            result.thetaAxis = QVector<double>(theta_scan.size());
            result.cbfData = QVector<double>(theta_scan.size());
            result.dcvData = QVector<double>(theta_scan.size());
            double cmax = p_cbf_spatial.maxCoeff(), dmax = p_dcv_1d.maxCoeff();
            for(int i=0; i<theta_scan.size(); ++i) {
                result.thetaAxis[i] = cbf_engine.getThetaScan()(i);
                result.cbfData[i] = 10.0 * log10(p_cbf_spatial(i) / (cmax + 1e-12) + 1e-12);
                result.dcvData[i] = 10.0 * log10(p_dcv_1d(i) / (dmax + 1e-12) + 1e-12);
            }

            result.tracks = valid_confirmed_tracks;
            history_frames.push_back(result);
            batch_frames.push_back(result);
            emit frameProcessed(result);

        } catch (...) {
            totalRealtimeMs += sectionTimer.elapsed();
            continue;
        }

        totalRealtimeMs += sectionTimer.elapsed();

        if (batch_frames.size() == m_config.batchSize || frameIndex == timeToFilesMap.size()) {
            sectionTimer.restart();
            std::vector<BatchTargetFeature> batchFeatures;
            int currentEndFrame = frameIndex;

            for (int tid = 1; tid <= trackManager.getConfirmedTargetCount(); ++tid) {
                std::vector<double> freqs, freqsDcv, shafts;
                int active_frames = 0;
                double last_angle = 0.0;

                for (const auto& f : batch_frames) {
                    for (const auto& t : f.tracks) {
                        if (t.id == tid && t.isActive) {
                            active_frames++;
                            if (t.shaftFreq > 0) shafts.push_back(t.shaftFreq);
                            for (double f_line : t.lineSpectra) freqs.push_back(f_line);
                            for (double f_line : t.lineSpectraDcv) freqsDcv.push_back(f_line);
                            last_angle = t.currentAngle;
                        }
                    }
                }

                if (active_frames == 0) continue;

                BatchTargetFeature feature;
                feature.formalId = tid;
                feature.calAngle = last_angle;
                feature.calDemon = shafts.empty() ? 0.0 : calculateMedian(shafts);

                std::sort(freqs.begin(), freqs.end());
                if (!freqs.empty()) {
                    std::vector<double> final_f; std::vector<int> final_c;
                    std::vector<double> cur_cluster; cur_cluster.push_back(freqs[0]);

                    for (size_t i = 1; i < freqs.size(); ++i) {
                        if (freqs[i] - freqs[i-1] > 2.0) {
                            final_f.push_back(calculateMedian(cur_cluster)); final_c.push_back(cur_cluster.size()); cur_cluster.clear();
                        }
                        cur_cluster.push_back(freqs[i]);
                    }
                    final_f.push_back(calculateMedian(cur_cluster)); final_c.push_back(cur_cluster.size());

                    int min_hit = std::max(2, (int)std::floor(0.20 * active_frames));
                    if (active_frames <= 2) min_hit = 1;

                    for (size_t i = 0; i < final_f.size(); ++i) {
                        if (final_c[i] >= min_hit) feature.calLofar.push_back(final_f[i]);
                    }
                }

                std::sort(freqsDcv.begin(), freqsDcv.end());
                if (!freqsDcv.empty()) {
                    std::vector<double> final_f_dcv; std::vector<int> final_c_dcv;
                    std::vector<double> cur_cluster_dcv; cur_cluster_dcv.push_back(freqsDcv[0]);

                    for (size_t i = 1; i < freqsDcv.size(); ++i) {
                        if (freqsDcv[i] - freqsDcv[i-1] > 2.0) {
                            final_f_dcv.push_back(calculateMedian(cur_cluster_dcv)); final_c_dcv.push_back(cur_cluster_dcv.size()); cur_cluster_dcv.clear();
                        }
                        cur_cluster_dcv.push_back(freqsDcv[i]);
                    }
                    final_f_dcv.push_back(calculateMedian(cur_cluster_dcv)); final_c_dcv.push_back(cur_cluster_dcv.size());

                    int min_hit_dcv = std::max(2, (int)std::floor(0.20 * active_frames));
                    if (active_frames <= 2) min_hit_dcv = 1;

                    for (size_t i = 0; i < final_f_dcv.size(); ++i) {
                        if (final_c_dcv[i] >= min_hit_dcv) feature.calLofarDcv.push_back(final_f_dcv[i]);
                    }
                }

                batchFeatures.push_back(feature);
            }

            emit batchFinished(batchIndex, batchStartFrame, currentEndFrame, batchFeatures);

            QList<OfflineTargetResult> offResults;
            for (int tid = 1; tid <= trackManager.getConfirmedTargetCount(); ++tid) {
                std::vector<QVector<double>> tHistory;
                double startAng = -1.0;
                double minFoundFreq = 9999.0;
                double maxFoundFreq = -9999.0;

                for (const auto& f : history_frames) {
                    for (const auto& tr : f.tracks) {
                        if (tr.id == tid) {
                            tHistory.push_back(tr.lofarFullLinear);
                            if (startAng < 0 && tr.isActive) startAng = tr.currentAngle;
                            if (tr.isActive && !tr.lineSpectraDcv.empty()) {
                                for(double f_line : tr.lineSpectraDcv) {
                                    if(f_line < minFoundFreq) minFoundFreq = f_line;
                                    if(f_line > maxFoundFreq) maxFoundFreq = f_line;
                                }
                            }
                            break;
                        }
                    }
                }
                if (tHistory.empty() || tHistory[0].isEmpty()) continue;

                int M_time = tHistory.size();
                Eigen::MatrixXd lofar_mat = Eigen::MatrixXd::Zero(M_time, half_fft);
                for (int r = 0; r < M_time; ++r) {
                    for (int c = 0; c < half_fft; ++c) lofar_mat(r, c) = tHistory[r][c];
                }

                Eigen::RowVectorXd center_freq, f_stft, t_stft;
                Eigen::MatrixXd Z_TPSW;
                Eigen::MatrixXi counter;

                detect_line_spectrum_from_lofar_change(lofar_mat, fs, NFFT_R, center_freq, Z_TPSW, counter, f_stft, t_stft,
                                                       m_config.tpswG, m_config.tpswE, m_config.tpswC, m_config.dpL, m_config.dpAlpha, m_config.dpBeta, m_config.dpGamma,m_config.dpPrctileThresh, m_config.dpPeakStdMult  );

                OfflineTargetResult offRes;
                offRes.targetId = tid;
                offRes.startAngle = startAng;
                offRes.timeFrames = M_time;
                offRes.freqBins = half_fft;
                offRes.minTime = history_frames.front().timestamp;
                offRes.maxTime = history_frames.back().timestamp;
                if (std::abs(offRes.maxTime - offRes.minTime) < 0.1) offRes.maxTime += 3.0;

                if (minFoundFreq > maxFoundFreq) {
                    offRes.displayFreqMin = m_config.lofarMin;
                    offRes.displayFreqMax = m_config.lofarMax;
                } else {
                    offRes.displayFreqMin = std::max(0.0, minFoundFreq - 10.0);
                    offRes.displayFreqMax = std::min(fs / 2.0, maxFoundFreq + 10.0);
                }

                offRes.rawLofarDb.resize(M_time * half_fft);
                offRes.tpswLofarDb.resize(M_time * half_fft);
                offRes.dpCounter.resize(M_time * half_fft);

                for (int r = 0; r < M_time; ++r) {
                    for (int c = 0; c < half_fft; ++c) {
                        int idx = r * half_fft + c;
                        offRes.rawLofarDb[idx] = 10.0 * log10(lofar_mat(r, c) + 1e-12);
                        offRes.tpswLofarDb[idx] = 10.0 * log10(Z_TPSW(r, c) + 1e-12);
                        offRes.dpCounter[idx] = (counter(c, r) > 0) ? 500.0 : 0.0;
                    }
                }
                offResults.append(offRes);
            }

            emit offlineResultsReady(offResults);
            totalBatchMs += sectionTimer.elapsed();

            batch_frames.clear();
            batchIndex++;
            batchStartFrame = frameIndex + 1;

            bool isLastBatch = (frameIndex == timeToFilesMap.size());
            generateAndEmitEvaluation(isLastBatch);
        }
        frameIndex++;
    }

    if (!m_isRunning) {
        bool isLastBatchPerfectMatch = ((frameIndex - 1) > 0 &&
                                        (frameIndex - 1) == timeToFilesMap.size() &&
                                        (frameIndex - 1) % m_config.batchSize == 0);
        if (!isLastBatchPerfectMatch) {
            generateAndEmitEvaluation(true);
        }
    }

    fftw_destroy_plan(plan_ifft); fftw_free(demon_ifft_in); fftw_free(demon_ifft_out);
    fftw_destroy_plan(plan_fft);  fftw_free(demon_fft_in);  fftw_free(demon_fft_out);
    emit processingFinished();
}
