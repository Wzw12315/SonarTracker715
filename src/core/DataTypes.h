#pragma once
#include <QVector>
#include <QList>
#include <QString>
#include <QMetaType>
#include <vector>

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

    double tpswG = 45.0;
    double tpswE = 10.0;
    double tpswC = 1.25;

    int dpL = 3;
    double dpAlpha = 2.5;
    double dpBeta = 0.8;
    double dpGamma = 0.1;
    double dpPrctileThresh = 99.0;
    double dpPeakStdMult = 1.5;
    int batchSize = 40;

    double trackAssocGate = 6.0;
    int trackMHits = 10;

    bool enableDepthResolve = false;
    QString krakenRawPath = "";
};
Q_DECLARE_METATYPE(DspConfig)

struct TargetTruth {
    int id;
    QString name;
    double initialAngle;
    double initialDistance;
    double speed;
    double course;
    std::vector<double> trueLofarFreqs;
    double trueDemonFreq;
    double trueDepth;
};

struct BatchTargetFeature {
    int formalId;
    double calAngle;
    std::vector<double> calLofar;
    std::vector<double> calLofarDcv;
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

    std::vector<double> lineSpectraDcv;
    QVector<double> lineSpectrumAmpDcv;
    QVector<double> accumulatedDcvSpectrum;

    double estimatedDepth = -1.0;
    QString targetClass = "未知";
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

struct TargetEvaluation {
    int targetId;
    QString lineSpectraStr;
    double accuracy;
    double shaftFreq;

    QString lineSpectraStrDcv;
    double accuracyDcv = 0.0;

    bool hasTruth = false;
    double initialTrueAngle = 0.0;
    double currentTrueAngle = 0.0;
    double initialCalcAngle = 0.0;
    double currentCalcAngle = 0.0;

    double estimatedDepth = -1.0;
    QString targetClass = "未知";

    QString name = "";
    double trueDepth = -1.0;
    QString trueClass = "未知";
    bool isMfpCorrect = false;

    int mfpCorrectCount = 0;
    int mfpTotalCount = 0;
};
Q_DECLARE_METATYPE(TargetEvaluation)

struct SystemEvaluationResult {
    double totalTimeSec;
    double realtimeTimeSec;
    double batchTimeSec;
    int confirmedTargetCount;
    bool isMfpEnabled = false;
    bool isFinal = false; // 标记是否为终极报告
    QList<TargetEvaluation> targetEvals;
};
Q_DECLARE_METATYPE(SystemEvaluationResult)
