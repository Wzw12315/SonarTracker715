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
#include "../core/SelfValidator.h"
#include <QDateTime> // 【新增】：用于三击时间判断
#include <QPointer>  // 【新增】：用于安全指针
// 【新增】：用于保存图表原始布局信息的结构体
struct PlotLayoutInfo {
    QWidget* originalParent = nullptr;
    QLayout* originalLayout = nullptr;
    int row = -1;
    int col = -1;
    int index = -1; // 备用，用于非网格布局
};
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    // 【新增】：重写事件过滤器，用于捕获独立窗口的关闭/状态改变事件
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

    // 【新增】：接收正确率的槽函数
        void onBatchAccuracyComputed(int batchIndex, double accuracy);
private:
    void setupUi();
    void createTargetPlots(int targetId);
    void setupPlotInteraction(QCustomPlot* plot);
    void updatePlotOriginalRange(QCustomPlot* plot);

    // 【修改】：将更新函数重命名，使其包含所有的 Tab2 绘图
    void updateTab2Plots();
    // 【新增】：缓存每个批次的正确率
        QList<QPair<int, double>> m_batchAccuracies;

        // 【新增】：弹出和恢复图表的函数
            void popOutPlot(QCustomPlot* plot);
            void restorePlot(QWidget* popupWindow);
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
    // 【新增】：批处理大小输入框
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

    // 【修改】：Tab2 的布局与图表指针
    QCustomPlot* m_cbfWaterfallPlot;
    QCustomPlot* m_dcvWaterfallPlot;
    QWidget* m_sliceWidget;
    QGridLayout* m_sliceLayout;

    QWidget* m_lofarWaterfallWidget;
    QGridLayout* m_lofarWaterfallLayout;

    QMap<int, QCustomPlot*> m_lsPlots;
    QMap<int, QCustomPlot*> m_lofarPlots;
    QMap<int, QCustomPlot*> m_demonPlots;

    DspWorker* m_worker;
    SelfValidator* m_validator;

    QList<FrameResult> m_historyResults;
    QString m_currentDir;
    DspConfig m_currentConfig;


        // 【新增】：存储图表原始布局信息，键为弹出的独立窗口指针
        QMap<QWidget*, QPair<QCustomPlot*, PlotLayoutInfo>> m_popupPlots;
};
