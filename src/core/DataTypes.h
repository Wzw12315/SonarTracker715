#pragma once
#include <QVector>
#include <QList>
#include <QString>
#include <QMetaType>
#include <vector>

// 全局信号处理配置参数
struct DspConfig {
    double fs = 5000.0;
    int M = 512;
    double d = 1.2;
    double c = 1500.0;
    double r_scan = 20000.0;
    double timeStep = 3.0;

    int dcvRlIter = 20;
    double lofarMin = 100.0;
    double lofarMax = 300.0;
    double demonMin = 350.0;
    double demonMax = 2000.0;
    int nfftR = 15000;
    int nfftWin = 30000;

    double azDetBgMult = 5.0;
    double azDetSidelobeRatio = 0.02;
    int azDetPeakMinDist = 2;

    int lofarBgMedWindow = 150;
    double lofarSnrThreshMult = 2.5;
    int lofarPeakMinDist = 30;

    int dcvLofarBgMedWindow = 100;
    double dcvLofarSnrThreshMult = 1.2;
    int dcvLofarPeakMinDist = 15;

    int firOrder = 64;
    double firCutoff = 0.1;

    // 【针对低 SNR 环境优化的后处理参数】
        double tpswG = 45.0;     // 保护窗保持 45（约 7.5Hz）评估局部背景足够了
        double tpswE = 10.0;     // 【调大】排除窗，防止弱信号的主瓣展宽被误认为背景
        double tpswC = 1.25;     // 【调大】补偿因子，拉高检测门限，压制大量离散噪声点

        int dpL = 3;             // 【调小】搜索限制窗，严禁航迹大跨度跳跃
        double dpAlpha = 2.5;    // 【大幅调大】惩罚因子，强制提取出连贯、平滑的直线特征
        double dpBeta = 0.8;     // 【调小】幅度权重，削弱对瞬时高亮噪声点的盲目追随
        double dpGamma = 0.1;
        // 【新增】：暴露的 DP 门限参数
            double dpPrctileThresh = 99.0;   // DP 状态判决门限 (分位数，默认 99.0)
            double dpPeakStdMult = 1.5;      // DP 寻峰提取门限 (标准差乘子，默认 1.5)
    int batchSize = 40;

    // 【新增】：航迹关联与生命周期参数
    double trackAssocGate = 6.0;
    int trackMHits = 10;

    bool enableDepthResolve = false;
    QString krakenRawPath = "";
};
Q_DECLARE_METATYPE(DspConfig)
struct TargetTruth {
    int id;
    QString name;
    double initialAngle;      // 起始方位(度)
    double initialDistance;   // 起始距离(m)
    double speed;             // 航速(m/s)
    double course;            // 航向(度)
    std::vector<double> trueLofarFreqs; // 真实线谱群
    double trueDemonFreq;     // 真实轴频
    double trueDepth;
};
struct BatchTargetFeature {
    int formalId;
    double calAngle;
    std::vector<double> calLofar;
    std::vector<double> calLofarDcv; // [新增] 批处理聚合后的 DCV 累积线谱
    double calDemon;
};

struct TargetTrack {
    int id;
    int internal_id;
    bool isConfirmed;
    int totalHits;
    int age;

    bool isActive;
    int missedCount;
    double currentAngle;
    double currentAngleCbf;
    int currentLoc;

    QVector<double> lofarSpectrum;
    QVector<double> demonSpectrum;
    QVector<double> lineSpectrumAmp;

    QVector<double> lofarFullLinear;
    QVector<double> cbfLofarFullLinear;

    std::vector<double> lineSpectra;
    double shaftFreq;

    // [新增] 累积DCV线谱专属存储
    std::vector<double> lineSpectraDcv;
    QVector<double> lineSpectrumAmpDcv;
    QVector<double> accumulatedDcvSpectrum;

    double estimatedDepth = -1.0;    // 默认 -1 表示未启用或未检测
    QString targetClass = "未知";     // 默认未知
    bool isSubmarine = false;
};
Q_DECLARE_METATYPE(TargetTrack)

struct FrameResult {
    int frameIndex;
    double timestamp;
    QVector<double> thetaAxis;
    QVector<double> cbfData;
    QVector<double> dcvData;
    QVector<double> detectedAngles;
    QString logMessage;
    QList<TargetTrack> tracks;
};
Q_DECLARE_METATYPE(FrameResult)

struct OfflineTargetResult {
    int targetId;
    double startAngle;
    int timeFrames;
    int freqBins;
    double minTime;
    double maxTime;
    double displayFreqMin;
    double displayFreqMax;
    QVector<double> rawLofarDb;
    QVector<double> tpswLofarDb;
    QVector<double> dpCounter;
};
Q_DECLARE_METATYPE(QList<OfflineTargetResult>)

// =========================================================
// 【新增】：用于前端渲染 Dashboard 驾驶舱的结构化评估结果
// =========================================================
struct TargetEvaluation {
    int targetId;
    QString lineSpectraStr;
    double accuracy; // 百分比正确率 (0~100)
    double shaftFreq;

    // [新增] 累积DCV专属评估结果
    QString lineSpectraStrDcv;
    double accuracyDcv = 0.0;

    // [新增] 真实和解算方位历程跟踪
    bool hasTruth = false;
    double initialTrueAngle = 0.0;
    double currentTrueAngle = 0.0;
    double initialCalcAngle = 0.0;
    double currentCalcAngle = 0.0;

    double estimatedDepth = -1.0;
        QString targetClass = "未知";

        // ===================================
            // 【新增】：传给 Tab 4 专属 MFP 表格的字段
            // ===================================
            QString name = "";            // 目标真实名称
            double trueDepth = -1.0;      // 真实深度
            QString trueClass = "未知";   // 真实类别
            bool isMfpCorrect = false;    // 判别是否正确

            int mfpCorrectCount = 0; // 累计正确次数
                int mfpTotalCount = 0;   // 累计评估次数
};
Q_DECLARE_METATYPE(TargetEvaluation)

struct SystemEvaluationResult {
    double totalTimeSec;
    double realtimeTimeSec;
    double batchTimeSec;
    int confirmedTargetCount;
    // 【新增】：将当前的开启状态传递给前端
        bool isMfpEnabled = false;
    QList<TargetEvaluation> targetEvals;
};
Q_DECLARE_METATYPE(SystemEvaluationResult)
