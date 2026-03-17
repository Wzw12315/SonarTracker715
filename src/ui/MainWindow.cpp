#include "MainWindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QScrollArea>
#include <QDateTime>
#include <QSplitter>
#include <cmath>
#include <algorithm>
#include <QToolTip>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
    m_worker(new DspWorker(this)),
    m_validator(new SelfValidator(this))
{
    setupUi();

    connect(m_btnSelectFiles, &QPushButton::clicked, this, &MainWindow::onSelectFilesClicked);
    connect(m_btnLoadTruth, &QPushButton::clicked, this, &MainWindow::onLoadTruthClicked);
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_btnPauseResume, &QPushButton::clicked, this, &MainWindow::onPauseResumeClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(m_btnExport, &QPushButton::clicked, this, &MainWindow::onExportClicked);

    connect(m_worker, &DspWorker::frameProcessed, this, &MainWindow::onFrameProcessed, Qt::QueuedConnection);
    connect(m_worker, &DspWorker::logReady, this, &MainWindow::appendLog, Qt::QueuedConnection);
    connect(m_worker, &DspWorker::reportReady, this, &MainWindow::appendReport, Qt::QueuedConnection);
    connect(m_worker, &DspWorker::offlineResultsReady, this, &MainWindow::onOfflineResultsReady, Qt::QueuedConnection);
    connect(m_worker, &DspWorker::processingFinished, this, &MainWindow::onProcessingFinished, Qt::QueuedConnection);

    connect(m_worker, &DspWorker::batchFinished,
            m_validator, &SelfValidator::onBatchFinished, Qt::QueuedConnection);
    connect(m_validator, &SelfValidator::validationLogReady,
            this, &MainWindow::appendReport, Qt::QueuedConnection);
}

MainWindow::~MainWindow() {
    m_worker->stop();
    m_worker->wait();
}

void MainWindow::setupPlotInteraction(QCustomPlot* plot) {
    if (!plot) return;
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(plot, &QWidget::customContextMenuRequested, this, &MainWindow::onPlotContextMenu);
    plot->setProperty("showTooltip", true);
    connect(plot, &QCustomPlot::mouseMove, this, &MainWindow::onPlotMouseMove);
    connect(plot, &QCustomPlot::mouseDoubleClick, this, &MainWindow::onPlotDoubleClick);
}

void MainWindow::updatePlotOriginalRange(QCustomPlot* plot) {
    if (!plot) return;
    plot->setProperty("hasOrigRange", true);
    plot->setProperty("origXMin", plot->xAxis->range().lower);
    plot->setProperty("origXMax", plot->xAxis->range().upper);
    plot->setProperty("origYMin", plot->yAxis->range().lower);
    plot->setProperty("origYMax", plot->yAxis->range().upper);
}

void MainWindow::onPlotContextMenu(const QPoint &pos) {
    QCustomPlot* plot = qobject_cast<QCustomPlot*>(sender());
    if (!plot) return;

    QMenu menu(this);
    menu.setStyleSheet("QMenu { background-color: #f0f0f0; border: 1px solid #ccc; } QMenu::item:selected { background-color: #0078d7; color: white; }");

    QAction* actReset = menu.addAction("🔄 还原原始视角 (双击)");
    QAction* actZoomIn = menu.addAction("🔍 放大区域");
    QAction* actZoomOut = menu.addAction("🔎 缩小区域");
    menu.addSeparator();
    QAction* actToggleTip = menu.addAction(plot->property("showTooltip").toBool() ? "💡 隐藏光标数值" : "💡 开启光标数值");
    menu.addSeparator();
    QAction* actSave = menu.addAction("💾 将当前图表保存为 PNG...");

    QAction* selected = menu.exec(plot->mapToGlobal(pos));
    if (selected == actReset) {
        onPlotDoubleClick(nullptr);
    } else if (selected == actZoomIn) {
        plot->xAxis->scaleRange(0.8);
        plot->yAxis->scaleRange(0.8);
        plot->replot();
    } else if (selected == actZoomOut) {
        plot->xAxis->scaleRange(1.25);
        plot->yAxis->scaleRange(1.25);
        plot->replot();
    } else if (selected == actToggleTip) {
        plot->setProperty("showTooltip", !plot->property("showTooltip").toBool());
        if (!plot->property("showTooltip").toBool()) QToolTip::hideText();
    } else if (selected == actSave) {
        QString file = QFileDialog::getSaveFileName(this, "保存图表", "plot_export.png", "Images (*.png)");
        if (!file.isEmpty()) {
            plot->savePng(file, plot->width(), plot->height());
            appendLog(QString(">> 图表已成功导出至: %1\n").arg(file));
        }
    }
}

void MainWindow::onPlotMouseMove(QMouseEvent *event) {
    QCustomPlot* plot = qobject_cast<QCustomPlot*>(sender());
    if (!plot || !plot->property("showTooltip").toBool()) return;

    double x = plot->xAxis->pixelToCoord(event->pos().x());
    double y = plot->yAxis->pixelToCoord(event->pos().y());

    QString xLabel = plot->xAxis->label().isEmpty() ? "X轴" : plot->xAxis->label();
    QString yLabel = plot->yAxis->label().isEmpty() ? "Y轴" : plot->yAxis->label();

    QString text = QString("%1: %2\n%3: %4").arg(xLabel).arg(x, 0, 'f', 2).arg(yLabel).arg(y, 0, 'f', 2);

    for (int i = 0; i < plot->plottableCount(); ++i) {
        if (QCPColorMap* cmap = qobject_cast<QCPColorMap*>(plot->plottable(i))) {
            int keyBin, valueBin;
            cmap->data()->coordToCell(x, y, &keyBin, &valueBin);
            double z = cmap->data()->cell(keyBin, valueBin);
            text += QString("\n能量强度(dB): %1").arg(z, 0, 'f', 2);
            break;
        }
    }
    QToolTip::showText(event->globalPos(), text, plot);
}

