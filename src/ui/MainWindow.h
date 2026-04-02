#pragma once
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMap>
#include <QSet>
#include <QGridLayout>
#include <QList>
#include <QLabel>
#include <QGroupBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include "qcustomplot.h"
#include "../core/DspWorker.h"
#include "../core/SelfValidator.h"
#include <QDateTime>
#include <QPointer>
#include <QDialog>
#include <QSpinBox>

struct PlotLayoutInfo {
    QWidget* originalParent = nullptr;
    QLayout* originalLayout = nullptr;
    int row = -1;
    int col = -1;
    int index = -1;
};

struct TargetClassInfo {
    QString trueClass;
    QString estClass;
    bool isCorrect;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
private slots:
    void onSelectFilesClicked();
    void onManualTruthClicked(); // 【修改】：替换了原来的 onLoadTruthClicked
    void onStartClicked();
    void onPauseResumeClicked();
    void onStopClicked();
    void onExportClicked();
    void onDeleteTargetClicked();
    void onFrameProcessed(const FrameResult& result);
    void appendLog(const QString& log);
    void appendReport(const QString& report);
    void onOfflineResultsReady(const QList<OfflineTargetResult>& results);
    void onProcessingFinished();
    void onEvaluationResultReady(const SystemEvaluationResult& result);
    void onBatchAccuracyComputed(int batchIndex, double accuracy);

    void onPlotContextMenu(const QPoint &pos);
    void onPlotMouseMove(QMouseEvent *event);
    void onPlotDoubleClick(QMouseEvent *event);
    void onTargetNameChanged(QTableWidgetItem* item); // 【意见三】监听目标改名
    void onDepthResolveToggled(bool checked);
    void onMfpResultReady(const QList<TargetEvaluation>& mfpResults); // 【新增】
private:
    void setupUi();
    void setupPlotInteraction(QCustomPlot* plot);
    void popOutPlot(QCustomPlot* plot);
    void restorePlot(QWidget* popupWindow);
    void updatePlotOriginalRange(QCustomPlot* plot);
    void closePopupsFromLayout(QLayout* targetLayout);
    void createTargetPlots(int targetId);
    void updateTab2Plots();
    QWidget* createCardWidget(QLabel* contentLabel, const QString& bgColor, const QString& title);

    DspWorker* m_worker;
    SelfValidator* m_validator;
    QString m_currentDir;
    DspConfig m_currentConfig;
    QList<FrameResult> m_historyResults;
    QList<QPair<int, double>> m_batchAccuracies;
    QMap<int, TargetClassInfo> m_targetClasses;

    QMap<QWidget*, QPair<QCustomPlot*, PlotLayoutInfo>> m_popupPlots;
    QMap<int, QCustomPlot*> m_lsPlots;
    QMap<int, QCustomPlot*> m_lofarPlots;
    QMap<int, QCustomPlot*> m_demonPlots;

    QLineEdit* m_editFs;
    QLineEdit* m_editM;
    QLineEdit* m_editD;
    QLineEdit* m_editC;
    QLineEdit* m_editRScan;
    QLineEdit* m_editTimeStep;
    QLineEdit* m_editLofarMin;
    QLineEdit* m_editLofarMax;
    QLineEdit* m_editDemonMin;
    QLineEdit* m_editDemonMax;
    QLineEdit* m_editNfftR;
    QLineEdit* m_editNfftWin;
    QLineEdit* m_editAzDetBgMult;
    QLineEdit* m_editAzDetSidelobeRatio;
    QLineEdit* m_editAzDetPeakMinDist;
    QLineEdit* m_editLofarBgMedWindow;
    QLineEdit* m_editLofarSnrThreshMult;
    QLineEdit* m_editLofarPeakMinDist;
    // [新增] 累积 DCV 线谱提取参数输入框
    QLineEdit* m_editDcvLofarBgMedWindow;
    QLineEdit* m_editDcvLofarSnrThreshMult;
    QLineEdit* m_editDcvLofarPeakMinDist;
    QLineEdit* m_editFirOrder;
    QLineEdit* m_editFirCutoff;
    QLineEdit* m_editTpswG;
    QLineEdit* m_editTpswE;
    QLineEdit* m_editTpswC;
    QLineEdit* m_editDpL;
    QLineEdit* m_editDpAlpha;
    QLineEdit* m_editDpBeta;
    QLineEdit* m_editDpGamma;
    QLineEdit* m_editDpPrctileThresh; // 【新增】
    QLineEdit* m_editDpPeakStdMult;   // 【新增】
    QLineEdit* m_editDcvRlIter;
    QLineEdit* m_editBatchSize;

