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
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

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

    // 【新增】：绑定正确率回传信号
        connect(m_validator, &SelfValidator::batchAccuracyComputed,
                this, &MainWindow::onBatchAccuracyComputed, Qt::QueuedConnection);
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
// =========================================================================
// 【新增】：图表弹出与恢复机制
// =========================================================================



void MainWindow::popOutPlot(QCustomPlot* plot) {
    // 防止重复弹出
    if (plot->parentWidget() && plot->parentWidget()->property("isPopup").toBool()) {
        return;
    }

    PlotLayoutInfo info;
    info.originalParent = plot->parentWidget();

    // 记录图表在原有布局中的位置信息
    if (info.originalParent && info.originalParent->layout()) {
        info.originalLayout = info.originalParent->layout();

        // 尝试判断是不是 QGridLayout
        QGridLayout* gridLayout = qobject_cast<QGridLayout*>(info.originalLayout);
        if (gridLayout) {
            int rowSpan, colSpan;
            int idx = gridLayout->indexOf(plot);
            if (idx != -1) {
                gridLayout->getItemPosition(idx, &info.row, &info.col, &rowSpan, &colSpan);
            }
        } else {
            // 如果是普通的 QVBoxLayout/QHBoxLayout，记下 index
            info.index = info.originalLayout->indexOf(plot);
        }

        // 从原布局中移除图表 (不会 delete plot)
        info.originalLayout->removeWidget(plot);
    }
    plot->setParent(nullptr);

    // 创建一个独立窗口承载该图表
    QWidget* popupWindow = new QWidget();
    popupWindow->setProperty("isPopup", true);
    popupWindow->setWindowTitle("图表独立查看 (关闭或最小化即可还原)");
    popupWindow->setMinimumSize(800, 600);

    QVBoxLayout* popupLayout = new QVBoxLayout(popupWindow);
    popupLayout->setContentsMargins(0, 0, 0, 0);
    popupLayout->addWidget(plot);

    // 保存信息
    m_popupPlots.insert(popupWindow, qMakePair(plot, info));

    // 给独立窗口安装事件过滤器，以便捕获关闭和最小化事件
    popupWindow->installEventFilter(this);

    // 获取当前屏幕并居中显示
    popupWindow->setAttribute(Qt::WA_DeleteOnClose);
    popupWindow->show();
    appendLog(">> 已将图表弹出为独立窗口。\n");
}

