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
#include "qcustomplot.h"
#include "../core/DspWorker.h"
#include "../core/SelfValidator.h" // 【新增】：引入自校验模块

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSelectFilesClicked();
    void onLoadTruthClicked();   // 【新增】：导入 JSON 真值的槽函数
    void onStartClicked();
    void onPauseResumeClicked();
    void onStopClicked();
    void onExportClicked();
    void onFrameProcessed(const FrameResult& result);
    void appendLog(const QString& log);
    void appendReport(const QString& report);
    void onOfflineResultsReady(const QList<OfflineTargetResult>& results);
    void onProcessingFinished();

    // 图表交互事件槽函数
    void onPlotContextMenu(const QPoint &pos);
    void onPlotMouseMove(QMouseEvent *event);
    void onPlotDoubleClick(QMouseEvent *event);

private:
    void setupUi();
    void createTargetPlots(int targetId);

    // 为图表注入交互灵魂的工具函数
    void setupPlotInteraction(QCustomPlot* plot);
    void updatePlotOriginalRange(QCustomPlot* plot);

    // 参数输入框指针
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

    // DCV 迭代次数参数输入框
    QLineEdit* m_editDcvRlIter;

    QPushButton* m_btnSelectFiles;
    QPushButton* m_btnLoadTruth; // 【新增】：先验真值导入按钮
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

    QVBoxLayout* m_tab2Layout;
    QCustomPlot* m_dcvWaterfallPlot;

    QWidget* m_lofarWaterfallWidget;
    QGridLayout* m_lofarWaterfallLayout;

    QMap<int, QCustomPlot*> m_lsPlots;
    QMap<int, QCustomPlot*> m_lofarPlots;
    QMap<int, QCustomPlot*> m_demonPlots;

    DspWorker* m_worker;
    SelfValidator* m_validator; // 【新增】：自校验模块实体指针

    QList<FrameResult> m_historyResults;
    QString m_currentDir;
    DspConfig m_currentConfig;
};