    // 【新增】：航迹关联参数输入框指针
    QLineEdit* m_editTrackAssocGate;
    QLineEdit* m_editTrackMHits;
    QPushButton* m_btnSelectFiles;
    QPushButton* m_btnManualTruth; // 【修改】：替换了 m_btnLoadTruth
    QPushButton* m_btnStart;
    QPushButton* m_btnPauseResume;
    QPushButton* m_btnStop;
    QPushButton* m_btnExport;
    QLabel* m_lblSysInfo;
    QPlainTextEdit* m_logConsole;
    QPlainTextEdit* m_reportConsole;

    QTabWidget* m_mainTabWidget;
    QCustomPlot* m_timeAzimuthPlot;
    QCustomPlot* m_spatialPlot;
    QCPTextElement* m_plotTitle;
    QWidget* m_targetPanelWidget;
    QGridLayout* m_targetLayout;

    QCustomPlot* m_cbfWaterfallPlot;
    QCustomPlot* m_dcvWaterfallPlot;
    QWidget* m_sliceWidget;
    QGridLayout* m_sliceLayout;

    QWidget* m_lofarWaterfallWidget;
    QGridLayout* m_lofarWaterfallLayout;

    QLabel* m_lblStatTime;
    QLabel* m_lblStatTargets;
    QLabel* m_lblStatAvgAcc;
    QTableWidget* m_tableTargetFeatures;
    QTableWidget* m_tableMfpResults;  // 【新增】：专属的 MFP 结果表格
    QCustomPlot* m_plotTargetAccuracy;
    QCustomPlot* m_plotBatchAccuracy;
    QCPBars* m_accuracyBars;

    QLineEdit* m_editDeleteTargetId;
    QPushButton* m_btnDeleteTarget;

    QCustomPlot* m_plotTrueAzimuth;
    QCustomPlot* m_plotCalcAzimuth;
    QMap<int, QCPGraph*> m_trueAzimuthGraphs;
    QMap<int, QCPGraph*> m_calcAzimuthGraphs;
    QLabel* m_lblModeIndicator;

    QStringList m_selectedFiles;         // 【意见一】保存多选的文件路径
    QLineEdit* m_editTaskName;           // 【意见六】任务名称修改框
    QLabel* m_lblNewTargetAlarm;         // 【意见四】新目标指示灯
    QWidget* m_targetLightsWidget;       // 【意见二】常亮指示灯容器
    QHBoxLayout* m_targetLightsLayout;   // 【意见二】常亮指示灯布局
    QMap<int, QLabel*> m_targetLights;   // 【意见二】记录每个目标对应的指示灯
    QMap<int, QString> m_targetNames;    // ★ 【意见三】用来保存用户自定义的目标名称

    QCheckBox* m_chkDepthResolve;
    QString m_krakenRawPath; // 存储选择的 raw 文件路径

    QMap<int, int> m_mfpCorrectCounts;   // 【新增】：长期统计正确次数
        QMap<int, int> m_mfpTotalCounts;     // 【新增】：长期统计总次数
        QMap<int, TargetEvaluation> m_latestMfpResults; // 【新增】：缓存最新深度用于生成终极报告
};