void MainWindow::restorePlot(QWidget* popupWindow) {
    if (!m_popupPlots.contains(popupWindow)) return;

    QPair<QCustomPlot*, PlotLayoutInfo> data = m_popupPlots.take(popupWindow);
    QCustomPlot* plot = data.first;
    PlotLayoutInfo info = data.second;

    if (plot && info.originalParent && info.originalLayout) {
        // 把图表放回原布局
        plot->setParent(info.originalParent);

        QGridLayout* gridLayout = qobject_cast<QGridLayout*>(info.originalLayout);
        if (gridLayout && info.row != -1 && info.col != -1) {
            gridLayout->addWidget(plot, info.row, info.col);
        } else if (QBoxLayout* boxLayout = qobject_cast<QBoxLayout*>(info.originalLayout)) {
            if (info.index != -1) {
                boxLayout->insertWidget(info.index, plot);
            } else {
                boxLayout->addWidget(plot);
            }
        } else {
            info.originalLayout->addWidget(plot);
        }
        plot->show();
    }
    appendLog(">> 图表已恢复至主界面原始位置。\n");
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    // 检查是否是我们创建的弹窗
    QWidget* widget = qobject_cast<QWidget*>(obj);
    if (widget && widget->property("isPopup").toBool()) {

        // 当窗口被关闭时
        if (event->type() == QEvent::Close) {
            restorePlot(widget);
            // 这里不 return true，让 Qt 继续完成窗口销毁工作
        }
        // 当窗口状态改变（如最小化）时
        else if (event->type() == QEvent::WindowStateChange) {
            if (widget->isMinimized()) {
                restorePlot(widget);
                // 因为要销毁窗口，所以触发 close
                widget->close();
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
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

    // 【新增】：将弹出窗口功能加入右键菜单
    QAction* actPopOut = menu.addAction("🪟 弹出为独立窗口");

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
    } else if (selected == actPopOut) {
        // 【新增】：点击菜单项时触发弹出
        popOutPlot(plot);
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
    // 【新增】：在UI上添加批处理帧数的输入框，默认值为 10
        fArray->addRow("批处理帧数 (帧):", m_editBatchSize = new QLineEdit("10"));
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

    QGroupBox* gAzDet = new QGroupBox("空间谱方位寻峰门限", paramContainer);
    QFormLayout* fAzDet = new QFormLayout(gAzDet);
    fAzDet->addRow("背景噪声容限乘子:", m_editAzDetBgMult = new QLineEdit("8.0"));
    fAzDet->addRow("旁瓣抑制比 (线性):", m_editAzDetSidelobeRatio = new QLineEdit("0.02"));
    fAzDet->addRow("寻峰最小点距:", m_editAzDetPeakMinDist = new QLineEdit("3"));
    paramLayout->addWidget(gAzDet);

    QGroupBox* gLofarExt = new QGroupBox("实时 LOFAR 线谱提取", paramContainer);
    QFormLayout* fLofarExt = new QFormLayout(gLofarExt);
    fLofarExt->addRow("背景估计中值窗宽:", m_editLofarBgMedWindow = new QLineEdit("60"));
    fLofarExt->addRow("SNR 阈值乘数:", m_editLofarSnrThreshMult = new QLineEdit("2.0"));
    fLofarExt->addRow("寻峰最小点数间距:", m_editLofarPeakMinDist = new QLineEdit("15"));
    paramLayout->addWidget(gLofarExt);

    QGroupBox* gDemon = new QGroupBox("DEMON 包络数字滤波", paramContainer);
    QFormLayout* fDemon = new QFormLayout(gDemon);
    fDemon->addRow("FIR 滤波器阶数:", m_editFirOrder = new QLineEdit("64"));
    fDemon->addRow("归一化截止频率:", m_editFirCutoff = new QLineEdit("0.1"));
    paramLayout->addWidget(gDemon);

    QGroupBox* gDp = new QGroupBox("TPSW 与 DP 轨迹寻优", paramContainer);
    QFormLayout* fDp = new QFormLayout(gDp);
    fDp->addRow("TPSW 保护窗 (G):", m_editTpswG = new QLineEdit("45"));
    fDp->addRow("TPSW 排除窗 (E):", m_editTpswE = new QLineEdit("10"));
    fDp->addRow("TPSW 补偿因子 (C):", m_editTpswC = new QLineEdit("1.15"));
    fDp->addRow("DP 记忆窗长 (L):", m_editDpL = new QLineEdit("11"));
    fDp->addRow("惩罚因子 Alpha:", m_editDpAlpha = new QLineEdit("0.6"));
    fDp->addRow("惩罚因子 Beta:", m_editDpBeta = new QLineEdit("1.5"));
    fDp->addRow("偏置因子 Gamma:", m_editDpGamma = new QLineEdit("0.1"));
    paramLayout->addWidget(gDp);

    QGroupBox* gDcv = new QGroupBox("高分辨反卷积 (DCV) 设置", paramContainer);
    QFormLayout* fDcv = new QFormLayout(gDcv);
    fDcv->addRow("RL 迭代次数:", m_editDcvRlIter = new QLineEdit("10"));
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

    // ====================================================================
    // 【全新重构的 Tab2】：包含 CBF瀑布图、DCV瀑布图及目标切片对比
    // ====================================================================
    QWidget* tab2 = new QWidget();
    QVBoxLayout* tab2MainLayout = new QVBoxLayout(tab2);

    QScrollArea* tab2Scroll = new QScrollArea(tab2);
    tab2Scroll->setWidgetResizable(true);
    tab2Scroll->setFrameShape(QFrame::NoFrame);

    QWidget* tab2Container = new QWidget(tab2Scroll);
    QVBoxLayout* tab2ContainerLayout = new QVBoxLayout(tab2Container);

    QHBoxLayout* waterfallsLayout = new QHBoxLayout();

    m_cbfWaterfallPlot = new QCustomPlot(tab2Container);
    setupPlotInteraction(m_cbfWaterfallPlot);
    m_cbfWaterfallPlot->setMinimumHeight(400);
    m_cbfWaterfallPlot->plotLayout()->insertRow(0);
    m_cbfWaterfallPlot->plotLayout()->addElement(0, 0, new QCPTextElement(m_cbfWaterfallPlot, "常规波束形成(CBF) 空间谱历程", QFont("sans", 12, QFont::Bold)));
    waterfallsLayout->addWidget(m_cbfWaterfallPlot);

    m_dcvWaterfallPlot = new QCustomPlot(tab2Container);
    setupPlotInteraction(m_dcvWaterfallPlot);
    m_dcvWaterfallPlot->setMinimumHeight(400);
    m_dcvWaterfallPlot->plotLayout()->insertRow(0);
    m_dcvWaterfallPlot->plotLayout()->addElement(0, 0, new QCPTextElement(m_dcvWaterfallPlot, "高分辨反卷积(DCV) 全方位时空谱历程", QFont("sans", 12, QFont::Bold)));
    waterfallsLayout->addWidget(m_dcvWaterfallPlot);

    tab2ContainerLayout->addLayout(waterfallsLayout);

    m_sliceWidget = new QWidget(tab2Container);
    m_sliceLayout = new QGridLayout(m_sliceWidget);
    m_sliceLayout->setAlignment(Qt::AlignTop);
    tab2ContainerLayout->addWidget(m_sliceWidget);

    tab2Scroll->setWidget(tab2Container);
    tab2MainLayout->addWidget(tab2Scroll);
    m_mainTabWidget->addTab(tab2, "📊 后处理: 空间方位谱全景与切片");
    // ====================================================================

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
    // 【新增】：读取界面上设置的批处理帧数
        m_currentConfig.batchSize = m_editBatchSize->text().toInt();
    m_currentConfig.lofarMin = m_editLofarMin->text().toDouble();
    m_currentConfig.lofarMax = m_editLofarMax->text().toDouble();
    m_currentConfig.demonMin = m_editDemonMin->text().toDouble();
    m_currentConfig.demonMax = m_editDemonMax->text().toDouble();
    m_currentConfig.nfftR = m_editNfftR->text().toInt();
    m_currentConfig.nfftWin = m_editNfftWin->text().toInt();

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
    m_batchAccuracies.clear(); // 【新增】：清空过往批次的正确率缓存
    m_timeAzimuthPlot->graph(0)->data()->clear();
    m_reportConsole->clear();
    m_logConsole->clear();

    QLayoutItem* item;
    while ((item = m_targetLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }
    m_lsPlots.clear(); m_lofarPlots.clear(); m_demonPlots.clear();

    // 【新增】：一并清空 Tab2 的图表状态
    if (m_cbfWaterfallPlot) { m_cbfWaterfallPlot->clearPlottables(); m_cbfWaterfallPlot->replot(); }
    if (m_dcvWaterfallPlot) { m_dcvWaterfallPlot->clearPlottables(); m_dcvWaterfallPlot->replot(); }
    if (m_sliceLayout) {
        while ((item = m_sliceLayout->takeAt(0)) != nullptr) {
            if (item->widget()) delete item->widget();
            delete item;
        }
    }

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

    // =================================================================
    // 【核心新增】：导出当前绘制的所有图片到配套的文件夹中
    // =================================================================
    QFileInfo fileInfo(fileName);
    // 生成与 txt 同级配套的文件夹路径
    QString plotsDirPath = fileInfo.absolutePath() + "/" + fileInfo.completeBaseName() + "_Plots";
    QDir dir;
    if (!dir.exists(plotsDirPath)) {
        dir.mkpath(plotsDirPath);
    }

    // 定义一个 Lambda 闭包函数：智能提取图表标题并保存
    auto savePlot = [&](QCustomPlot* plot, const QString& defaultName) {
        if (!plot) return;
        QString title = defaultName;
        // 尝试从图表上方提取文本元素（标题）
        if (plot->plotLayout()->rowCount() > 0 && plot->plotLayout()->columnCount() > 0) {
            if (auto* textElement = qobject_cast<QCPTextElement*>(plot->plotLayout()->element(0, 0))) {
                if (!textElement->text().isEmpty()) {
                    title = textElement->text();
                }
            }
        }
        // 替换 Windows/Linux 文件系统不支持的非法字符以及换行符，防止保存失败
        title.replace(QRegularExpression("[\\\\/:*?\"<>|\\n]"), "_");

        QString imgPath = plotsDirPath + "/" + title + ".png";
        // 以控件当前的实际分辨率进行保存，保底为 800x600
        int w = plot->width() > 0 ? plot->width() : 800;
        int h = plot->height() > 0 ? plot->height() : 600;
        plot->savePng(imgPath, w, h);
    };

    // 1. 保存 Tab1 实时探测界面的固定图表
    savePlot(m_timeAzimuthPlot, "TimeAzimuthPlot");
    savePlot(m_spatialPlot, "SpatialPlot");

    // 2. 保存 Tab1 目标实时跟踪子图表 (遍历 QMap)
    for (int tid : m_lsPlots.keys()) {
        savePlot(m_lsPlots[tid], QString("Target_%1_LS").arg(tid));
        savePlot(m_lofarPlots[tid], QString("Target_%1_LOFAR").arg(tid));
        savePlot(m_demonPlots[tid], QString("Target_%1_DEMON").arg(tid));
    }

    // 3. 保存 Tab2 后处理全景瀑布图
    savePlot(m_cbfWaterfallPlot, "CBF_Waterfall");
    savePlot(m_dcvWaterfallPlot, "DCV_Waterfall");

    // 4. 定义辅助函数：遍历网格布局并保存所有其中的 QCustomPlot
    auto saveLayoutPlots = [&](QLayout* layout, const QString& fallbackPrefix) {
        if (!layout) return;
        for (int i = 0; i < layout->count(); ++i) {
            if (QWidget* w = layout->itemAt(i)->widget()) {
                if (QCustomPlot* cp = qobject_cast<QCustomPlot*>(w)) {
                    savePlot(cp, QString("%1_%2").arg(fallbackPrefix).arg(i));
                }
            }
        }
    };

    // 5. 保存 Tab2 中的各目标切片对比图
    saveLayoutPlots(m_sliceLayout, "TargetSlice");

    // 6. 保存 Tab3 中的历史累积 LOFAR 瀑布图与 DP 寻优图
    saveLayoutPlots(m_lofarWaterfallLayout, "OfflineLofar");

    appendLog(QString("\n>> 成功：分析报表已完整导出至 %1\n").arg(fileName));
    appendLog(QString(">> 成功：所有配套图表已导出至文件夹 %1\n").arg(plotsDirPath));

    QMessageBox::information(this, "导出成功",
                             QString("综合评估报表及运行日志已成功导出！\n\n此外，当前所有图表（共计几十张）也已自动保存为PNG图片，位于同级配套目录：\n%1").arg(plotsDirPath));
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
    (m_spatialPlot);

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
void MainWindow::appendReport(const QString& report) {
    QString finalReport = report;

    // 【核心新增】：如果是最终报告，替换掉里面的占位符生成表格
    if (finalReport.contains("[BATCH_ACCURACY_TABLE_PLACEHOLDER]")) {
        QString table = "======================================================\n";
        table += "             各批次综合识别正确率汇总表             \n";
        table += "======================================================\n";
        table += "| 批次号 | 识别正确率 |\n";
        table += "|--------|------------|\n";
        for (const auto& pair : m_batchAccuracies) {
            table += QString("| 第 %1 批 | %2% |\n").arg(pair.first, -4).arg(pair.second, 8, 'f', 2);
        }
        if (m_batchAccuracies.isEmpty()) {
            table += "| 无数据 |    ---     |\n";
        }
        table += "======================================================\n";

        finalReport.replace("[BATCH_ACCURACY_TABLE_PLACEHOLDER]", table);
    }

    m_reportConsole->appendPlainText(finalReport);
    m_reportConsole->moveCursor(QTextCursor::End);
}
void MainWindow::onOfflineResultsReady(const QList<OfflineTargetResult>& results) {
    if (results.isEmpty()) return;

    QLayoutItem* item;
    while ((item = m_lofarWaterfallLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    int col = 0;
    for (const auto& res : results) {
        QCustomPlot* pRaw = new QCustomPlot(m_lofarWaterfallWidget);
        setupPlotInteraction(pRaw);
        pRaw->setMinimumSize(400, 250); m_lofarWaterfallLayout->addWidget(pRaw, 0, col);
        pRaw->plotLayout()->insertRow(0);
        pRaw->plotLayout()->addElement(0, 0, new QCPTextElement(pRaw, QString("目标%1 原始LOFAR谱 (随批次积累)").arg(res.targetId), QFont("sans", 10, QFont::Bold)));
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

    // 【修改】：调用重构后的全新 Tab2 绘制函数
    updateTab2Plots();
}

// 【修改】：完全重写的 Tab2 联合绘图函数 (包含两张瀑布图与各目标对比切片)
void MainWindow::updateTab2Plots() {
    if (m_historyResults.isEmpty()) return;
    int num_frames = m_historyResults.size();
    double min_time = m_historyResults.first().timestamp;
    double max_time = m_historyResults.last().timestamp;
    if (std::abs(max_time - min_time) < 0.1) max_time = min_time + 3.0;

    // ------------------------------------------------------------------
    // 1. 绘制 CBF 和 DCV 瀑布图
    // ------------------------------------------------------------------
    m_cbfWaterfallPlot->clearPlottables();
    m_dcvWaterfallPlot->clearPlottables();

    int nx_uniform = 361;
    QCPColorMap *cmapCbf = new QCPColorMap(m_cbfWaterfallPlot->xAxis, m_cbfWaterfallPlot->yAxis);
    QCPColorMap *cmapDcv = new QCPColorMap(m_dcvWaterfallPlot->xAxis, m_dcvWaterfallPlot->yAxis);

    cmapCbf->data()->setSize(nx_uniform, num_frames);
    cmapCbf->data()->setRange(QCPRange(0, 180), QCPRange(min_time, max_time));
    cmapDcv->data()->setSize(nx_uniform, num_frames);
    cmapDcv->data()->setRange(QCPRange(0, 180), QCPRange(min_time, max_time));

    double cbf_max = -9999.0, dcv_max = -9999.0;

    for (int t = 0; t < num_frames; ++t) {
        const auto& frame = m_historyResults[t];
        const QVector<double>& theta_arr = frame.thetaAxis;
        const QVector<double>& cbf_arr = frame.cbfData;
        const QVector<double>& dcv_arr = frame.dcvData;

        for (int x = 0; x < nx_uniform; ++x) {
            double target_theta = x * 0.5;
            double v_cbf = -120.0, v_dcv = -120.0;

            if (theta_arr.size() > 1) {
                if (target_theta <= theta_arr.first()) { v_cbf = cbf_arr.first(); v_dcv = dcv_arr.first(); }
                else if (target_theta >= theta_arr.last()) { v_cbf = cbf_arr.last(); v_dcv = dcv_arr.last(); }
                else {
                    auto it = std::lower_bound(theta_arr.begin(), theta_arr.end(), target_theta);
                    int idx = std::distance(theta_arr.begin(), it);
                    if (idx > 0 && idx < theta_arr.size()) {
                        double t1 = theta_arr[idx - 1], t2 = theta_arr[idx];
                        double c1 = cbf_arr[idx - 1], c2 = cbf_arr[idx];
                        double d1 = dcv_arr[idx - 1], d2 = dcv_arr[idx];
                        if (t2 - t1 > 1e-6) {
                            v_cbf = c1 + (c2 - c1) * (target_theta - t1) / (t2 - t1);
                            v_dcv = d1 + (d2 - d1) * (target_theta - t1) / (t2 - t1);
                        } else {
                            v_cbf = c1; v_dcv = d1;
                        }
                    }
                }
            }
            cmapCbf->data()->setCell(x, t, v_cbf);
            cmapDcv->data()->setCell(x, t, v_dcv);
            if (v_cbf > cbf_max) cbf_max = v_cbf;
            if (v_dcv > dcv_max) dcv_max = v_dcv;
        }
    }

    cmapCbf->setGradient(QCPColorGradient::gpJet);
    cmapCbf->setInterpolate(true);
    cmapCbf->setDataRange(QCPRange(cbf_max - 20.0, cbf_max));
    cmapCbf->setTightBoundary(true);
    m_cbfWaterfallPlot->xAxis->setLabel("方位角/°");
    m_cbfWaterfallPlot->yAxis->setLabel("物理时间/s");
    m_cbfWaterfallPlot->xAxis->setRange(0, 180);
    m_cbfWaterfallPlot->yAxis->setRange(min_time, max_time);
    m_cbfWaterfallPlot->replot();
    updatePlotOriginalRange(m_cbfWaterfallPlot);

    cmapDcv->setGradient(QCPColorGradient::gpJet);
    cmapDcv->setInterpolate(true);
    cmapDcv->setDataRange(QCPRange(dcv_max - 35.0, dcv_max));
    cmapDcv->setTightBoundary(true);
    m_dcvWaterfallPlot->xAxis->setLabel("方位角/°");
    m_dcvWaterfallPlot->yAxis->setLabel("物理时间/s");
    m_dcvWaterfallPlot->xAxis->setRange(0, 180);
    m_dcvWaterfallPlot->yAxis->setRange(min_time, max_time);
    m_dcvWaterfallPlot->replot();
    updatePlotOriginalRange(m_dcvWaterfallPlot);

    // ------------------------------------------------------------------
    // 2. 生成各目标的特定方位频域切片图 (CBF vs DCV)
    // ------------------------------------------------------------------
    QLayoutItem* item;
    while ((item = m_sliceLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    QSet<int> targetIds;
    for (const auto& frame : m_historyResults) {
        for (const auto& tr : frame.tracks) {
            if (tr.isConfirmed) targetIds.insert(tr.id);
        }
    }

    QList<int> sortedIds = targetIds.values();
    std::sort(sortedIds.begin(), sortedIds.end());

    int col = 0;
    for (int tid : sortedIds) {
        int active_frames = 0;
        double sum_ang = 0.0;

        QVector<double> slice_cbf_sum;
        QVector<double> slice_dcv_sum;

        for (const auto& frame : m_historyResults) {
            for (const auto& tr : frame.tracks) {
                if (tr.id == tid && tr.isActive && !tr.lofarFullLinear.isEmpty() && !tr.cbfLofarFullLinear.isEmpty()) {
                    if (slice_dcv_sum.isEmpty()) {
                        slice_cbf_sum.resize(tr.cbfLofarFullLinear.size());
                        slice_dcv_sum.resize(tr.lofarFullLinear.size());
                        slice_cbf_sum.fill(0.0); slice_dcv_sum.fill(0.0);
                    }
                    for(int i=0; i<slice_dcv_sum.size(); ++i) {
                        slice_cbf_sum[i] += tr.cbfLofarFullLinear[i];
                        slice_dcv_sum[i] += tr.lofarFullLinear[i];
                    }
                    sum_ang += tr.currentAngle;
                    active_frames++;
                    break;
                }
            }
        }

        if (active_frames > 0 && !slice_dcv_sum.isEmpty()) {
            double avg_ang = sum_ang / active_frames;

            std::vector<double> v_cbf(slice_cbf_sum.size()), v_dcv(slice_dcv_sum.size());
            for(int i=0; i<slice_dcv_sum.size(); ++i) {
                v_cbf[i] = slice_cbf_sum[i] / active_frames;
                v_dcv[i] = slice_dcv_sum[i] / active_frames;
            }

            auto calculateMedianStd = [](std::vector<double> v) {
                if (v.empty()) return 1e-12;
                std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                return v[v.size() / 2];
            };

            double med_cbf = calculateMedianStd(v_cbf);
            double med_dcv = calculateMedianStd(v_dcv);
            double max_cbf = *std::max_element(v_cbf.begin(), v_cbf.end());
            double max_dcv = *std::max_element(v_dcv.begin(), v_dcv.end());

            double cbf_snr = 10.0 * std::log10((max_cbf + 1e-12) / (med_cbf + 1e-12));
            double dcv_snr = 10.0 * std::log10((max_dcv + 1e-12) / (med_dcv + 1e-12));
            double gain_penalty = std::max(0.0, dcv_snr - cbf_snr);

            QVector<double> f_axis(v_dcv.size());
            QVector<double> cbf_db(v_cbf.size());
            QVector<double> dcv_db(v_dcv.size());

            double df_calc = (m_currentConfig.fs / 2.0) / (v_dcv.size() - 1);
            for(int i=0; i<v_dcv.size(); ++i) {
                f_axis[i] = i * df_calc;

                double d_val = 10.0 * std::log10(v_dcv[i] / (max_dcv + 1e-12) + 1e-12);
                double c_val = 10.0 * std::log10(v_cbf[i] / (max_cbf + 1e-12) + 1e-12) - gain_penalty;

                dcv_db[i] = std::max(-80.0, d_val);
                cbf_db[i] = std::max(-80.0, c_val);
            }

            QCustomPlot* pCbf = new QCustomPlot(m_sliceWidget);
            setupPlotInteraction(pCbf);
            pCbf->setMinimumSize(400, 250);
            pCbf->addGraph(); pCbf->graph(0)->setPen(QPen(Qt::blue, 1.5));
            pCbf->graph(0)->setData(f_axis, cbf_db);
            pCbf->plotLayout()->insertRow(0);
            pCbf->plotLayout()->addElement(0, 0, new QCPTextElement(pCbf, QString("目标%1 (约 %2°) - CBF").arg(tid).arg(avg_ang, 0, 'f', 1), QFont("sans", 10, QFont::Bold)));
            pCbf->xAxis->setRange(m_currentConfig.lofarMin, m_currentConfig.lofarMax);
            pCbf->yAxis->setRange(-80, 5);
            if (col == 0) pCbf->yAxis->setLabel("功率 / dB");
            pCbf->xAxis->setVisible(false);
            m_sliceLayout->addWidget(pCbf, 0, col);
            updatePlotOriginalRange(pCbf);

            QCustomPlot* pDcv = new QCustomPlot(m_sliceWidget);
            setupPlotInteraction(pDcv);
            pDcv->setMinimumSize(400, 250);
            pDcv->addGraph(); pDcv->graph(0)->setPen(QPen(Qt::red, 1.5));
            pDcv->graph(0)->setData(f_axis, dcv_db);
            pDcv->plotLayout()->insertRow(0);
            pDcv->plotLayout()->addElement(0, 0, new QCPTextElement(pDcv, QString("目标%1 (约 %2°) - DCV").arg(tid).arg(avg_ang, 0, 'f', 1), QFont("sans", 10, QFont::Bold)));
            pDcv->xAxis->setRange(m_currentConfig.lofarMin, m_currentConfig.lofarMax);
            pDcv->yAxis->setRange(-80, 5);
            pDcv->xAxis->setLabel("频率 / Hz");
            if (col == 0) pDcv->yAxis->setLabel("功率 / dB");
            m_sliceLayout->addWidget(pDcv, 1, col);
            updatePlotOriginalRange(pDcv);

            col++;
        }
    }
}

void MainWindow::onProcessingFinished() {
    m_lblSysInfo->setText(QString("状态: 分析完成\n结束时间: %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
    m_btnStart->setEnabled(true); m_btnSelectFiles->setEnabled(true); m_btnLoadTruth->setEnabled(true);
    m_btnPauseResume->setEnabled(false); m_btnStop->setEnabled(false);

    // 保证结束时再执行一次最终绘制
    updateTab2Plots();
}
// 在文件靠下的地方加入这个函数
void MainWindow::onBatchAccuracyComputed(int batchIndex, double accuracy) {
    m_batchAccuracies.append(qMakePair(batchIndex, accuracy));
}
