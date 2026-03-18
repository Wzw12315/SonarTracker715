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
    void onLoadTruthClicked();
    void onStartClicked();
    void onPauseResumeClicked();
    void onStopClicked();
    void onExportClicked();
    void onFrameProcessed(const FrameResult& result);
    void appendLog(const QString& log);
    void appendReport(const QString& report);
    void onOfflineResultsReady(const QList<OfflineTargetResult>& results);
    void onProcessingFinished();

    void onPlotContextMenu(const QPoint &pos);
    void onPlotMouseMove(QMouseEvent *event);
    void onPlotDoubleClick(QMouseEvent *event);
    void onBatchAccuracyComputed(int batchIndex, double accuracy);

    void onEvaluationResultReady(const SystemEvaluationResult& result);

private:
    void setupUi();
    void createTargetPlots(int targetId);
    void setupPlotInteraction(QCustomPlot* plot);
    void updatePlotOriginalRange(QCustomPlot* plot);
    void updateTab2Plots();

    QWidget* createCardWidget(QLabel* contentLabel, const QString& bgColor, const QString& title);

    QList<QPair<int, double>> m_batchAccuracies;
    QMap<int, TargetClassInfo> m_targetClasses;

    void popOutPlot(QCustomPlot* plot);
    void restorePlot(QWidget* popupWindow);
    // 【新增】：安全回收悬浮窗，防野指针闪退
    void closePopupsFromLayout(QLayout* targetLayout);

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
    QLineEdit* m_editFirOrder;
    QLineEdit* m_editFirCutoff;
    QLineEdit* m_editTpswG;
    QLineEdit* m_editTpswE;
    QLineEdit* m_editTpswC;
    QLineEdit* m_editDpL;
    QLineEdit* m_editDpAlpha;
    QLineEdit* m_editDpBeta;
    QLineEdit* m_editDpGamma;
    QLineEdit* m_editDcvRlIter;
    QLineEdit* m_editBatchSize;

    QPushButton* m_btnSelectFiles;
    QPushButton* m_btnLoadTruth;
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
    QCustomPlot* m_plotTargetAccuracy;
    QCustomPlot* m_plotBatchAccuracy;
    QCPBars* m_accuracyBars;

    QMap<int, QCustomPlot*> m_lsPlots;
    QMap<int, QCustomPlot*> m_lofarPlots;
    QMap<int, QCustomPlot*> m_demonPlots;

    DspWorker* m_worker;
    SelfValidator* m_validator;

    QList<FrameResult> m_historyResults;
    QString m_currentDir;
    DspConfig m_currentConfig;
    QMap<QWidget*, QPair<QCustomPlot*, PlotLayoutInfo>> m_popupPlots;
};