void MainWindow::onPlotDoubleClick(QMouseEvent *event) {
    Q_UNUSED(event);
    QCustomPlot* plot = qobject_cast<QCustomPlot*>(sender());
    if (!plot) return;

    if (plot->property("hasOrigRange").toBool()) {
        plot->xAxis->setRange(plot->property("origXMin").toDouble(), plot->property("origXMax").toDouble());
        plot->yAxis->setRange(plot->property("origYMin").toDouble(), plot->property("origYMax").toDouble());
    } else {
        plot->rescaleAxes();
    }
    plot->replot();
}

void MainWindow::setupUi() {
    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* mainVLayout = new QVBoxLayout(centralWidget);
    QSplitter* verticalSplitter = new QSplitter(Qt::Vertical, centralWidget);

    QWidget* topWidget = new QWidget(verticalSplitter);
    QHBoxLayout* topLayout = new QHBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);

    QWidget* leftPanel = new QWidget(topWidget);
    leftPanel->setFixedWidth(350);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0,0,0,0);

    QGroupBox* groupButtons = new QGroupBox("系统控制指令区", leftPanel);
    QVBoxLayout* btnLayout = new QVBoxLayout(groupButtons);

    m_btnSelectFiles = new QPushButton("📂 数据文件输入...", this);
    m_btnLoadTruth   = new QPushButton("🎯 导入先验真值(JSON)...", this);
    m_btnStart       = new QPushButton("▶ 开始算法处理", this);
    m_btnPauseResume = new QPushButton("⏸ 暂停/继续", this);
    m_btnStop        = new QPushButton("⏹ 终止算法", this);
    m_btnExport      = new QPushButton("💾 导出文本报表", this);

    m_btnStart->setEnabled(false);
    m_btnPauseResume->setEnabled(false);
    m_btnStop->setEnabled(false);

    btnLayout->addWidget(m_btnSelectFiles);
    btnLayout->addWidget(m_btnLoadTruth);
    btnLayout->addWidget(m_btnStart);
    btnLayout->addWidget(m_btnPauseResume);
    btnLayout->addWidget(m_btnStop);
    btnLayout->addWidget(m_btnExport);
    leftLayout->addWidget(groupButtons);

    QScrollArea* paramScroll = new QScrollArea(leftPanel);
    paramScroll->setWidgetResizable(true);
    paramScroll->setFrameShape(QFrame::NoFrame);
    QWidget* paramContainer = new QWidget(paramScroll);
    QVBoxLayout* paramLayout = new QVBoxLayout(paramContainer);
    paramLayout->setContentsMargins(0,0,0,0);

    QGroupBox* gArray = new QGroupBox("阵列与物理声学环境", paramContainer);
    QFormLayout* fArray = new QFormLayout(gArray);
    fArray->addRow("采样率 (Hz):", m_editFs = new QLineEdit("5000"));
    fArray->addRow("阵元数量:", m_editM = new QLineEdit("512"));
    fArray->addRow("阵元间距 (m):", m_editD = new QLineEdit("1.2"));
    fArray->addRow("环境声速 (m/s):", m_editC = new QLineEdit("1500.0"));
    fArray->addRow("聚焦半径 (m):", m_editRScan = new QLineEdit("20000.0"));
    fArray->addRow("时间步进 (s):", m_editTimeStep = new QLineEdit("3.0"));
    paramLayout->addWidget(gArray);

    QGroupBox* gFreq = new QGroupBox("目标特征频段划分", paramContainer);
    QFormLayout* fFreq = new QFormLayout(gFreq);
    fFreq->addRow("LOFAR 下限 (Hz):", m_editLofarMin = new QLineEdit("100"));
    fFreq->addRow("LOFAR 上限 (Hz):", m_editLofarMax = new QLineEdit("300"));
    fFreq->addRow("DEMON 下限 (Hz):", m_editDemonMin = new QLineEdit("500"));
    fFreq->addRow("DEMON 上限 (Hz):", m_editDemonMax = new QLineEdit("2000"));
    fFreq->addRow("短窗FFT (快拍):", m_editNfftR = new QLineEdit("15000"));
    fFreq->addRow("长窗FFT (分析):", m_editNfftWin = new QLineEdit("30000"));
    paramLayout->addWidget(gFreq);

    // 【新增 UI】：空间谱方位检测的参数面板
    QGroupBox* gAzDet = new QGroupBox("空间谱方位寻峰门限", paramContainer);
    QFormLayout* fAzDet = new QFormLayout(gAzDet);
    fAzDet->addRow("背景噪声容限乘子:", m_editAzDetBgMult = new QLineEdit("5.0"));
    fAzDet->addRow("旁瓣抑制比 (线性):", m_editAzDetSidelobeRatio = new QLineEdit("0.02"));
    fAzDet->addRow("寻峰最小点距:", m_editAzDetPeakMinDist = new QLineEdit("10"));
    paramLayout->addWidget(gAzDet);

    QGroupBox* gLofarExt = new QGroupBox("实时 LOFAR 线谱提取", paramContainer);
    QFormLayout* fLofarExt = new QFormLayout(gLofarExt);
    fLofarExt->addRow("背景估计中值窗宽:", m_editLofarBgMedWindow = new QLineEdit("150"));
    fLofarExt->addRow("SNR 阈值乘数:", m_editLofarSnrThreshMult = new QLineEdit("2.5"));
    fLofarExt->addRow("寻峰最小点数间距:", m_editLofarPeakMinDist = new QLineEdit("30"));
    paramLayout->addWidget(gLofarExt);

    QGroupBox* gDemon = new QGroupBox("DEMON 包络数字滤波", paramContainer);
    QFormLayout* fDemon = new QFormLayout(gDemon);
    fDemon->addRow("FIR 滤波器阶数:", m_editFirOrder = new QLineEdit("64"));
    fDemon->addRow("归一化截止频率:", m_editFirCutoff = new QLineEdit("0.1"));
    paramLayout->addWidget(gDemon);

    QGroupBox* gDp = new QGroupBox("TPSW 与 DP 轨迹寻优", paramContainer);
    QFormLayout* fDp = new QFormLayout(gDp);
    fDp->addRow("TPSW 保护窗 (G):", m_editTpswG = new QLineEdit("45"));
    fDp->addRow("TPSW 排除窗 (E):", m_editTpswE = new QLineEdit("5"));
    fDp->addRow("TPSW 补偿因子 (C):", m_editTpswC = new QLineEdit("1.15"));
    fDp->addRow("DP 记忆窗长 (L):", m_editDpL = new QLineEdit("11"));
    fDp->addRow("惩罚因子 Alpha:", m_editDpAlpha = new QLineEdit("0.6"));
    fDp->addRow("惩罚因子 Beta:", m_editDpBeta = new QLineEdit("1.5"));
    fDp->addRow("偏置因子 Gamma:", m_editDpGamma = new QLineEdit("0.1"));
    paramLayout->addWidget(gDp);

    QGroupBox* gDcv = new QGroupBox("高分辨反卷积 (DCV) 设置", paramContainer);
    QFormLayout* fDcv = new QFormLayout(gDcv);
    fDcv->addRow("RL 迭代次数:", m_editDcvRlIter = new QLineEdit("25"));
    paramLayout->addWidget(gDcv);

    paramScroll->setWidget(paramContainer);
    leftLayout->addWidget(paramScroll, 2);

    QGroupBox* groupLog = new QGroupBox("系统状态与终端", leftPanel);
    QVBoxLayout* logLayout = new QVBoxLayout(groupLog);
    m_lblSysInfo = new QLabel("引擎初始化完成，参数已就绪。\n等待注入探测数据...");
    m_lblSysInfo->setStyleSheet("color: #333333; font-weight: bold;");
    logLayout->addWidget(m_lblSysInfo);
    m_logConsole = new QPlainTextEdit(this); m_logConsole->setReadOnly(true);
    m_logConsole->setStyleSheet("background-color: #1e1e1e; color: #00ff00; font-family: Consolas;");
    logLayout->addWidget(m_logConsole);
    leftLayout->addWidget(groupLog, 1);

    topLayout->addWidget(leftPanel);

    m_mainTabWidget = new QTabWidget(topWidget);
    topLayout->addWidget(m_mainTabWidget, 1);

    QWidget* tab1 = new QWidget();
    QHBoxLayout* tab1Layout = new QHBoxLayout(tab1);
    QSplitter* horizontalSplitter = new QSplitter(Qt::Horizontal, tab1);

    QWidget* midPanel = new QWidget(horizontalSplitter);
    QVBoxLayout* midLayout = new QVBoxLayout(midPanel);
    m_timeAzimuthPlot = new QCustomPlot(midPanel);
    setupPlotInteraction(m_timeAzimuthPlot);
    m_timeAzimuthPlot->addGraph();
    m_timeAzimuthPlot->graph(0)->setLineStyle(QCPGraph::lsNone);
    m_timeAzimuthPlot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, Qt::red, Qt::black, 7));
    m_timeAzimuthPlot->plotLayout()->insertRow(0);
    m_timeAzimuthPlot->plotLayout()->addElement(0, 0, new QCPTextElement(m_timeAzimuthPlot, "宽带实时方位检测提取结果", QFont("sans", 12, QFont::Bold)));
    m_timeAzimuthPlot->xAxis->setLabel("方位角/°"); m_timeAzimuthPlot->yAxis->setLabel("物理时间/s");
    m_timeAzimuthPlot->xAxis->setRange(0, 180);
    m_timeAzimuthPlot->yAxis->setRangeReversed(true);
    midLayout->addWidget(m_timeAzimuthPlot);

    QWidget* rightPanel = new QWidget(horizontalSplitter);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);

    m_spatialPlot = new QCustomPlot(rightPanel);
    setupPlotInteraction(m_spatialPlot);
    m_spatialPlot->setMinimumHeight(250); m_spatialPlot->setMaximumHeight(350);
    m_spatialPlot->addGraph(); m_spatialPlot->graph(0)->setName("CBF (常规波束)"); m_spatialPlot->graph(0)->setPen(QPen(Qt::gray, 2, Qt::DashLine));
    m_spatialPlot->addGraph(); m_spatialPlot->graph(1)->setName("DCV (高分辨)"); m_spatialPlot->graph(1)->setPen(QPen(Qt::blue, 2));
    m_spatialPlot->plotLayout()->insertRow(0);
    m_plotTitle = new QCPTextElement(m_spatialPlot, "宽带空间谱实时折线图", QFont("sans", 12, QFont::Bold));
    m_spatialPlot->plotLayout()->addElement(0, 0, m_plotTitle);
    m_spatialPlot->xAxis->setLabel("方位角/°"); m_spatialPlot->yAxis->setLabel("归一化功率/dB");
    m_spatialPlot->xAxis->setRange(0, 180); m_spatialPlot->yAxis->setRange(-40, 5); m_spatialPlot->legend->setVisible(true);
    rightLayout->addWidget(m_spatialPlot);

    QScrollArea* scrollArea = new QScrollArea(rightPanel);
    scrollArea->setWidgetResizable(true);
    m_targetPanelWidget = new QWidget(scrollArea);
    m_targetLayout = new QGridLayout(m_targetPanelWidget);
    m_targetLayout->setAlignment(Qt::AlignTop);
    scrollArea->setWidget(m_targetPanelWidget);
    rightLayout->addWidget(scrollArea, 1);

    horizontalSplitter->addWidget(midPanel);
    horizontalSplitter->addWidget(rightPanel);
    horizontalSplitter->setStretchFactor(0, 1);
    horizontalSplitter->setStretchFactor(1, 3);
    tab1Layout->addWidget(horizontalSplitter);
    m_mainTabWidget->addTab(tab1, "💻 实时探测与关联");

    QWidget* tab2 = new QWidget();
    m_tab2Layout = new QVBoxLayout(tab2);
    m_dcvWaterfallPlot = new QCustomPlot(tab2);
    setupPlotInteraction(m_dcvWaterfallPlot);
    m_dcvWaterfallPlot->plotLayout()->insertRow(0);
    m_dcvWaterfallPlot->plotLayout()->addElement(0, 0, new QCPTextElement(m_dcvWaterfallPlot, "高分辨反卷积(DCV) 全方位时空谱历程", QFont("sans", 14, QFont::Bold)));
    m_tab2Layout->addWidget(m_dcvWaterfallPlot);
    m_mainTabWidget->addTab(tab2, "📊 后处理: 空间方位谱全景");

    QWidget* tab3 = new QWidget();
    QVBoxLayout* tab3Layout = new QVBoxLayout(tab3);
    QScrollArea* lofarScroll = new QScrollArea(tab3);
    lofarScroll->setWidgetResizable(true);
    m_lofarWaterfallWidget = new QWidget(lofarScroll);
    m_lofarWaterfallLayout = new QGridLayout(m_lofarWaterfallWidget);
    m_lofarWaterfallLayout->setAlignment(Qt::AlignTop);
    lofarScroll->setWidget(m_lofarWaterfallWidget);
    tab3Layout->addWidget(lofarScroll);
    m_mainTabWidget->addTab(tab3, "📉 后处理: 深度解耦与DP特征提取");

    verticalSplitter->addWidget(topWidget);

    QGroupBox* groupReport = new QGroupBox("综合处理评估报告终端", verticalSplitter);
    QVBoxLayout* reportLayout = new QVBoxLayout(groupReport);
    m_reportConsole = new QPlainTextEdit(this);
    m_reportConsole->setReadOnly(true);
    m_reportConsole->setStyleSheet("background-color: #2b2b2b; color: #ffaa00; font-family: Consolas; font-size: 13px;");
    reportLayout->addWidget(m_reportConsole);

    verticalSplitter->addWidget(groupReport);
    verticalSplitter->setStretchFactor(0, 4);
    verticalSplitter->setStretchFactor(1, 1);

    mainVLayout->addWidget(verticalSplitter);

    setCentralWidget(centralWidget);
    resize(1600, 1000);
    setWindowTitle("SonarTracker715 - 宽带方位动态跟踪与解耦系统");
}

