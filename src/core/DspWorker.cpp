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
    for (int i = 0; i < x.size(); ++i) {
        int start = std::max(0, i - half_w);
        int end = std::min((int)x.size() - 1, i + half_w);
        std::vector<double> window(x.data() + start, x.data() + end + 1);
        res(i) = calculateMedian(window);
    }
    return res;
}

DspWorker::DspWorker(QObject *parent) : QThread(parent), m_isRunning(false), m_isPaused(false) {
    qRegisterMetaType<FrameResult>("FrameResult");
    qRegisterMetaType<QList<OfflineTargetResult>>("QList<OfflineTargetResult>");
    qRegisterMetaType<std::vector<BatchTargetFeature>>("std::vector<BatchTargetFeature>");
}

DspWorker::~DspWorker() { stop(); wait(); }
void DspWorker::setDirectory(const QString& dirPath) { m_directory = dirPath; }
void DspWorker::setConfig(const DspConfig& config) { m_config = config; }
void DspWorker::stop() { m_isRunning = false; }
void DspWorker::pause() { m_isPaused = true; }
void DspWorker::resume() { m_isPaused = false; }

void DspWorker::run() {
    m_isRunning = true;
    m_isPaused = false;
    QElapsedTimer globalTimer;
    globalTimer.start();

    QMap<double, QStringList> timeToFilesMap;
    QDirIterator it(m_directory, QStringList() << "*.raw", QDir::Files, QDirIterator::Subdirectories);
    QRegularExpression re("_(\\d+(\\.\\d+)?)s\\.raw$");
    while(it.hasNext()) {
        QString filePath = it.next();
        QRegularExpressionMatch match = re.match(filePath);
        if(match.hasMatch()) timeToFilesMap[match.captured(1).toDouble()].append(filePath);
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
    std::vector<FrameResult> history_frames;
    std::vector<FrameResult> batch_frames;

    QMap<int, QVector<double>> lastLofar;
    QMap<int, QVector<double>> lastDemon;
    QMap<int, QVector<double>> lastLineAmp;
    QMap<int, QVector<double>> lastLofarFull;
    QMap<int, QVector<double>> lastCbfLofarFull; // 【新增】：缓存 CBF 完整切片

    int frameIndex = 1;
    int batchIndex = 1;
    int batchStartFrame = 1;

    for (double current_time : timeToFilesMap.keys()) {
        while (m_isPaused && m_isRunning) QThread::msleep(100);
        if (!m_isRunning) break;

        FrameResult result;
        result.frameIndex = frameIndex;
        result.timestamp = current_time;
        const QStringList& targetFiles = timeToFilesMap[current_time];

        try {
            Eigen::MatrixXd signal_raw = Eigen::MatrixXd::Zero(M, NFFT_R);
            for(const QString& file : targetFiles) signal_raw += RawReader::read_raw_file(file.toStdString(), M, NFFT_R);

            signal_w.leftCols(NFFT_WIN - NFFT_R) = signal_w.rightCols(NFFT_WIN - NFFT_R);
            signal_w.rightCols(NFFT_R) = signal_raw;

            CBFResult cbf_res = cbf_engine.process(signal_w);
            Eigen::VectorXd p_cbf_spatial = cbf_res.P_cbf_spatial;

            int f_num = f_lofar.size();
            int theta_len = theta_scan.size();
            Eigen::MatrixXd P_dcv_out = Eigen::MatrixXd::Zero(theta_len, f_num);
            Eigen::MatrixXd P_dcv_out_energy = Eigen::MatrixXd::Zero(theta_len, f_num);

            Eigen::VectorXd cos_th(theta_len);
            for(int i = 0; i < theta_len; ++i) {
                cos_th(i) = std::cos(theta_scan(i) * M_PI / 180.0);
            }

            for (int k = 0; k < f_num; ++k) {
                Eigen::VectorXd P_f = cbf_res.P_out.col(k);
                Eigen::VectorXd u_y = f_lofar(k) * cos_th;

                Eigen::VectorXd PSF_f(theta_len);
                for (int i = 0; i < theta_len; ++i) {
                    double arg = M_PI * d * u_y(i) / c;
                    if (std::abs(arg) < 1e-9) {
                        PSF_f(i) = M * M;
                    } else {
                        double val = std::sin(M * arg) / std::sin(arg);
                        PSF_f(i) = val * val;
                    }
                }
                double max_psf = PSF_f.maxCoeff();
                if (max_psf > 0) PSF_f /= max_psf;

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
                        if (snr_db(idx) >= 2.0) {
                            locs_ls.push_back(idx);
                        }
                    }

                    t.lineSpectrumAmp = QVector<double>(spectrum_db.size(), -150.0);
                    for(int idx : locs_ls) {
                        t.lineSpectra.push_back(cbf_engine.getFLofar()(idx));
                        t.lineSpectrumAmp[idx] = spectrum_db(idx);
                    }

                    // --- 提取 DCV 完整切片 ---
                    Eigen::VectorXd full_lofar_linear = Eigen::VectorXd::Constant(half_fft, 1e-12);
                    double df_calc = fs / (double)NFFT_WIN;
                    auto f_lofar_vec = cbf_engine.getFLofar();
                    for(int k = 0; k < f_lofar_vec.size(); ++k) {
                        int bin = std::round(f_lofar_vec(k) / df_calc);
                        if(bin >= 0 && bin < half_fft) full_lofar_linear(bin) = p_dcv_single(k);
                    }
                    t.lofarFullLinear = QVector<double>(full_lofar_linear.data(), full_lofar_linear.data() + full_lofar_linear.size());

                    // --- 【新增】：提取 CBF 完整切片 ---
                    Eigen::VectorXd p_cbf_single = cbf_res.P_out.row(t.currentLoc);
                    Eigen::VectorXd full_cbf_linear = Eigen::VectorXd::Constant(half_fft, 1e-12);
                    for(int k = 0; k < f_lofar_vec.size(); ++k) {
                        int bin = std::round(f_lofar_vec(k) / df_calc);
                        if(bin >= 0 && bin < half_fft) full_cbf_linear(bin) = p_cbf_single(k);
                    }
                    t.cbfLofarFullLinear = QVector<double>(full_cbf_linear.data(), full_cbf_linear.data() + full_cbf_linear.size());


                    std::complex<double> J(0, 1);
                    Eigen::MatrixXd Phase_demon = 2.0 * M_PI * tau_mat.row(t.currentLoc).transpose() * f_demon;
                    Eigen::MatrixXcd W_steer_demon = (J * Phase_demon).array().exp();
                    Eigen::RowVectorXcd beam_f_demon = (cbf_res.signal_fft_demon.array() * W_steer_demon.array()).colwise().sum();

                    memset(demon_ifft_in, 0, sizeof(fftw_complex) * NFFT_WIN);
                    int demon_start_idx = std::round(m_config.demonMin * NFFT_WIN / fs);
                    for(int i=0; i<beam_f_demon.size(); ++i) {
                        demon_ifft_in[demon_start_idx + i][0] = beam_f_demon(i).real();
                        demon_ifft_in[demon_start_idx + i][1] = beam_f_demon(i).imag();
                    }
                    fftw_execute(plan_ifft);

                    Eigen::VectorXd rs_square(NFFT_WIN);
                    for(int i=0; i<NFFT_WIN; ++i) rs_square(i) = demon_ifft_out[i] * demon_ifft_out[i];

                    FIR fir_demon(demon_lpf_coefs.data(), demon_lpf_coefs.size());
                    Eigen::VectorXd envlf(NFFT_WIN);
                    for(int i = 0; i < NFFT_WIN; ++i) envlf(i) = fir_demon.filter(rs_square(i));
                    Eigen::VectorXd s = envlf.array() - envlf.mean();

                    for(int i=0; i<NFFT_WIN; ++i) demon_fft_in[i] = s(i);
                    fftw_execute(plan_fft);

                    int f_end_demon = (f_show * NFFT_WIN) / fs;
                    Eigen::VectorXd data_freq_amp(f_end_demon);
                    for(int i=1; i<=f_end_demon; ++i) data_freq_amp(i-1) = std::sqrt(demon_fft_out[i][0]*demon_fft_out[i][0] + demon_fft_out[i][1]*demon_fft_out[i][1]);
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
                    lastCbfLofarFull[t.id] = t.cbfLofarFullLinear; // 【新增】

                    frameLog += QString("  ▶ 目标%1 (DCV:%2°, CBF:%3°) 实时轴频检测: %4 Hz\n")
                                .arg(t.id).arg(t.currentAngle, 0, 'f', 1).arg(t.currentAngleCbf, 0, 'f', 1).arg(t.shaftFreq, 0, 'f', 1);

                    if (!t.lineSpectra.empty()) {
                        valid_clusters++;
                        clusterLog += QString("    -> 波束 %1° 簇线谱: [ ").arg(t.currentAngle, 0, 'f', 1);
                        std::vector<double> freqs = t.lineSpectra; std::sort(freqs.begin(), freqs.end());
                        for(double f : freqs) clusterLog += QString("%1 ").arg(f, 0, 'f', 1);
                        clusterLog += "] Hz\n";
                    }
                } else {
                    t.lofarSpectrum = lastLofar.value(t.id);
                    t.demonSpectrum = lastDemon.value(t.id);
                    t.lineSpectrumAmp = lastLineAmp.value(t.id);
                    t.lofarFullLinear = lastLofarFull.value(t.id);
                    t.cbfLofarFullLinear = lastCbfLofarFull.value(t.id); // 【新增】
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

        } catch (...) { continue; }

        // =====================================================================
        // 批处理统计、特征计算与【实时滚动绘图】
        // =====================================================================
        if (batch_frames.size() == m_batchSize || frameIndex == timeToFilesMap.size()) {
            std::vector<BatchTargetFeature> batchFeatures;
            int currentEndFrame = frameIndex;

            // 1. 统计当前批次特征给综合报告
            for (int tid = 1; tid <= trackManager.getConfirmedTargetCount(); ++tid) {
                std::vector<double> freqs, shafts;
                int active_frames = 0;
                double last_angle = 0.0;

                for (const auto& f : batch_frames) {
                    for (const auto& t : f.tracks) {
                        if (t.id == tid && t.isActive) {
                            active_frames++;
                            if (t.shaftFreq > 0) shafts.push_back(t.shaftFreq);
                            for (double f_line : t.lineSpectra) freqs.push_back(f_line);
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
                    std::vector<double> final_f;
                    std::vector<int> final_c;
                    std::vector<double> cur_cluster;
                    cur_cluster.push_back(freqs[0]);

                    for (size_t i = 1; i < freqs.size(); ++i) {
                        if (freqs[i] - freqs[i-1] > 2.0) {
                            final_f.push_back(calculateMedian(cur_cluster));
                            final_c.push_back(cur_cluster.size());
                            cur_cluster.clear();
                        }
                        cur_cluster.push_back(freqs[i]);
                    }
                    final_f.push_back(calculateMedian(cur_cluster));
                    final_c.push_back(cur_cluster.size());

                    int min_hit = std::max(2, (int)std::floor(0.20 * active_frames));
                    if (active_frames <= 2) min_hit = 1;

                    for (size_t i = 0; i < final_f.size(); ++i) {
                        if (final_c[i] >= min_hit) {
                            feature.calLofar.push_back(final_f[i]);
                        }
                    }
                }

                batchFeatures.push_back(feature);
            }

            emit batchFinished(batchIndex, batchStartFrame, currentEndFrame, batchFeatures);

            // =================================================================
            // 2. 随批次生成全量历史瀑布图与 DP 特征图
            // =================================================================
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
                            if (tr.isActive && !tr.lineSpectra.empty()) {
                                for(double f_line : tr.lineSpectra) {
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
                                                       m_config.tpswG, m_config.tpswE, m_config.tpswC, m_config.dpL, m_config.dpAlpha, m_config.dpBeta, m_config.dpGamma);

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
                    offRes.displayFreqMin = std::max(0.0, minFoundFreq - 15.0);
                    offRes.displayFreqMax = std::min(fs / 2.0, maxFoundFreq + 15.0);
                }

                offRes.rawLofarDb.resize(M_time * half_fft);
                offRes.tpswLofarDb.resize(M_time * half_fft);
                offRes.dpCounter.resize(M_time * half_fft);

                for (int r = 0; r < M_time; ++r) {
                    for (int c = 0; c < half_fft; ++c) {
                        int idx = r * half_fft + c;
                        offRes.rawLofarDb[idx] = 10.0 * log10(lofar_mat(r, c) + 1e-12);
                        offRes.tpswLofarDb[idx] = 10.0 * log10(Z_TPSW(r, c) + 1e-12);
                        offRes.dpCounter[idx] = (counter(c, r) > 0) ? 10.0 : 0.0;
                    }
                }
                offResults.append(offRes);
            }

            emit offlineResultsReady(offResults);

            batch_frames.clear();
            batchIndex++;
            batchStartFrame = frameIndex + 1;
        }

        frameIndex++;
    }

    // 【核心修复】：删除原来此处的 `if (!m_isRunning) return;`
        // 使得点击终止按钮后，依然能强制继续往下走，进行最终报表的整理和输出！

        double total_time_sec = globalTimer.elapsed() / 1000.0;
        QString report = "\n======================================================\n                 高级航迹管理评估报告                 \n======================================================\n";
        report += "【系统级总体评价】\n";
        report += QString("  ▶ 全流程总计耗时: %1 秒\n").arg(total_time_sec, 0, 'f', 2);
        report += QString("  ▶ 总计稳定识别目标个数: %1 个\n\n").arg(trackManager.getConfirmedTargetCount());

        report += "======================================================\n       目标最终特征提取池 (聚类线谱 + 统计轴频)       \n======================================================\n";

        for (int tid = 1; tid <= trackManager.getConfirmedTargetCount(); ++tid) {
            std::vector<double> freqs, shafts;
            int active_frames = 0;
            for (const auto& frame : history_frames) {
                for (const auto& t : frame.tracks) {
                    if (t.id == tid && t.isActive) {
                        active_frames++;
                        if (t.shaftFreq > 0) shafts.push_back(t.shaftFreq);
                        for (double f : t.lineSpectra) freqs.push_back(f);
                    }
                }
            }
            if (active_frames == 0) continue;

            std::sort(freqs.begin(), freqs.end());
            QString freqStr = "未检测到有效线谱";
            int total_hits = 0;
            int num_valid_lines = 0;

            if (!freqs.empty()) {
                std::vector<double> final_f; std::vector<int> final_c;
                std::vector<double> cur_cluster; cur_cluster.push_back(freqs[0]);

                for (size_t i = 1; i < freqs.size(); ++i) {
                    if (freqs[i] - freqs[i-1] > 3.5) {
                        final_f.push_back(calculateMedian(cur_cluster));
                        final_c.push_back(cur_cluster.size());
                        cur_cluster.clear();
                    }
                    cur_cluster.push_back(freqs[i]);
                }
                final_f.push_back(calculateMedian(cur_cluster)); final_c.push_back(cur_cluster.size());

                std::vector<double> valid_f;
                std::vector<int> valid_c;

                int min_hit = std::max(2, (int)std::floor(0.20 * active_frames));
                if (active_frames <= 2) min_hit = 1;

                for (size_t i = 0; i < final_f.size(); ++i) {
                    if (final_c[i] >= min_hit) {
                        valid_f.push_back(final_f[i]);
                        valid_c.push_back(final_c[i]);
                    }
                }

                if (valid_f.empty()) {
                    freqStr = "发散或命中率极低 (已被系统滤除)";
                } else {
                    freqStr = "";
                    num_valid_lines = valid_f.size();
                    for (size_t i = 0; i < valid_f.size(); ++i) {
                        freqStr += QString("%1Hz(%2/%3)").arg(valid_f[i], 0, 'f', 1).arg(valid_c[i]).arg(active_frames);
                        if (i != valid_f.size() - 1) freqStr += ", ";
                        // 【累加命中总次数】
                        total_hits += valid_c[i];
                    }
                }
            }

            double median_shaft = shafts.empty() ? 0.0 : calculateMedian(shafts);
            report += QString("  ▶ 目标 %1 [高频线谱] : %2 \n").arg(tid).arg(freqStr);

            // 【新增】：输出包含计算公式的百分比线谱正确率
            if (num_valid_lines > 0 && active_frames > 0) {
                int total_possible = num_valid_lines * active_frames;
                double line_acc = (double)total_hits * 100.0 / total_possible;
                report += QString("             [线谱提取正确率] : %1% (%2/%3)\n").arg(line_acc, 0, 'f', 2).arg(total_hits).arg(total_possible);
            }

            if (median_shaft > 0) report += QString("             [低频轴频] : 稳定中心约 %1 Hz\n").arg(median_shaft, 0, 'f', 1);
            else report += QString("             [低频轴频] : 未检测到\n");
        }

        // 【新增】：在此处放入占位符，MainWindow 会拦截并将批次总表渲染到这个位置
        report += "\n[BATCH_ACCURACY_TABLE_PLACEHOLDER]\n";

        report += "=================================================================================\n";
        report += "                 目标每帧方位角动态跟踪表 (DCV vs CBF 精度对比)              \n";
        report += "=================================================================================\n";
        QString h1 = "| 帧号   | 时间(s)  ";
        for (int tid = 1; tid <= trackManager.getConfirmedTargetCount(); ++tid) {
            h1 += QString("|   目标%1(DCV/CBF)   ").arg(tid, -6);
        }
        report += h1 + "|\n|--------|----------";
        for (int tid = 1; tid <= trackManager.getConfirmedTargetCount(); ++tid) report += "|--------------------------";
        report += "|\n";

        for (const auto& f : history_frames) {
            QString row = QString("| %1 | %2 ").arg(f.frameIndex, -6).arg(f.timestamp, -8, 'f', 1);
            for (int tid = 1; tid <= trackManager.getConfirmedTargetCount(); ++tid) {
                double ang_dcv = -1, ang_cbf = -1;
                for(auto& tr : f.tracks) {
                    if(tr.id == tid) { ang_dcv = tr.currentAngle; ang_cbf = tr.currentAngleCbf; }
                }
                if(ang_dcv >= 0) row += QString("|  %1° / %2°       ").arg(ang_dcv, 5, 'f', 1).arg(ang_cbf, 5, 'f', 1);
                else row += "| -                        ";
            }
            report += row + "|\n";
        }
        report += "=================================================================================\n";

        emit reportReady(report);

        fftw_destroy_plan(plan_ifft); fftw_free(demon_ifft_in); fftw_free(demon_ifft_out);
        fftw_destroy_plan(plan_fft);  fftw_free(demon_fft_in);  fftw_free(demon_fft_out);
        emit processingFinished();
    }
