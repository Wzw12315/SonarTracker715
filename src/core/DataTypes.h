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
    double r_scan = 9000.0;
    double timeStep = 3.0;

    int dcvRlIter = 20;
    double lofarMin = 100.0;
    double lofarMax = 300.0;
    double demonMin = 350.0;
    double demonMax = 2000.0;
    int nfftR = 15000;
    int nfftWin = 30000;

    int lofarBgMedWindow = 150;
    double lofarSnrThreshMult = 2.5;
    int lofarPeakMinDist = 30;

    int firOrder = 64;
    double firCutoff = 0.1;

    double tpswG = 45.0;
    double tpswE = 2.0;
    double tpswC = 1.15;   // 【新增】：TPSW 补偿因子 C
    int dpL = 5;
    double dpAlpha = 1.5;
    double dpBeta = 1.0;
    double dpGamma = 0.1;
};
Q_DECLARE_METATYPE(DspConfig)

// 定义单个目标的实时航迹状态
struct TargetTrack {
    int id;               // 正式对外暴露的目标 ID (仅转正后分配)
    int internal_id;      // 内部跟踪分配的试探流水号
    bool isConfirmed;     // 【新增】：是否已满足 M/N 确认条件
    int totalHits;        // 【新增】：总计命中次数
    int age;              // 【新增】：自创建以来的存活总帧数

    bool isActive;
    int missedCount;
    double currentAngle;
    double currentAngleCbf;

    int currentLoc;

    QVector<double> lofarSpectrum;
    QVector<double> demonSpectrum;
    QVector<double> lineSpectrumAmp;
    QVector<double> lofarFullLinear;
    std::vector<double> lineSpectra;
    double shaftFreq;
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