void MainWindow::onSelectFilesClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择数据根目录", "");
    if (dir.isEmpty()) return;
    m_currentDir = dir;
    m_lblSysInfo->setText(QString("状态: 就绪\n目录: %1").arg(dir));
    appendLog(QString("已选择目录: %1\n请点击【开始处理】...\n").arg(dir));
    m_btnStart->setEnabled(true);
}

void MainWindow::onLoadTruthClicked() {
    QString fileName = QFileDialog::getOpenFileName(this, "选择先验真值文件", "", "JSON Files (*.json);;All Files (*)");
    if (!fileName.isEmpty()) {
        m_validator->loadTruthData(fileName);
        appendLog(QString("\n>> 已成功加载先验真值配置: %1\n").arg(fileName));
    }
}

void MainWindow::onStartClicked() {
    if (m_currentDir.isEmpty()) return;

    m_currentConfig.fs = m_editFs->text().toDouble();
    m_currentConfig.M = m_editM->text().toInt();
    m_currentConfig.d = m_editD->text().toDouble();
    m_currentConfig.c = m_editC->text().toDouble();
    m_currentConfig.r_scan = m_editRScan->text().toDouble();
    m_currentConfig.timeStep = m_editTimeStep->text().toDouble();

    m_currentConfig.lofarMin = m_editLofarMin->text().toDouble();
    m_currentConfig.lofarMax = m_editLofarMax->text().toDouble();
    m_currentConfig.demonMin = m_editDemonMin->text().toDouble();
    m_currentConfig.demonMax = m_editDemonMax->text().toDouble();
    m_currentConfig.nfftR = m_editNfftR->text().toInt();
    m_currentConfig.nfftWin = m_editNfftWin->text().toInt();

    // 【新增】：读取空间谱检测参数
    m_currentConfig.azDetBgMult = m_editAzDetBgMult->text().toDouble();
    m_currentConfig.azDetSidelobeRatio = m_editAzDetSidelobeRatio->text().toDouble();
    m_currentConfig.azDetPeakMinDist = m_editAzDetPeakMinDist->text().toInt();

    m_currentConfig.lofarBgMedWindow = m_editLofarBgMedWindow->text().toInt();
    m_currentConfig.lofarSnrThreshMult = m_editLofarSnrThreshMult->text().toDouble();
    m_currentConfig.lofarPeakMinDist = m_editLofarPeakMinDist->text().toInt();

    m_currentConfig.firOrder = m_editFirOrder->text().toInt();
    m_currentConfig.firCutoff = m_editFirCutoff->text().toDouble();

    m_currentConfig.tpswG = m_editTpswG->text().toDouble();
    m_currentConfig.tpswE = m_editTpswE->text().toDouble();
    m_currentConfig.tpswC = m_editTpswC->text().toDouble();
    m_currentConfig.dpL = m_editDpL->text().toInt();
    m_currentConfig.dpAlpha = m_editDpAlpha->text().toDouble();
    m_currentConfig.dpBeta = m_editDpBeta->text().toDouble();
    m_currentConfig.dpGamma = m_editDpGamma->text().toDouble();

    m_currentConfig.dcvRlIter = m_editDcvRlIter->text().toInt();

    m_btnStart->setEnabled(false); m_btnSelectFiles->setEnabled(false); m_btnLoadTruth->setEnabled(false);
    m_btnPauseResume->setEnabled(true); m_btnStop->setEnabled(true);
    m_mainTabWidget->setCurrentIndex(0);
    m_lblSysInfo->setText(QString("状态: 运行中\n开始时间: %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));

    m_historyResults.clear();
    m_timeAzimuthPlot->graph(0)->data()->clear();
    m_reportConsole->clear();
    m_logConsole->clear();

    QLayoutItem* item;
    while ((item = m_targetLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }
    m_lsPlots.clear(); m_lofarPlots.clear(); m_demonPlots.clear();

    if (m_dcvWaterfallPlot) { m_dcvWaterfallPlot->clearPlottables(); m_dcvWaterfallPlot->replot(); }

    while ((item = m_lofarWaterfallLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    m_worker->setDirectory(m_currentDir);
    m_worker->setConfig(m_currentConfig);
    m_worker->start();
}

void MainWindow::onPauseResumeClicked() {
    if (m_worker->isRunning()) {
        if (m_worker->isPaused()) {
            m_worker->resume(); m_lblSysInfo->setText("状态: 运行中 (恢复)"); appendLog("\n>> 系统恢复处理...\n");
        } else {
            m_worker->pause(); m_lblSysInfo->setText("状态: 已挂起 (暂停)"); appendLog("\n>> 系统已暂停处理...\n");
        }
    }
}

void MainWindow::onStopClicked() {
    if (m_worker->isRunning()) {
        m_worker->stop(); m_lblSysInfo->setText("状态: 已手动终止"); appendLog("\n>> 接收到终止指令...\n");
        m_btnStart->setEnabled(true); m_btnSelectFiles->setEnabled(true); m_btnLoadTruth->setEnabled(true);
        m_btnPauseResume->setEnabled(false); m_btnStop->setEnabled(false);
    }
}

void MainWindow::onExportClicked() {
    if (m_reportConsole->toPlainText().isEmpty() && m_logConsole->toPlainText().isEmpty()) {
        QMessageBox::warning(this, "导出失败", "当前没有可导出的报表或日志数据！");
        return;
    }

    QString defaultFileName = QString("SonarReport_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString fileName = QFileDialog::getSaveFileName(this, "保存报表结果", defaultFileName, "Text Files (*.txt);;All Files (*)");

    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", "无法创建或打开文件以写入！");
        return;
    }

    QTextStream out(&file);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out.setGenerateByteOrderMark(true);

    out << "======================================================\n";
    out << QString("         SonarTracker715 综合分析导出报表\n");
    out << QString("         导出时间: ") << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
    out << "======================================================\n\n";

    out << QString("【一、综合评估终端结果】\n");
    out << m_reportConsole->toPlainText() << "\n\n";

    out << "======================================================\n";
    out << QString("【二、系统运行实时追踪流水日志】\n");
    out << m_logConsole->toPlainText() << "\n";

    file.close();

    appendLog(QString("\n>> 成功：分析报表已完整导出至 %1\n").arg(fileName));
    QMessageBox::information(this, "导出成功", "综合评估报表及运行日志已成功导出！\n(注：图表图片请在对应图表上右键保存)");
}

void MainWindow::createTargetPlots(int targetId) {
    QCustomPlot* lsPlot = new QCustomPlot(this);
    setupPlotInteraction(lsPlot);
    lsPlot->setMinimumHeight(200); lsPlot->addGraph(); lsPlot->graph(0)->setPen(QPen(Qt::red, 1.5));
    lsPlot->xAxis->setLabel("频率/Hz"); lsPlot->yAxis->setLabel("功率/dB");
    lsPlot->xAxis->setRange(m_currentConfig.lofarMin, m_currentConfig.lofarMax); lsPlot->yAxis->setRange(-60, 40);
    lsPlot->plotLayout()->insertRow(0); lsPlot->plotLayout()->addElement(0, 0, new QCPTextElement(lsPlot, "", QFont("sans", 9, QFont::Bold)));

    QCustomPlot* lofarPlot = new QCustomPlot(this);
    setupPlotInteraction(lofarPlot);
    lofarPlot->setMinimumHeight(200); lofarPlot->addGraph(); lofarPlot->graph(0)->setPen(QPen(Qt::blue, 1.5));
    lofarPlot->xAxis->setLabel("频率/Hz"); lofarPlot->yAxis->setLabel("功率/dB");
    lofarPlot->xAxis->setRange(m_currentConfig.lofarMin, m_currentConfig.lofarMax); lofarPlot->yAxis->setRange(-60, 40);
    lofarPlot->plotLayout()->insertRow(0); lofarPlot->plotLayout()->addElement(0, 0, new QCPTextElement(lofarPlot, "", QFont("sans", 9, QFont::Bold)));

    QCustomPlot* demonPlot = new QCustomPlot(this);
    setupPlotInteraction(demonPlot);
    demonPlot->setMinimumHeight(200); demonPlot->addGraph(); demonPlot->graph(0)->setPen(QPen(Qt::darkGreen, 1.5));
    demonPlot->xAxis->setLabel("频率/Hz"); demonPlot->yAxis->setLabel("归一幅度");
    demonPlot->xAxis->setRange(0, 100); demonPlot->yAxis->setRange(0, 1.1);
    demonPlot->plotLayout()->insertRow(0); demonPlot->plotLayout()->addElement(0, 0, new QCPTextElement(demonPlot, "", QFont("sans", 9, QFont::Bold)));

    m_lsPlots.insert(targetId, lsPlot); m_lofarPlots.insert(targetId, lofarPlot); m_demonPlots.insert(targetId, demonPlot);
    int col = targetId - 1;
    m_targetLayout->addWidget(lsPlot, 0, col); m_targetLayout->addWidget(lofarPlot, 1, col); m_targetLayout->addWidget(demonPlot, 2, col);
}

void MainWindow::onFrameProcessed(const FrameResult& result) {
    m_historyResults.append(result);

    m_spatialPlot->graph(0)->setData(result.thetaAxis, result.cbfData);
    m_spatialPlot->graph(1)->setData(result.thetaAxis, result.dcvData);
    m_plotTitle->setText(QString("宽带空间谱实时折线图 (第%1帧 | 时间: %2s)").arg(result.frameIndex).arg(result.timestamp));
    m_spatialPlot->replot();
    updatePlotOriginalRange(m_spatialPlot);

    for (double ang : result.detectedAngles) m_timeAzimuthPlot->graph(0)->addData(ang, result.timestamp);
    m_timeAzimuthPlot->yAxis->setRange(std::max(0.0, result.timestamp - 30.0), result.timestamp + 5.0);
    m_timeAzimuthPlot->replot();
    updatePlotOriginalRange(m_timeAzimuthPlot);

    for (const TargetTrack& t : result.tracks) {
        if (!m_lofarPlots.contains(t.id)) createTargetPlots(t.id);

        QCustomPlot* lsp = m_lsPlots[t.id]; QCustomPlot* lp = m_lofarPlots[t.id]; QCustomPlot* dp = m_demonPlots[t.id];
        QString statusStr = t.isActive ? "[跟踪中]" : "[已熄火]";
        QColor lsColor = t.isActive ? Qt::red : Qt::darkGray; QColor lofarColor = t.isActive ? Qt::blue : Qt::darkGray; QColor demonColor = t.isActive ? Qt::darkGreen : Qt::darkGray;
        QColor bgColor = t.isActive ? Qt::white : QColor(240, 240, 240); QColor textColor = t.isActive ? Qt::black : Qt::gray;

        lsp->setBackground(bgColor); lp->setBackground(bgColor); dp->setBackground(bgColor);

        QString t1 = QString("目标%1 (方位: %2°) 拾取线谱 (第%3帧)").arg(t.id).arg(t.currentAngle, 0, 'f', 1).arg(result.frameIndex);
        QString t2 = QString("目标%1 (方位: %2°) LOFAR %3").arg(t.id).arg(t.currentAngle, 0, 'f', 1).arg(statusStr);
        QString t3 = t.isActive ? QString("目标%1 (方位: %2°) 轴频: %3Hz").arg(t.id).arg(t.currentAngle, 0, 'f', 1).arg(t.shaftFreq, 0, 'f', 1)
                                : QString("目标%1 (方位: %2°) 轴频: --Hz").arg(t.id).arg(t.currentAngle, 0, 'f', 1);

        if (auto* title = qobject_cast<QCPTextElement*>(lsp->plotLayout()->element(0, 0))) { title->setText(t1); title->setTextColor(textColor); }
        if (auto* title = qobject_cast<QCPTextElement*>(lp->plotLayout()->element(0, 0))) { title->setText(t2); title->setTextColor(textColor); }
        if (auto* title = qobject_cast<QCPTextElement*>(dp->plotLayout()->element(0, 0))) { title->setText(t3); title->setTextColor(textColor); }

        if (!t.lofarSpectrum.isEmpty()) {
            QVector<double> f_lofar(t.lofarSpectrum.size());
            for(int i=0; i<f_lofar.size(); ++i) f_lofar[i] = m_currentConfig.lofarMin + i * ((m_currentConfig.lofarMax - m_currentConfig.lofarMin) / f_lofar.size());

            if (!t.lineSpectrumAmp.isEmpty()) {
                lsp->graph(0)->setData(f_lofar, t.lineSpectrumAmp);
                lsp->graph(0)->setPen(QPen(lsColor, 1.5));
                lsp->yAxis->rescale();
                lsp->yAxis->setRange(lsp->yAxis->range().lower - 5, lsp->yAxis->range().upper + 5);
            }
            lp->graph(0)->setData(f_lofar, t.lofarSpectrum);
            lp->graph(0)->setPen(QPen(lofarColor, 1.5));
            lp->yAxis->rescale();
            lp->yAxis->setRange(lp->yAxis->range().lower - 5, lp->yAxis->range().upper + 5);

            lsp->replot();
            lp->replot();
        }
        if (!t.demonSpectrum.isEmpty()) {
            QVector<double> f_demon(t.demonSpectrum.size());
            for(int i=0; i<f_demon.size(); ++i) f_demon[i] = (i + 1) * (m_currentConfig.fs / m_currentConfig.nfftWin);
            dp->graph(0)->setData(f_demon, t.demonSpectrum); dp->graph(0)->setPen(QPen(demonColor, 1.5)); dp->replot();
        }

        updatePlotOriginalRange(lsp);
        updatePlotOriginalRange(lp);
        updatePlotOriginalRange(dp);
    }
}

void MainWindow::appendLog(const QString& log) { m_logConsole->appendPlainText(log); m_logConsole->moveCursor(QTextCursor::End); }
void MainWindow::appendReport(const QString& report) { m_reportConsole->appendPlainText(report); m_reportConsole->moveCursor(QTextCursor::End); }

void MainWindow::onOfflineResultsReady(const QList<OfflineTargetResult>& results) {
    if (results.isEmpty()) return;

    int col = 0;
    for (const auto& res : results) {
        QCustomPlot* pRaw = new QCustomPlot(m_lofarWaterfallWidget);
        setupPlotInteraction(pRaw);
        pRaw->setMinimumSize(400, 250); m_lofarWaterfallLayout->addWidget(pRaw, 0, col);
        pRaw->plotLayout()->insertRow(0);
        pRaw->plotLayout()->addElement(0, 0, new QCPTextElement(pRaw, QString("目标%1 原始LOFAR谱 (自适应局部放大)").arg(res.targetId), QFont("sans", 10, QFont::Bold)));
        QCPColorMap *cmapRaw = new QCPColorMap(pRaw->xAxis, pRaw->yAxis);
        cmapRaw->data()->setSize(res.freqBins, res.timeFrames); cmapRaw->data()->setRange(QCPRange(0, m_currentConfig.fs/2.0), QCPRange(res.minTime, res.maxTime));
        double rmax = -999; for(double v : res.rawLofarDb) if(v > rmax) rmax = v;
        for(int t=0; t<res.timeFrames; ++t) for(int f=0; f<res.freqBins; ++f) cmapRaw->data()->setCell(f, t, res.rawLofarDb[t * res.freqBins + f] - rmax);
        cmapRaw->setGradient(QCPColorGradient::gpJet); cmapRaw->setInterpolate(true);
        cmapRaw->setDataRange(QCPRange(-40.0, 0)); cmapRaw->setTightBoundary(true);
        pRaw->xAxis->setLabel("频率/Hz"); pRaw->yAxis->setLabel("物理时间/s");
        pRaw->xAxis->setRange(res.displayFreqMin, res.displayFreqMax); pRaw->yAxis->setRange(res.minTime, res.maxTime);
        updatePlotOriginalRange(pRaw);

        QCustomPlot* pTpsw = new QCustomPlot(m_lofarWaterfallWidget);
        setupPlotInteraction(pTpsw);
        pTpsw->setMinimumSize(400, 250); m_lofarWaterfallLayout->addWidget(pTpsw, 1, col);
        pTpsw->plotLayout()->insertRow(0); pTpsw->plotLayout()->addElement(0, 0, new QCPTextElement(pTpsw, "历史LOFAR谱 (TPSW背景均衡)", QFont("sans", 10, QFont::Bold)));
        QCPColorMap *cmapTpsw = new QCPColorMap(pTpsw->xAxis, pTpsw->yAxis);
        cmapTpsw->data()->setSize(res.freqBins, res.timeFrames); cmapTpsw->data()->setRange(QCPRange(0, m_currentConfig.fs/2.0), QCPRange(res.minTime, res.maxTime));
        double tmax = -999; for(double v : res.tpswLofarDb) if(v > tmax) tmax = v;
        for(int t=0; t<res.timeFrames; ++t) for(int f=0; f<res.freqBins; ++f) cmapTpsw->data()->setCell(f, t, res.tpswLofarDb[t * res.freqBins + f] - tmax);
        cmapTpsw->setGradient(QCPColorGradient::gpJet); cmapTpsw->setInterpolate(true);
        cmapTpsw->setDataRange(QCPRange(-15.0, 0)); cmapTpsw->setTightBoundary(true);
        pTpsw->xAxis->setLabel("频率/Hz"); pTpsw->yAxis->setLabel("物理时间/s");
        pTpsw->xAxis->setRange(res.displayFreqMin, res.displayFreqMax); pTpsw->yAxis->setRange(res.minTime, res.maxTime);
        updatePlotOriginalRange(pTpsw);

        QCustomPlot* pDp = new QCustomPlot(m_lofarWaterfallWidget);
        setupPlotInteraction(pDp);
        pDp->setMinimumSize(400, 250); m_lofarWaterfallLayout->addWidget(pDp, 2, col);
        pDp->plotLayout()->insertRow(0); pDp->plotLayout()->addElement(0, 0, new QCPTextElement(pDp, "专属线谱连续轨迹图 (DP寻优)", QFont("sans", 10, QFont::Bold)));
        QCPColorMap *cmapDp = new QCPColorMap(pDp->xAxis, pDp->yAxis);
        cmapDp->data()->setSize(res.freqBins, res.timeFrames); cmapDp->data()->setRange(QCPRange(0, m_currentConfig.fs/2.0), QCPRange(res.minTime, res.maxTime));
        for(int t=0; t<res.timeFrames; ++t) for(int f=0; f<res.freqBins; ++f) cmapDp->data()->setCell(f, t, res.dpCounter[t * res.freqBins + f]);
        cmapDp->setGradient(QCPColorGradient::gpJet); cmapDp->setInterpolate(false);
        cmapDp->setDataRange(QCPRange(0, 10)); cmapDp->setTightBoundary(true);
        pDp->xAxis->setLabel("频率/Hz"); pDp->yAxis->setLabel("物理时间/s");
        pDp->xAxis->setRange(res.displayFreqMin, res.displayFreqMax); pDp->yAxis->setRange(res.minTime, res.maxTime);
        updatePlotOriginalRange(pDp);

        col++;
    }
}

void MainWindow::onProcessingFinished() {
    m_lblSysInfo->setText(QString("状态: 分析完成\n结束时间: %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
    m_btnStart->setEnabled(true); m_btnSelectFiles->setEnabled(true); m_btnLoadTruth->setEnabled(true);
    m_btnPauseResume->setEnabled(false); m_btnStop->setEnabled(false);

    if (m_historyResults.isEmpty()) return;
    int num_frames = m_historyResults.size();
    double min_time = m_historyResults.first().timestamp;
    double max_time = m_historyResults.last().timestamp;
    if (std::abs(max_time - min_time) < 0.1) max_time = min_time + 3.0;

    m_dcvWaterfallPlot->clearPlottables();
    int nx_uniform = 361;
    QCPColorMap *colorMap = new QCPColorMap(m_dcvWaterfallPlot->xAxis, m_dcvWaterfallPlot->yAxis);
    colorMap->data()->setSize(nx_uniform, num_frames);
    colorMap->data()->setRange(QCPRange(0, 180), QCPRange(min_time, max_time));

    double dcv_max = -9999.0;
    for (int t = 0; t < num_frames; ++t) {
        const auto& frame = m_historyResults[t];
        const QVector<double>& theta_arr = frame.thetaAxis;
        const QVector<double>& dcv_arr = frame.dcvData;

        for (int x = 0; x < nx_uniform; ++x) {
            double target_theta = x * 0.5;
            double val = -120.0;
            if (theta_arr.size() > 1) {
                if (target_theta <= theta_arr.first()) val = dcv_arr.first();
                else if (target_theta >= theta_arr.last()) val = dcv_arr.last();
                else {
                    auto it = std::lower_bound(theta_arr.begin(), theta_arr.end(), target_theta);
                    int idx = std::distance(theta_arr.begin(), it);
                    if (idx > 0 && idx < theta_arr.size()) {
                        double t1 = theta_arr[idx - 1], t2 = theta_arr[idx];
                        double v1 = dcv_arr[idx - 1], v2 = dcv_arr[idx];
                        if (t2 - t1 > 1e-6) val = v1 + (v2 - v1) * (target_theta - t1) / (t2 - t1);
                        else val = v1;
                    }
                }
            }
            colorMap->data()->setCell(x, t, val);
            if (val > dcv_max) dcv_max = val;
        }
    }

    colorMap->setGradient(QCPColorGradient::gpJet);
    colorMap->setInterpolate(true);
    colorMap->setDataRange(QCPRange(dcv_max - 35.0, dcv_max));
    colorMap->setTightBoundary(true);

    m_dcvWaterfallPlot->xAxis->setLabel("方位角/°");
    m_dcvWaterfallPlot->yAxis->setLabel("物理时间/s");
    m_dcvWaterfallPlot->xAxis->setRange(0, 180);
    m_dcvWaterfallPlot->yAxis->setRange(min_time, max_time);
    m_dcvWaterfallPlot->replot();
    updatePlotOriginalRange(m_dcvWaterfallPlot);
}
