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
#include <QPixmap>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QInputDialog> // 确保顶部包含此头文件
#include <QTimer> // 确保顶部包含此头文件

// ==================== 独立真值配置与导入窗口类 ====================
class TruthInputDialog : public QDialog {
    Q_OBJECT
public:
    explicit TruthInputDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("目标先验真值综合配置大厅");
        resize(750, 600);
        QVBoxLayout* mainLayout = new QVBoxLayout(this);

        // 顶部控制区
        QHBoxLayout* topLayout = new QHBoxLayout();
        QPushButton* btnAddTarget = new QPushButton(" ➕ 新增一个目标参数");
        btnAddTarget->setStyleSheet("font-weight: bold; color: #27ae60; padding: 5px 10px;");
        topLayout->addWidget(btnAddTarget);
        topLayout->addStretch();

        QPushButton* btnLoadJson = new QPushButton(" 📂 从 JSON 文件导入...");
        topLayout->addWidget(btnLoadJson);
        mainLayout->addLayout(topLayout);

        // 滚动区域存放输入卡片
        QScrollArea* scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        m_cardsContainer = new QWidget();
        m_cardsLayout = new QVBoxLayout(m_cardsContainer);
        m_cardsLayout->setAlignment(Qt::AlignTop);
        scrollArea->setWidget(m_cardsContainer);
        mainLayout->addWidget(scrollArea);

        // 底部按钮区
        QHBoxLayout* bottomLayout = new QHBoxLayout();
        QPushButton* btnSaveJson = new QPushButton(" 💾 将当前配置保存为 JSON...");
        QPushButton* btnApply = new QPushButton(" ✔️ 应用配置并关闭");
        bottomLayout->addStretch();
        bottomLayout->addWidget(btnSaveJson);
        bottomLayout->addWidget(btnApply);
        mainLayout->addLayout(bottomLayout);

        connect(btnAddTarget, &QPushButton::clicked, this, [this](){ addCard(); });
        connect(btnLoadJson, &QPushButton::clicked, this, &TruthInputDialog::loadJson);
        connect(btnSaveJson, &QPushButton::clicked, this, &TruthInputDialog::saveJson);
        connect(btnApply, &QPushButton::clicked, this, &TruthInputDialog::accept);

        // 默认初始化生成一个空卡片
        addCard();
    }

    std::vector<TargetTruth> getTruthData() const {
        std::vector<TargetTruth> data;
        int validId = 1;
        for (int i = 0; i < m_cardsLayout->count(); ++i) {
            QWidget* card = m_cardsLayout->itemAt(i)->widget();
            if (!card) continue;

            TargetTruth t;
            t.id = validId++; // 重新按顺序编排ID
            t.name = card->findChild<QLineEdit*>("name")->text();
            t.initialAngle = card->findChild<QLineEdit*>("initAngle")->text().toDouble();
            t.initialDistance = card->findChild<QLineEdit*>("initDist")->text().toDouble();
            t.speed = card->findChild<QLineEdit*>("speed")->text().toDouble();
            t.course = card->findChild<QLineEdit*>("course")->text().toDouble();
            t.trueDemonFreq = card->findChild<QLineEdit*>("demon")->text().toDouble();

            // 【新增】：读取界面的深度值
            t.trueDepth = card->findChild<QLineEdit*>("trueDepth")->text().toDouble();

            QString lofarStr = card->findChild<QLineEdit*>("lofar")->text();
            QStringList lofarList = lofarStr.split(QRegularExpression("[,，\\s]+"), Qt::SkipEmptyParts);
            for (const QString& s : lofarList) t.trueLofarFreqs.push_back(s.toDouble());
            data.push_back(t);
        }
        return data;
    }

    void populateFromData(const std::vector<TargetTruth>& data) {
        if (data.empty()) return;
        QLayoutItem* item; while ((item = m_cardsLayout->takeAt(0)) != nullptr) { if (item->widget()) delete item->widget(); delete item; }
        for (const auto& t : data) addCard(t);
    }

private:
    void addCard(const TargetTruth& t = TargetTruth()) {
        QGroupBox* group = new QGroupBox("目标参数配置卡片");
        group->setStyleSheet("QGroupBox { border: 1px solid #bdc3c7; border-radius: 5px; margin-top: 2ex; } QGroupBox::title { subcontrol-origin: margin; left: 10px; color: #2c3e50; font-weight: bold; }");
        QGridLayout* grid = new QGridLayout(group);

        QPushButton* btnDel = new QPushButton("❌ 移除此目标");
        btnDel->setStyleSheet("QPushButton { color: #e74c3c; border: none; font-weight: bold; } QPushButton:hover { color: #c0392b; text-decoration: underline; }");
        btnDel->setCursor(Qt::PointingHandCursor);
        connect(btnDel, &QPushButton::clicked, group, &QGroupBox::deleteLater);

        grid->addWidget(new QLabel("目标名称:"), 0, 0);
        QLineEdit* editName = new QLineEdit(t.name.isEmpty() ? "Target X" : t.name); editName->setObjectName("name");
        grid->addWidget(editName, 0, 1);

        grid->addWidget(new QLabel("起始方位(度):"), 0, 2);
        QLineEdit* editInitAngle = new QLineEdit(QString::number(t.initialAngle == 0 ? 90.0 : t.initialAngle, 'f', 1)); editInitAngle->setObjectName("initAngle");
        grid->addWidget(editInitAngle, 0, 3);
        grid->addWidget(btnDel, 0, 4, 1, 1, Qt::AlignRight);

        grid->addWidget(new QLabel("起始距离(m):"), 1, 0);
        QLineEdit* editInitDist = new QLineEdit(QString::number(t.initialDistance == 0 ? 20000.0 : t.initialDistance, 'f', 1)); editInitDist->setObjectName("initDist");
        grid->addWidget(editInitDist, 1, 1);

        grid->addWidget(new QLabel("运动航速(m/s):"), 1, 2);
        QLineEdit* editSpeed = new QLineEdit(QString::number(t.speed == 0 ? 5.0 : t.speed, 'f', 1)); editSpeed->setObjectName("speed");
        grid->addWidget(editSpeed, 1, 3);

        grid->addWidget(new QLabel("运动航向(度):"), 2, 0);
        QLineEdit* editCourse = new QLineEdit(QString::number(t.course == 0 ? 45.0 : t.course, 'f', 1)); editCourse->setObjectName("course");
        grid->addWidget(editCourse, 2, 1);

        grid->addWidget(new QLabel("真实轴频(Hz):"), 2, 2);
        QLineEdit* editDemon = new QLineEdit(QString::number(t.trueDemonFreq == 0 ? 3.5 : t.trueDemonFreq, 'f', 1)); editDemon->setObjectName("demon");
        grid->addWidget(editDemon, 2, 3);

        // ==============================================================
        // 【新增】：在网格第 3 行插入深度的输入框
        // ==============================================================
        grid->addWidget(new QLabel("真实深度(m):"), 3, 0);
        QLineEdit* editDepth = new QLineEdit(QString::number(t.trueDepth <= 0 ? 50.0 : t.trueDepth, 'f', 1));
        editDepth->setObjectName("trueDepth");
        grid->addWidget(editDepth, 3, 1);

        // 原本的线谱挪到第 4 行
        grid->addWidget(new QLabel("真实线谱群(Hz,逗号分隔):"), 4, 0);
        QStringList lofarList; for (double f : t.trueLofarFreqs) lofarList << QString::number(f, 'f', 1);
        QLineEdit* editLofar = new QLineEdit(lofarList.isEmpty() ? "120.0, 150.0" : lofarList.join(", ")); editLofar->setObjectName("lofar");
        grid->addWidget(editLofar, 4, 1, 1, 3);

        m_cardsLayout->addWidget(group);
    }

    void loadJson() {
        QString filePath = QFileDialog::getOpenFileName(this, "选择先验真值 JSON 文件", "", "JSON Files (*.json);;All Files (*)");
        if (filePath.isEmpty()) return;
        QFile file(filePath); if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(file.readAll()); file.close();
        if (!jsonDoc.isObject()) return;

        QJsonObject rootObj = jsonDoc.object();
        if (rootObj.contains("targets") && rootObj["targets"].isArray()) {
            QLayoutItem* item; while ((item = m_cardsLayout->takeAt(0)) != nullptr) { if (item->widget()) delete item->widget(); delete item; }
            QJsonArray targetArray = rootObj["targets"].toArray();
            for (int i = 0; i < targetArray.size(); ++i) {
                QJsonObject tObj = targetArray[i].toObject(); TargetTruth truth;
                truth.name = tObj["name"].toString();
                truth.initialAngle = tObj["initialAngle"].toDouble();
                truth.initialDistance = tObj["initialDistance"].toDouble();
                truth.speed = tObj["speed"].toDouble();
                truth.course = tObj["course"].toDouble();
                truth.trueDemonFreq = tObj["trueDemonFreq"].toDouble();

                // 【新增】：解析深度
                truth.trueDepth = tObj["trueDepth"].toDouble();

                QJsonArray lofarArr = tObj["trueLofarFreqs"].toArray();
                for (int j = 0; j < lofarArr.size(); ++j) truth.trueLofarFreqs.push_back(lofarArr[j].toDouble());
                addCard(truth);
            }
        }
    }

    void saveJson() {
        QString filePath = QFileDialog::getSaveFileName(this, "保存先验真值配置", "GroundTruth_Targets.json", "JSON Files (*.json)");
        if (filePath.isEmpty()) return;
        QJsonArray targetArray;
        for (const auto& t : getTruthData()) {
            QJsonObject tObj;
            tObj["id"] = t.id;
            tObj["name"] = t.name;
            tObj["initialAngle"] = t.initialAngle;
            tObj["initialDistance"] = t.initialDistance;
            tObj["speed"] = t.speed;
            tObj["course"] = t.course;
            tObj["trueDemonFreq"] = t.trueDemonFreq;

            // 【新增】：写入深度
            tObj["trueDepth"] = t.trueDepth;

            QJsonArray lofarArr;
            for (double f : t.trueLofarFreqs) lofarArr.append(f);
            tObj["trueLofarFreqs"] = lofarArr;
            targetArray.append(tObj);
        }
        QJsonObject rootObj; rootObj["targets"] = targetArray; QJsonDocument doc(rootObj);
        QFile file(filePath); if (file.open(QIODevice::WriteOnly | QIODevice::Text)) file.write(doc.toJson(QJsonDocument::Indented));
    }

private:
    QWidget* m_cardsContainer;
    QVBoxLayout* m_cardsLayout;
};
// ============================================================


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
    m_worker(new DspWorker(this)),
    m_validator(new SelfValidator(this))
{
    // 【核心修复 1】：注册 Qt 元类型，防止列表数据在多线程信号传递时丢失或引发异常
    qRegisterMetaType<SystemEvaluationResult>("SystemEvaluationResult");
    qRegisterMetaType<QList<TargetEvaluation>>("QList<TargetEvaluation>");

    setupUi();

    // 绑定顶部控制栏按钮
    connect(m_btnSelectFiles, &QPushButton::clicked, this, &MainWindow::onSelectFilesClicked);
    connect(m_btnManualTruth, &QPushButton::clicked, this, &MainWindow::onManualTruthClicked);
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_btnPauseResume, &QPushButton::clicked, this, &MainWindow::onPauseResumeClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(m_btnExport, &QPushButton::clicked, this, &MainWindow::onExportClicked);

    // 绑定 DspWorker 工作线程发出的实时信号
    connect(m_worker, &DspWorker::frameProcessed, this, &MainWindow::onFrameProcessed, Qt::QueuedConnection);
    connect(m_worker, &DspWorker::logReady, this, &MainWindow::appendLog, Qt::QueuedConnection);
    connect(m_worker, &DspWorker::reportReady, this, &MainWindow::appendReport, Qt::QueuedConnection);
    connect(m_worker, &DspWorker::offlineResultsReady, this, &MainWindow::onOfflineResultsReady, Qt::QueuedConnection);
    connect(m_worker, &DspWorker::processingFinished, this, &MainWindow::onProcessingFinished, Qt::QueuedConnection);

    // 绑定 DspWorker 的批处理特征提取完成信号 到 SelfValidator 进行判决
    connect(m_worker, &DspWorker::batchFinished, m_validator, &SelfValidator::onBatchFinished, Qt::QueuedConnection);

    // 绑定 SelfValidator 的批次结果输出信号 到 主界面
    connect(m_validator, &SelfValidator::validationLogReady, this, &MainWindow::appendReport, Qt::QueuedConnection);
    connect(m_validator, &SelfValidator::batchAccuracyComputed, this, &MainWindow::onBatchAccuracyComputed, Qt::QueuedConnection);

    // 绑定 DspWorker 的实时特征评估表 到 主界面 (用于刷新表一)
    connect(m_worker, &DspWorker::evaluationResultReady, this, &MainWindow::onEvaluationResultReady, Qt::QueuedConnection);

    // =========================================================
    // 【核心修复 2】：把验证器的判决结果和表二彻底绑定！
    // =========================================================
    connect(m_validator, &SelfValidator::mfpResultReady, this, &MainWindow::onMfpResultReady, Qt::QueuedConnection);
}


void MainWindow::onManualTruthClicked() {
    TruthInputDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        std::vector<TargetTruth> customData = dialog.getTruthData();
        m_validator->setTruthData(customData);
        appendLog(QString("\n>> 已成功应用目标先验真值配置，共激活 %1 个目标校验。\n").arg(customData.size()));
    }
}

void MainWindow::onStartClicked() {
    if (m_selectedFiles.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先导入数据文件！");
        return;
    }

    m_currentConfig.fs = m_editFs->text().toDouble();
    m_currentConfig.M = m_editM->text().toInt();
    m_currentConfig.d = m_editD->text().toDouble();
    m_currentConfig.c = m_editC->text().toDouble();
    m_currentConfig.r_scan = m_editRScan->text().toDouble();
    m_currentConfig.timeStep = m_editTimeStep->text().toDouble();
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
    m_currentConfig.dcvLofarBgMedWindow = m_editDcvLofarBgMedWindow->text().toInt();
    m_currentConfig.dcvLofarSnrThreshMult = m_editDcvLofarSnrThreshMult->text().toDouble();
    m_currentConfig.dcvLofarPeakMinDist = m_editDcvLofarPeakMinDist->text().toInt();

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
    m_currentConfig.trackAssocGate = m_editTrackAssocGate->text().toDouble();
    m_currentConfig.trackMHits = m_editTrackMHits->text().toInt();
    m_currentConfig.enableDepthResolve = m_chkDepthResolve->isChecked();
    m_currentConfig.krakenRawPath = m_krakenRawPath;
    m_btnStart->setEnabled(false); m_btnSelectFiles->setEnabled(false); m_btnManualTruth->setEnabled(false);
    m_btnPauseResume->setEnabled(true); m_btnStop->setEnabled(true);
    m_mainTabWidget->setCurrentIndex(0);
    m_lblSysInfo->setText(QString("状态: 运行中\n任务: %1\n开始时间: %2").arg(m_editTaskName->text()).arg(QDateTime::currentDateTime().toString("HH:mm:ss")));

    m_historyResults.clear();
    m_batchAccuracies.clear();
    m_targetClasses.clear();

    // 【意见二】：清空界面上所有常亮目标指示灯
    for (auto* light : m_targetLights.values()) {
        m_targetLightsLayout->removeWidget(light);
        light->deleteLater();
    }
    m_targetLights.clear();

    m_timeAzimuthPlot->graph(0)->data()->clear();
    m_plotBatchAccuracy->graph(0)->data()->clear();
    m_plotBatchAccuracy->replot();

    m_reportConsole->clear();
    m_logConsole->clear();

    QLayoutItem* item;
    while ((item = m_targetLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }
    m_lsPlots.clear(); m_lofarPlots.clear(); m_demonPlots.clear();

    if (m_cbfWaterfallPlot) { m_cbfWaterfallPlot->clearPlottables(); m_cbfWaterfallPlot->replot(); }
    if (m_dcvWaterfallPlot) { m_dcvWaterfallPlot->clearPlottables(); m_dcvWaterfallPlot->replot(); }

    closePopupsFromLayout(m_sliceLayout);
    if (m_sliceLayout) {
        while ((item = m_sliceLayout->takeAt(0)) != nullptr) {
            if (item->widget()) delete item->widget();
            delete item;
        }
    }

    closePopupsFromLayout(m_lofarWaterfallLayout);
    while ((item = m_lofarWaterfallLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    m_worker->setTargetFiles(m_selectedFiles);
    m_worker->setConfig(m_currentConfig);

    const std::vector<TargetTruth>& truths = m_validator->getTruthData();
    m_worker->setGroundTruths(truths);
    // =======================================================
    // 【新增这行代码】：把是否开启深度分辨的状态同步给验证器
    m_validator->setDepthResolveEnabled(m_currentConfig.enableDepthResolve);
    // =======================================================
    if (truths.empty()) {
        m_lblModeIndicator->setText("🔴 实战盲测模式 (无先验真值)");
        m_lblModeIndicator->setStyleSheet("background-color: #fdeaea; color: #e74c3c; font-size: 14px; font-weight: bold; border: 1px solid #e74c3c; border-radius: 5px; padding: 6px;");
        m_logConsole->appendPlainText("[系统提示] 未加载先验真值数据，系统进入【实战盲测模式】。Tab4正确率将显示为特征稳定度。\n");
    } else {
        m_lblModeIndicator->setText(QString("🟢 算法仿真评估模式 (真值:%1个)").arg(truths.size()));
        m_lblModeIndicator->setStyleSheet("background-color: #eafaf1; color: #27ae60; font-size: 14px; font-weight: bold; border: 1px solid #27ae60; border-radius: 5px; padding: 6px;");
        m_logConsole->appendPlainText(QString("[系统提示] 已加载 %1 个先验真值目标，系统进入【算法仿真评估模式】。\n").arg(truths.size()));
    }

    m_worker->start();
}

void MainWindow::onStopClicked() {
    if (m_worker->isRunning()) {
        m_worker->stop();

        // 【意见三】：增加终止时间显示
        QString currentText = m_lblSysInfo->text();
        currentText.replace("状态: 运行中", "状态: 已手动终止");
        currentText.replace("状态: 已挂起 (暂停)", "状态: 已手动终止");
        if (!currentText.contains("结束时间") && !currentText.contains("终止时间")) {
            currentText += QString("\n终止时间: %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss"));
        }
        m_lblSysInfo->setText(currentText);

        appendLog("\n>> 接收到终止指令...\n");
        m_btnStart->setEnabled(true); m_btnSelectFiles->setEnabled(true); m_btnManualTruth->setEnabled(true);
        m_btnPauseResume->setEnabled(false); m_btnStop->setEnabled(false);
    }
}

void MainWindow::onProcessingFinished() {
    // 【意见三】：增加分析完成时间显示
    QString currentText = m_lblSysInfo->text();
    currentText.replace("状态: 运行中", "状态: 分析完成");
    if (!currentText.contains("结束时间") && !currentText.contains("终止时间")) {
        currentText += QString("\n结束时间: %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss"));
    }
    m_lblSysInfo->setText(currentText);

    m_btnStart->setEnabled(true); m_btnSelectFiles->setEnabled(true); m_btnManualTruth->setEnabled(true);
    m_btnPauseResume->setEnabled(false); m_btnStop->setEnabled(false);
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

void MainWindow::popOutPlot(QCustomPlot* plot) {
    if (plot->parentWidget() && plot->parentWidget()->property("isPopup").toBool()) return;

    PlotLayoutInfo info;
    info.originalParent = plot->parentWidget();
    if (info.originalParent) {
        QSplitter* splitter = qobject_cast<QSplitter*>(info.originalParent);
        if (splitter) {
            info.index = splitter->indexOf(plot);
        }
        else if (info.originalParent->layout()) {
            info.originalLayout = info.originalParent->layout();
            QGridLayout* gridLayout = qobject_cast<QGridLayout*>(info.originalLayout);
            if (gridLayout) {
                int rowSpan, colSpan;
                int idx = gridLayout->indexOf(plot);
                if (idx != -1) gridLayout->getItemPosition(idx, &info.row, &info.col, &rowSpan, &colSpan);
            } else {
                info.index = info.originalLayout->indexOf(plot);
            }
            info.originalLayout->removeWidget(plot);
        }
    }
    plot->setParent(nullptr);

    QWidget* popupWindow = new QWidget();
    popupWindow->setProperty("isPopup", true);
    popupWindow->setWindowTitle("图表独立查看 (关闭或最小化即可还原)");
    popupWindow->setMinimumSize(800, 600);

    QVBoxLayout* popupLayout = new QVBoxLayout(popupWindow);
    popupLayout->setContentsMargins(0, 0, 0, 0);
    popupLayout->addWidget(plot);

    m_popupPlots.insert(popupWindow, qMakePair(plot, info));
    popupWindow->installEventFilter(this);
    popupWindow->setAttribute(Qt::WA_DeleteOnClose);
    popupWindow->show();
    appendLog(">> 已将图表弹出为独立窗口。\n");
}

void MainWindow::restorePlot(QWidget* popupWindow) {
    if (!m_popupPlots.contains(popupWindow)) return;

    QPair<QCustomPlot*, PlotLayoutInfo> data = m_popupPlots.take(popupWindow);
    QCustomPlot* plot = data.first;
    PlotLayoutInfo info = data.second;

    if (plot && info.originalParent) {
        plot->setParent(info.originalParent);
        QSplitter* splitter = qobject_cast<QSplitter*>(info.originalParent);
        if (splitter) {
            splitter->insertWidget(info.index, plot);
        }
        else if (info.originalLayout) {
            QGridLayout* gridLayout = qobject_cast<QGridLayout*>(info.originalLayout);
            if (gridLayout && info.row != -1 && info.col != -1) {
                gridLayout->addWidget(plot, info.row, info.col);
            } else if (QBoxLayout* boxLayout = qobject_cast<QBoxLayout*>(info.originalLayout)) {
                if (info.index != -1) boxLayout->insertWidget(info.index, plot);
                else boxLayout->addWidget(plot);
            } else {
                info.originalLayout->addWidget(plot);
            }
        }
        plot->show();
    }
    appendLog(">> 图表已恢复至主界面原始位置。\n");
}

void MainWindow::closePopupsFromLayout(QLayout* targetLayout) {
    if (!targetLayout) return;
    QList<QWidget*> popups = m_popupPlots.keys();
    for (QWidget* w : popups) {
        if (w && m_popupPlots[w].second.originalLayout == targetLayout) {
            w->close();
        }
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    QWidget* widget = qobject_cast<QWidget*>(obj);
    if (widget && widget->property("isPopup").toBool()) {
        if (event->type() == QEvent::Close) {
            restorePlot(widget);
        } else if (event->type() == QEvent::WindowStateChange) {
            if (widget->isMinimized()) {
                restorePlot(widget);
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
    QAction* actPopOut = menu.addAction("🪟 弹出为独立窗口");
    menu.addSeparator();
    QAction* actToggleTip = menu.addAction(plot->property("showTooltip").toBool() ? "💡 隐藏光标数值" : "💡 开启光标数值");
    menu.addSeparator();
    QAction* actSave = menu.addAction("💾 将当前图表保存为 PNG...");

    QAction* selected = menu.exec(plot->mapToGlobal(pos));
    if (selected == actReset) onPlotDoubleClick(nullptr);
    else if (selected == actZoomIn) { plot->xAxis->scaleRange(0.8); plot->yAxis->scaleRange(0.8); plot->replot(); }
    else if (selected == actZoomOut) { plot->xAxis->scaleRange(1.25); plot->yAxis->scaleRange(1.25); plot->replot(); }
    else if (selected == actPopOut) popOutPlot(plot);
    else if (selected == actToggleTip) {
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

// 【新增函数】：用于将用户填写的要剔除的ID发送给Worker线程
void MainWindow::onDeleteTargetClicked() {
    bool ok;
    int targetId = m_editDeleteTargetId->text().toInt(&ok);
    if (!ok || targetId <= 0) {
        QMessageBox::warning(this, "输入错误", "请输入有效的正整数目标ID！");
        return;
    }

    if (m_worker && m_worker->isRunning()) {
        m_worker->requestRemoveTarget(targetId);
        appendLog(QString("\n>> 已发送人工干预指令：系统将在下一帧彻底且永久剔除假目标 ID [%1]\n").arg(targetId));
    } else {
        appendLog(QString("\n>> 正在全局清理假目标 ID [%1] 的界面图表及所有相关指标...\n").arg(targetId));
    }

    // ====== 【意见二】：清理被删除目标的常亮指示灯 ======
    if (m_targetLights.contains(targetId)) {
        QLabel* light = m_targetLights.take(targetId);
        m_targetLightsLayout->removeWidget(light);
        light->deleteLater();
    }

    for (auto& frame : m_historyResults) {
        for (int i = frame.tracks.size() - 1; i >= 0; --i) {
            if (frame.tracks[i].id == targetId) {
                frame.tracks.removeAt(i);
            }
        }
    }

    auto removeTab1Plot = [&](QMap<int, QCustomPlot*>& plotMap) {
        if (plotMap.contains(targetId)) {
            QCustomPlot* p = plotMap.take(targetId);
            for (auto it = m_popupPlots.begin(); it != m_popupPlots.end(); ++it) {
                if (it.value().first == p) { it.key()->close(); break; }
            }
            m_targetLayout->removeWidget(p);
            p->hide(); p->deleteLater();
        }
    };
    removeTab1Plot(m_lsPlots);
    removeTab1Plot(m_lofarPlots);
    removeTab1Plot(m_demonPlots);

    if (m_sliceWidget) {
        QList<QString> suffixes = {"cbf", "dcv"};
        for (const QString& suf : suffixes) {
            QString name = QString("slice_%1_%2").arg(suf).arg(targetId);
            if (QCustomPlot* p = m_sliceWidget->findChild<QCustomPlot*>(name)) {
                m_sliceLayout->removeWidget(p); p->hide(); p->deleteLater();
            }
        }
    }

    updateTab2Plots();

    if (m_lofarWaterfallWidget) {
        QList<QString> prefixes = {"offline_raw", "offline_tpsw", "offline_dp"};
        for (const QString& pref : prefixes) {
            QString name = QString("%1_%2").arg(pref).arg(targetId);
            if (QCustomPlot* p = m_lofarWaterfallWidget->findChild<QCustomPlot*>(name)) {
                m_lofarWaterfallLayout->removeWidget(p); p->hide(); p->deleteLater();
            }
        }
    }

    if (m_tableTargetFeatures) {
        for (int r = 0; r < m_tableTargetFeatures->rowCount(); ++r) {
            QTableWidgetItem* item = m_tableTargetFeatures->item(r, 0);
            if (item && item->text() == QString("Target %1").arg(targetId)) {
                m_tableTargetFeatures->removeRow(r);
                break;
            }
        }
    }

    if (m_calcAzimuthGraphs.contains(targetId)) {
        m_plotCalcAzimuth->removeGraph(m_calcAzimuthGraphs.take(targetId));
        m_plotCalcAzimuth->replot();
    }
    if (m_trueAzimuthGraphs.contains(targetId)) {
        m_plotTrueAzimuth->removeGraph(m_trueAzimuthGraphs.take(targetId));
        m_plotTrueAzimuth->replot();
    }

    QSet<int> unique_tids;
    for (const auto& frame : m_historyResults) {
        for (const auto& t : frame.tracks) {
            if (t.isConfirmed) unique_tids.insert(t.id);
        }
    }
    m_lblStatTargets->setText(QString("<span style='font-size:36px; color:#2980b9;'>%1</span> 艘").arg(unique_tids.size()));

    m_editDeleteTargetId->clear();
}

// 【修改函数】：替换原有的 setupUi() 完整代码（由于字数较长，主要在左侧控件面板增加剔除假目标的文本框与按钮布局）
void MainWindow::setupUi() {
    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* mainVLayout = new QVBoxLayout(centralWidget);
    QSplitter* verticalSplitter = new QSplitter(Qt::Vertical, centralWidget);

    QWidget* topWidget = new QWidget(verticalSplitter);
    QVBoxLayout* topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* topMainSplitter = new QSplitter(Qt::Horizontal, topWidget);
    topLayout->addWidget(topMainSplitter);

    // ==========================================
    // 左侧：参数与控制面板
    // ==========================================
    QWidget* leftPanel = new QWidget(topMainSplitter);
    leftPanel->setMinimumWidth(250);
    leftPanel->setMaximumWidth(600);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0,0,0,0);

    QGroupBox* groupButtons = new QGroupBox("系统控制指令区", leftPanel);
    QVBoxLayout* btnLayout = new QVBoxLayout(groupButtons);

    m_btnSelectFiles = new QPushButton(" 数据文件输入...", this);
    m_btnSelectFiles->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    m_btnManualTruth = new QPushButton(" 目标先验真值配置窗口...", this);
    m_btnManualTruth->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_btnStart       = new QPushButton(" 开始算法处理", this);
    m_btnStart->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_btnPauseResume = new QPushButton(" 暂停/继续", this);
    m_btnPauseResume->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    m_btnStop        = new QPushButton(" 终止算法", this);
    m_btnStop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_btnExport      = new QPushButton(" 一键导出日志图片", this);
    m_btnExport->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_chkDepthResolve = new QCheckBox("启用 MFP 水上/水下目标分辨", this);
    connect(m_chkDepthResolve, &QCheckBox::toggled, this, &MainWindow::onDepthResolveToggled);
    m_chkDepthResolve->setChecked(false); // 默认不勾选
    m_editDeleteTargetId = new QLineEdit(this);
    m_editDeleteTargetId->setPlaceholderText("要剔除的目标ID...");
    m_btnDeleteTarget = new QPushButton(" 剔除虚假目标", this);
    m_btnDeleteTarget->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_btnDeleteTarget->setStyleSheet("QPushButton { color: #c0392b; font-weight: bold; }");
    connect(m_btnDeleteTarget, &QPushButton::clicked, this, &MainWindow::onDeleteTargetClicked);

    QHBoxLayout* delLayout = new QHBoxLayout();
    delLayout->addWidget(m_editDeleteTargetId);
    delLayout->addWidget(m_btnDeleteTarget);

    m_btnStart->setEnabled(false); m_btnPauseResume->setEnabled(false); m_btnStop->setEnabled(false);

    m_editTaskName = new QLineEdit(this);
    m_editTaskName->setPlaceholderText("导入数据后自动识别，可自定义...");
    btnLayout->addWidget(new QLabel("当前任务名称:", leftPanel));
    btnLayout->addWidget(m_editTaskName);

    btnLayout->addWidget(m_btnSelectFiles); btnLayout->addWidget(m_btnManualTruth);
    btnLayout->addWidget(m_btnStart); btnLayout->addWidget(m_btnPauseResume);
    btnLayout->addWidget(m_btnStop); btnLayout->addWidget(m_btnExport);
    // 【修复】：加上这一行，把复选框放进布局里！
    btnLayout->addWidget(m_chkDepthResolve);
    btnLayout->addLayout(delLayout);
    leftLayout->addWidget(groupButtons);

    QScrollArea* paramScroll = new QScrollArea(leftPanel);
    paramScroll->setWidgetResizable(true); paramScroll->setFrameShape(QFrame::NoFrame);
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
    fArray->addRow("批处理帧数 (帧):", m_editBatchSize = new QLineEdit("20"));
    paramLayout->addWidget(gArray);

    QGroupBox* gFreq = new QGroupBox("目标特征频段划分", paramContainer);
    QFormLayout* fFreq = new QFormLayout(gFreq);
    fFreq->addRow("LOFAR 下限 (Hz):", m_editLofarMin = new QLineEdit("100"));
    fFreq->addRow("LOFAR 上限 (Hz):", m_editLofarMax = new QLineEdit("300"));
    fFreq->addRow("DEMON 下限 (Hz):", m_editDemonMin = new QLineEdit("350"));
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

    QGroupBox* gTrack = new QGroupBox("目标航迹关联与判定", paramContainer);
    QFormLayout* fTrack = new QFormLayout(gTrack);
    fTrack->addRow("航迹关联波门 (°):", m_editTrackAssocGate = new QLineEdit("6.0"));
    fTrack->addRow("M/N 判定激活帧数:", m_editTrackMHits = new QLineEdit("11"));
    paramLayout->addWidget(gTrack);

    QGroupBox* gLofarExt = new QGroupBox("实时与累积线谱提取", paramContainer);
    QFormLayout* fLofarExt = new QFormLayout(gLofarExt);
    fLofarExt->addRow("【瞬时】中值窗宽:", m_editLofarBgMedWindow = new QLineEdit("60"));
    fLofarExt->addRow("【瞬时】SNR 乘数:", m_editLofarSnrThreshMult = new QLineEdit("2"));
    fLofarExt->addRow("【瞬时】最小点距:", m_editLofarPeakMinDist = new QLineEdit("30"));
    fLofarExt->addRow("【DCV累积】中值窗宽:", m_editDcvLofarBgMedWindow = new QLineEdit("150"));
    fLofarExt->addRow("【DCV累积】SNR乘数:", m_editDcvLofarSnrThreshMult = new QLineEdit("4"));
    fLofarExt->addRow("【DCV累积】最小点距:", m_editDcvLofarPeakMinDist = new QLineEdit("180"));
    paramLayout->addWidget(gLofarExt);

    QGroupBox* gDemon = new QGroupBox("DEMON 包络数字滤波", paramContainer);
    QFormLayout* fDemon = new QFormLayout(gDemon);
    fDemon->addRow("FIR 滤波器阶数:", m_editFirOrder = new QLineEdit("64"));
    fDemon->addRow("归一化截止频率:", m_editFirCutoff = new QLineEdit("0.1"));
    paramLayout->addWidget(gDemon);

    QGroupBox* gDp = new QGroupBox("TPSW 与 DP 轨迹寻优", paramContainer);
    QFormLayout* fDp = new QFormLayout(gDp);
    fDp->addRow("TPSW 保护窗 (G):", m_editTpswG = new QLineEdit("60"));
    fDp->addRow("TPSW 排除窗 (E):", m_editTpswE = new QLineEdit("5"));
    fDp->addRow("TPSW 补偿因子 (C):", m_editTpswC = new QLineEdit("1.6"));
    fDp->addRow("DP 记忆窗长 (L):", m_editDpL = new QLineEdit("3"));
    fDp->addRow("惩罚因子 Alpha:", m_editDpAlpha = new QLineEdit("2.5"));
    fDp->addRow("惩罚因子 Beta:", m_editDpBeta = new QLineEdit("0.8"));
    fDp->addRow("偏置因子 Gamma:", m_editDpGamma = new QLineEdit("0.1"));
    fDp->addRow("背景判决分位数(%):", m_editDpPrctileThresh = new QLineEdit("96.0"));
    fDp->addRow("寻峰提取门限(std倍数):", m_editDpPeakStdMult = new QLineEdit("1.5"));
    paramLayout->addWidget(gDp);

    QGroupBox* gDcv = new QGroupBox("高分辨反卷积 (DCV) 设置", paramContainer);
    QFormLayout* fDcv = new QFormLayout(gDcv);
    fDcv->addRow("RL 迭代次数:", m_editDcvRlIter = new QLineEdit("10"));
    paramLayout->addWidget(gDcv);

    paramScroll->setWidget(paramContainer);
    leftLayout->addWidget(paramScroll, 2);
    topMainSplitter->addWidget(leftPanel);

    // ==========================================
    // 中间：主图表展示区
    // ==========================================
    m_mainTabWidget = new QTabWidget(topMainSplitter);
    topMainSplitter->addWidget(m_mainTabWidget);

    QPushButton* toggleLeftBtn = new QPushButton("◀ 隐藏控制栏", m_mainTabWidget);
    toggleLeftBtn->setCheckable(true);
    toggleLeftBtn->setCursor(Qt::PointingHandCursor);
    toggleLeftBtn->setStyleSheet("QPushButton { border: none; padding: 4px 10px; color: #2c3e50; font-weight: bold; } QPushButton:hover { color: #2980b9; }");
    connect(toggleLeftBtn, &QPushButton::toggled, this, [leftPanel, toggleLeftBtn](bool checked){
        leftPanel->setVisible(!checked);
        toggleLeftBtn->setText(checked ? "▶ 展开控制栏" : "◀ 隐藏控制栏");
    });
    m_mainTabWidget->setCornerWidget(toggleLeftBtn, Qt::TopLeftCorner);

    // ==========================================
    // 右侧：系统状态与终端面板
    // ==========================================
    QWidget* rightSidePanel = new QWidget(topMainSplitter);
    rightSidePanel->setMinimumWidth(250);
    rightSidePanel->setMaximumWidth(600);
    QVBoxLayout* rightSideLayout = new QVBoxLayout(rightSidePanel);
    rightSideLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* groupLog = new QGroupBox("系统状态与终端", rightSidePanel);
    QVBoxLayout* logLayout = new QVBoxLayout(groupLog);

    // ==========================================
    // 【意见二】：模式状态醒目指示灯与多目标常亮指示灯
    // ==========================================
    m_lblModeIndicator = new QLabel("⚪ 当前模式: 待就绪", this);
    m_lblModeIndicator->setAlignment(Qt::AlignCenter);
    m_lblModeIndicator->setStyleSheet("background-color: #ecf0f1; color: #7f8c8d; font-size: 14px; font-weight: bold; border: 1px solid #bdc3c7; border-radius: 5px; padding: 6px;");

    m_targetLightsWidget = new QWidget(this);
    m_targetLightsLayout = new QHBoxLayout(m_targetLightsWidget);
    m_targetLightsLayout->setContentsMargins(0, 0, 0, 0);
    m_targetLightsLayout->setAlignment(Qt::AlignLeft);

    QHBoxLayout* indicatorLayout = new QHBoxLayout();
    indicatorLayout->addWidget(m_lblModeIndicator);
    indicatorLayout->addWidget(m_targetLightsWidget); // 放入容器，常亮展示
    indicatorLayout->addStretch(); // 靠左对齐，不拉伸
    logLayout->addLayout(indicatorLayout);
    // ==========================================

    m_lblSysInfo = new QLabel("引擎初始化完成，参数已就绪。\n等待注入探测数据...");
    m_lblSysInfo->setStyleSheet("color: #333333; font-weight: bold;");
    logLayout->addWidget(m_lblSysInfo);

    m_logConsole = new QPlainTextEdit(this);
    m_logConsole->setReadOnly(true);
    m_logConsole->setStyleSheet("background-color: #1e1e1e; color: #00ff00; font-family: Consolas;");
    logLayout->addWidget(m_logConsole);

    rightSideLayout->addWidget(groupLog);
    topMainSplitter->addWidget(rightSidePanel);

    topMainSplitter->setStretchFactor(0, 0);
    topMainSplitter->setStretchFactor(1, 1);
    topMainSplitter->setStretchFactor(2, 0);
    topMainSplitter->setSizes(QList<int>() << 330 << 940 << 330);

    // ================== TAB 1 ==================
    QWidget* tab1 = new QWidget();
    QHBoxLayout* tab1Layout = new QHBoxLayout(tab1);
    QSplitter* horizontalSplitter = new QSplitter(Qt::Horizontal, tab1);

    QWidget* midPanel = new QWidget(horizontalSplitter);
    QVBoxLayout* midLayout = new QVBoxLayout(midPanel);
    m_timeAzimuthPlot = new QCustomPlot(midPanel);
    m_timeAzimuthPlot->setMinimumSize(300, 200);
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
    m_spatialPlot->setMinimumSize(300, 250);
    m_spatialPlot->setMaximumHeight(350);
    setupPlotInteraction(m_spatialPlot);
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
    m_mainTabWidget->addTab(tab1, "实时处理：目标探测与关联");

    // ================== TAB 2 ==================
    QWidget* tab2 = new QWidget();
    QVBoxLayout* tab2MainLayout = new QVBoxLayout(tab2);
    QScrollArea* tab2Scroll = new QScrollArea(tab2);
    tab2Scroll->setWidgetResizable(true);
    tab2Scroll->setFrameShape(QFrame::NoFrame);
    QWidget* tab2Container = new QWidget(tab2Scroll);
    QVBoxLayout* tab2ContainerLayout = new QVBoxLayout(tab2Container);

    QSplitter* waterfallsSplitter = new QSplitter(Qt::Horizontal, tab2Container);
    m_cbfWaterfallPlot = new QCustomPlot(waterfallsSplitter);
    m_cbfWaterfallPlot->setMinimumSize(300, 400); setupPlotInteraction(m_cbfWaterfallPlot);
    m_cbfWaterfallPlot->plotLayout()->insertRow(0); m_cbfWaterfallPlot->plotLayout()->addElement(0, 0, new QCPTextElement(m_cbfWaterfallPlot, "常规波束形成(CBF) 空间谱历程", QFont("sans", 12, QFont::Bold)));
    waterfallsSplitter->addWidget(m_cbfWaterfallPlot);

    m_dcvWaterfallPlot = new QCustomPlot(waterfallsSplitter);
    m_dcvWaterfallPlot->setMinimumSize(300, 400); setupPlotInteraction(m_dcvWaterfallPlot);
    m_dcvWaterfallPlot->plotLayout()->insertRow(0); m_dcvWaterfallPlot->plotLayout()->addElement(0, 0, new QCPTextElement(m_dcvWaterfallPlot, "高分辨反卷积(DCV) 全方位时空谱历程", QFont("sans", 12, QFont::Bold)));
    waterfallsSplitter->addWidget(m_dcvWaterfallPlot);

    waterfallsSplitter->setStretchFactor(0, 1); waterfallsSplitter->setStretchFactor(1, 1);
    waterfallsSplitter->setSizes(QList<int>() << 1000 << 1000);
    tab2ContainerLayout->addWidget(waterfallsSplitter);

    m_sliceWidget = new QWidget(tab2Container);
    m_sliceLayout = new QGridLayout(m_sliceWidget);
    m_sliceLayout->setAlignment(Qt::AlignTop);
    tab2ContainerLayout->addWidget(m_sliceWidget);

    tab2Scroll->setWidget(tab2Container);
    tab2MainLayout->addWidget(tab2Scroll);
    m_mainTabWidget->addTab(tab2, "实时处理：CBF/DCV全景与切片");

    // ================== TAB 3 ==================
    QWidget* tab3 = new QWidget();
    QVBoxLayout* tab3Layout = new QVBoxLayout(tab3);
    QScrollArea* lofarScroll = new QScrollArea(tab3);
    lofarScroll->setWidgetResizable(true);
    m_lofarWaterfallWidget = new QWidget(lofarScroll);
    m_lofarWaterfallLayout = new QGridLayout(m_lofarWaterfallWidget);
    m_lofarWaterfallLayout->setAlignment(Qt::AlignTop);
    lofarScroll->setWidget(m_lofarWaterfallWidget);
    tab3Layout->addWidget(lofarScroll);
    m_mainTabWidget->addTab(tab3, "批处理：TPSW均衡与DP寻优");

    // ================== TAB 4 ==================
    QWidget* tab4 = new QWidget();
    QVBoxLayout* tab4Layout = new QVBoxLayout(tab4);
    tab4->setStyleSheet("QWidget { background-color: #f4f6f9; }");

    QWidget* cardsWidget = new QWidget(tab4);
    QHBoxLayout* cardsLayout = new QHBoxLayout(cardsWidget);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_lblStatTime = new QLabel("--"); m_lblStatTargets = new QLabel("--"); m_lblStatAvgAcc = new QLabel("--");
    cardsLayout->addWidget(createCardWidget(m_lblStatTime, "#ffffff", "系统全流程解算耗时分布"));
    cardsLayout->addWidget(createCardWidget(m_lblStatTargets, "#ffffff", "稳定识别/锁定目标总数"));
    cardsLayout->addWidget(createCardWidget(m_lblStatAvgAcc, "#ffffff", "全局平均线谱提取正确率"));
    tab4Layout->addWidget(cardsWidget);

    QSplitter* tab4ContentSplitter = new QSplitter(Qt::Vertical, tab4);

    // 1. 创建一个专属容器来垂直排列两个表格
    QWidget* tablesContainer = new QWidget(tab4ContentSplitter);
    QVBoxLayout* tablesLayout = new QVBoxLayout(tablesContainer);
    tablesLayout->setContentsMargins(0, 0, 0, 0);
    tablesLayout->setSpacing(10); // 两个表格之间留一点间距

    // ==========================================
    // 表格 1：原版特征提取与综合判定表 (恢复为 9 列，保留蓝色表头)
    // ==========================================
    m_tableTargetFeatures = new QTableWidget(tablesContainer);
    m_tableTargetFeatures->setColumnCount(9);

    // 恢复原来的表头（保留了“综合判定”，去掉了“水上水下”）
    m_tableTargetFeatures->setHorizontalHeaderLabels({"目标名称/ID", "瞬时线谱群", "瞬时准度", "累积DCV线谱", "DCV准度", "稳定轴频", "真实方位", "解算方位", "综合判定"});

    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableTargetFeatures->horizontalHeader()->setStretchLastSection(true);
    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    m_tableTargetFeatures->horizontalHeader()->setSectionResizeMode(8, QHeaderView::ResizeToContents);

    m_tableTargetFeatures->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_tableTargetFeatures->setAlternatingRowColors(true);
    m_tableTargetFeatures->setStyleSheet("QTableWidget { background-color: white; border-radius: 8px; } QHeaderView::section { background-color: #0078d7; color: white; font-weight: bold; border: none; padding: 6px; }");

    tablesLayout->addWidget(m_tableTargetFeatures);

    // ==========================================
    // 表格 2：新增的专属 MFP 水上/水下判别表 (6 列，橙色表头)
    // ==========================================
    m_tableMfpResults = new QTableWidget(tablesContainer);
    m_tableMfpResults->setColumnCount(6);
    m_tableMfpResults->setHorizontalHeaderLabels({"目标名称/ID", "估计深度", "真实深度", "真实类别", "系统判别", "判别结果"});

    m_tableMfpResults->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableMfpResults->horizontalHeader()->setStretchLastSection(true);
    m_tableMfpResults->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for(int i = 1; i < 6; ++i) {
        m_tableMfpResults->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }

    m_tableMfpResults->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_tableMfpResults->setAlternatingRowColors(true);
    // 橙色表头，与原表格产生鲜明视觉区分
    m_tableMfpResults->setStyleSheet("QTableWidget { background-color: white; border-radius: 8px; } QHeaderView::section { background-color: #e67e22; color: white; font-weight: bold; border: none; padding: 6px; }");
    m_tableMfpResults->setVisible(false); // 默认隐藏，仅在勾选 MFP 时由后端控制显示

    tablesLayout->addWidget(m_tableMfpResults);

    // 将包含了两个表格的容器装入界面的分割器中
    tab4ContentSplitter->addWidget(tablesContainer);

    // 绑定两个表格的双击改名信号
    connect(m_tableTargetFeatures, &QTableWidget::itemChanged, this, &MainWindow::onTargetNameChanged);
    connect(m_tableMfpResults, &QTableWidget::itemChanged, this, &MainWindow::onTargetNameChanged);
    QSplitter* midPlotsSplitter = new QSplitter(Qt::Horizontal, tab4ContentSplitter);

    m_plotTrueAzimuth = new QCustomPlot(midPlotsSplitter);
    m_plotTrueAzimuth->setMinimumSize(300, 200); m_plotTrueAzimuth->setStyleSheet("background-color: white; border-radius: 8px;");
    m_plotTrueAzimuth->plotLayout()->insertRow(0); m_plotTrueAzimuth->plotLayout()->addElement(0, 0, new QCPTextElement(m_plotTrueAzimuth, "先验真实方位轨迹演变", QFont("sans", 12, QFont::Bold)));
    m_plotTrueAzimuth->xAxis->setLabel("监控周期 (批次)"); m_plotTrueAzimuth->yAxis->setLabel("真实方位角 (°)");
    m_plotTrueAzimuth->yAxis->setRange(0, 180);
    setupPlotInteraction(m_plotTrueAzimuth);
    m_plotTrueAzimuth->legend->setVisible(true);
    m_plotTrueAzimuth->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignBottom | Qt::AlignLeft);
    m_plotTrueAzimuth->legend->setBrush(QColor(255, 255, 255, 180));
    midPlotsSplitter->addWidget(m_plotTrueAzimuth);

    m_plotCalcAzimuth = new QCustomPlot(midPlotsSplitter);
    m_plotCalcAzimuth->setMinimumSize(300, 200); m_plotCalcAzimuth->setStyleSheet("background-color: white; border-radius: 8px;");
    m_plotCalcAzimuth->plotLayout()->insertRow(0); m_plotCalcAzimuth->plotLayout()->addElement(0, 0, new QCPTextElement(m_plotCalcAzimuth, "解算跟踪方位轨迹演变", QFont("sans", 12, QFont::Bold)));
    m_plotCalcAzimuth->xAxis->setLabel("监控周期 (批次)"); m_plotCalcAzimuth->yAxis->setLabel("解算方位角 (°)");
    m_plotCalcAzimuth->yAxis->setRange(0, 180);
    setupPlotInteraction(m_plotCalcAzimuth);
    m_plotCalcAzimuth->legend->setVisible(true);
    m_plotCalcAzimuth->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignBottom | Qt::AlignLeft);
    m_plotCalcAzimuth->legend->setBrush(QColor(255, 255, 255, 180));
    midPlotsSplitter->addWidget(m_plotCalcAzimuth);

    tab4ContentSplitter->addWidget(midPlotsSplitter);

    QSplitter* bottomPlotsSplitter = new QSplitter(Qt::Horizontal, tab4ContentSplitter);
    m_plotTargetAccuracy = new QCustomPlot(bottomPlotsSplitter);
    m_plotTargetAccuracy->setMinimumSize(300, 200); m_plotTargetAccuracy->setStyleSheet("background-color: white; border-radius: 8px;");
    m_plotTargetAccuracy->plotLayout()->insertRow(0); m_plotTargetAccuracy->plotLayout()->addElement(0, 0, new QCPTextElement(m_plotTargetAccuracy, "各独立目标特征提取正确率", QFont("sans", 12, QFont::Bold)));

    m_accuracyBars = new QCPBars(m_plotTargetAccuracy->xAxis, m_plotTargetAccuracy->yAxis);
    m_accuracyBars->setName("瞬时准度");
    m_accuracyBars->setPen(QPen(Qt::NoPen));
    m_accuracyBars->setBrush(QColor(230, 126, 34));
    m_accuracyBars->setWidth(0.35);

    QCPBars* barsDcv = new QCPBars(m_plotTargetAccuracy->xAxis, m_plotTargetAccuracy->yAxis);
    barsDcv->setName("DCV准度");
    barsDcv->setPen(QPen(Qt::NoPen));
    barsDcv->setBrush(QColor(46, 204, 113));
    barsDcv->setWidth(0.35);

    QCPBarsGroup *group = new QCPBarsGroup(m_plotTargetAccuracy);
    group->append(m_accuracyBars);
    group->append(barsDcv);
    group->setSpacingType(QCPBarsGroup::stAbsolute);
    group->setSpacing(2);

    m_plotTargetAccuracy->legend->setVisible(true);
    m_plotTargetAccuracy->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);
    m_plotTargetAccuracy->legend->setBrush(QColor(255, 255, 255, 180));
    m_plotTargetAccuracy->xAxis->setLabel("目标编号"); m_plotTargetAccuracy->yAxis->setLabel("正确率 (%)"); m_plotTargetAccuracy->yAxis->setRange(0, 105);
    setupPlotInteraction(m_plotTargetAccuracy);
    bottomPlotsSplitter->addWidget(m_plotTargetAccuracy);

    m_plotBatchAccuracy = new QCustomPlot(bottomPlotsSplitter);
    m_plotBatchAccuracy->setMinimumSize(300, 200); m_plotBatchAccuracy->setStyleSheet("background-color: white; border-radius: 8px;");
    m_plotBatchAccuracy->plotLayout()->insertRow(0); m_plotBatchAccuracy->plotLayout()->addElement(0, 0, new QCPTextElement(m_plotBatchAccuracy, "连续监测周期(批次)综合正确率走势", QFont("sans", 12, QFont::Bold)));
    m_plotBatchAccuracy->addGraph(); m_plotBatchAccuracy->graph(0)->setPen(QPen(QColor(46, 204, 113), 3));
    m_plotBatchAccuracy->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, QColor(46, 204, 113), Qt::white, 8));
    m_plotBatchAccuracy->xAxis->setLabel("运算监测批次"); m_plotBatchAccuracy->yAxis->setLabel("批次综合正确率 (%)"); m_plotBatchAccuracy->yAxis->setRange(0, 105);
    setupPlotInteraction(m_plotBatchAccuracy);
    bottomPlotsSplitter->addWidget(m_plotBatchAccuracy);

    tab4ContentSplitter->addWidget(bottomPlotsSplitter);
    tab4ContentSplitter->setStretchFactor(0, 3);
    tab4ContentSplitter->setStretchFactor(1, 4);
    tab4ContentSplitter->setStretchFactor(2, 4);

    tab4Layout->addWidget(tab4ContentSplitter);
    m_mainTabWidget->addTab(tab4, "批处理：综合效能成果");

    // ==========================================
    // 底部：评估报告终端
    // ==========================================
    verticalSplitter->addWidget(topWidget);
    QGroupBox* groupReport = new QGroupBox("综合处理评估报告终端", verticalSplitter);
    QVBoxLayout* reportLayout = new QVBoxLayout(groupReport);
    m_reportConsole = new QPlainTextEdit(this);
    m_reportConsole->setReadOnly(true); m_reportConsole->setStyleSheet("background-color: #2b2b2b; color: #ffaa00; font-family: Consolas; font-size: 13px;");
    reportLayout->addWidget(m_reportConsole);
    verticalSplitter->addWidget(groupReport);
    verticalSplitter->setStretchFactor(0, 4); verticalSplitter->setStretchFactor(1, 1);

    QWidget* cornerWidget = new QWidget(m_mainTabWidget);
    QHBoxLayout* cornerLayout = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 0, 0, 0); cornerLayout->setSpacing(8);

    QPushButton* toggleBottomBtn = new QPushButton("▼ 隐藏报告栏", cornerWidget);
    toggleBottomBtn->setCheckable(true); toggleBottomBtn->setCursor(Qt::PointingHandCursor);
    toggleBottomBtn->setStyleSheet("QPushButton { border: none; padding: 4px 10px; color: #2c3e50; font-weight: bold; } QPushButton:hover { color: #2980b9; }");
    connect(toggleBottomBtn, &QPushButton::toggled, this, [groupReport, toggleBottomBtn](bool checked){
        groupReport->setVisible(!checked); toggleBottomBtn->setText(checked ? "▲ 展开报告栏" : "▼ 隐藏报告栏");
    });

    QPushButton* toggleRightBtn = new QPushButton("隐藏终端栏 ▶", cornerWidget);
    toggleRightBtn->setCheckable(true); toggleRightBtn->setCursor(Qt::PointingHandCursor);
    toggleRightBtn->setStyleSheet("QPushButton { border: none; padding: 4px 10px; color: #2c3e50; font-weight: bold; } QPushButton:hover { color: #2980b9; }");
    connect(toggleRightBtn, &QPushButton::toggled, this, [rightSidePanel, toggleRightBtn](bool checked){
        rightSidePanel->setVisible(!checked); toggleRightBtn->setText(checked ? "◀ 展开终端栏" : "隐藏终端栏 ▶");
    });

    cornerLayout->addWidget(toggleBottomBtn); cornerLayout->addWidget(toggleRightBtn);
    m_mainTabWidget->setCornerWidget(cornerWidget, Qt::TopRightCorner);

    mainVLayout->addWidget(verticalSplitter);
    setCentralWidget(centralWidget);
    resize(1600, 1000);
    setWindowTitle("SonarTracker");
}




void MainWindow::onSelectFilesClicked() {
    // 创建文件对话框
    QFileDialog dialog(this, "选择多个数据文件夹 (按住 Ctrl 或鼠标拖拽进行多选)");
    dialog.setFileMode(QFileDialog::Directory);           // 设定为纯选文件夹模式
    dialog.setOption(QFileDialog::ShowDirsOnly, true);    // 只显示文件夹
    dialog.setOption(QFileDialog::DontUseNativeDialog, true); // ★ 强制使用Qt自带对话框以支持多选文件夹

    // ★ 侵入底层视图，强行开启多选支持
    QListView *lView = dialog.findChild<QListView*>("listView");
    if (lView) lView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QTreeView *tView = dialog.findChild<QTreeView*>();
    if (tView) tView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    if (!dialog.exec()) return;

    QStringList selectedDirs = dialog.selectedFiles();
    if (selectedDirs.isEmpty()) return;

    m_selectedFiles.clear();

    // 遍历用户选中的所有文件夹，深度扫描里面所有的 .raw 文件
    for (const QString& dirPath : selectedDirs) {
        QDirIterator it(dirPath, QStringList() << "*.raw", QDir::Files, QDirIterator::Subdirectories);
        while(it.hasNext()) {
            m_selectedFiles.append(it.next());
        }
    }

    if (m_selectedFiles.isEmpty()) {
        QMessageBox::warning(this, "未找到数据", "所选的文件夹中未找到任何 .raw 数据文件！");
        return;
    }

    // 【意见六】：提取首个文件夹名称作为默认任务名称
    QFileInfo firstDirInfo(selectedDirs.first());
    QString defaultTaskName = firstDirInfo.fileName();
    if (selectedDirs.size() > 1) {
        defaultTaskName += QString("等%1个目录").arg(selectedDirs.size());
    }
    m_editTaskName->setText(defaultTaskName);

    m_lblSysInfo->setText(QString("状态: 就绪\n任务: %1\n共加载文件夹: %2 个\n共包含文件: %3 个")
                          .arg(defaultTaskName).arg(selectedDirs.size()).arg(m_selectedFiles.size()));
    appendLog(QString(">> 已成功导入 %1 个文件夹，共扫描到 %2 个 .raw 数据文件。\n")
              .arg(selectedDirs.size()).arg(m_selectedFiles.size()));

    // 【意见二】：如果当前系统内已经存在先验真值，提示用户是否沿用
    if (!m_validator->getTruthData().empty()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("先验配置确认");
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setText("检测到系统已载入【先验真值配置】。\n导入新批次数据时，是否继续沿用上一次的配置？");

        QPushButton* btnKeep = msgBox.addButton("🟢 继续沿用", QMessageBox::AcceptRole);
        QPushButton* btnClear = msgBox.addButton("🔴 清空重置", QMessageBox::DestructiveRole);
        QPushButton* btnEdit = msgBox.addButton("📝 打开面板修改", QMessageBox::ActionRole);

        msgBox.exec();

        if (msgBox.clickedButton() == btnClear) {
            m_validator->setTruthData(std::vector<TargetTruth>());
            appendLog(">> 用户指令：已清空先验真值，即将进入【实战盲测模式】。\n");
        } else if (msgBox.clickedButton() == btnEdit) {
            onManualTruthClicked();
        } else {
            appendLog(">> 用户指令：继续沿用上一批次的先验真值配置。\n");
        }
    }

    m_btnStart->setEnabled(true);
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
    // 替换为：
    out << QString("         SonarTracker 综合分析导出报表\n");
    out << QString("         任务名称: ") << m_editTaskName->text() << "\n";
    out << QString("         导出时间: ") << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
    out << "======================================================\n\n";

    out << QString("【一、综合评估终端结果】\n");
    out << m_reportConsole->toPlainText() << "\n\n";

    out << "======================================================\n";
    out << QString("【二、系统运行实时追踪流水日志】\n");
    out << m_logConsole->toPlainText() << "\n";

    file.close();

    QFileInfo fileInfo(fileName);
    QString plotsDirPath = fileInfo.absolutePath() + "/" + fileInfo.completeBaseName() + "_Plots";
    QDir dir;
    if (!dir.exists(plotsDirPath)) {
        dir.mkpath(plotsDirPath);
    }

    auto savePlot = [&](QCustomPlot* plot, const QString& defaultName) {
        if (!plot) return;
        QString title = defaultName;
        if (plot->plotLayout()->rowCount() > 0 && plot->plotLayout()->columnCount() > 0) {
            if (auto* textElement = qobject_cast<QCPTextElement*>(plot->plotLayout()->element(0, 0))) {
                if (!textElement->text().isEmpty()) {
                    title = textElement->text();
                }
            }
        }
        title.replace(QRegularExpression("[\\\\/:*?\"<>|\\n]"), "_");

        QString imgPath = plotsDirPath + "/" + title + ".png";

        // =========================================================
        // 【修改点】：强制使用弹出独立窗口的默认大尺寸 (800x600)
        // 采用 std::max 是为了防止：如果全屏时图表比 800x600 还要大，
        // 就保留更大的极致清晰度；如果被挤压，则底线撑到 800x600。
        // =========================================================
        int w = std::max(plot->width(), 800);
        int h = std::max(plot->height(), 600);

        plot->savePng(imgPath, w, h);
    };

    savePlot(m_timeAzimuthPlot, "TimeAzimuthPlot");
    savePlot(m_spatialPlot, "SpatialPlot");

    for (int tid : m_lsPlots.keys()) {
        savePlot(m_lsPlots[tid], QString("Target_%1_LS").arg(tid));
        savePlot(m_lofarPlots[tid], QString("Target_%1_LOFAR").arg(tid));
        savePlot(m_demonPlots[tid], QString("Target_%1_DEMON").arg(tid));
    }

    savePlot(m_cbfWaterfallPlot, "CBF_Waterfall");
    savePlot(m_dcvWaterfallPlot, "DCV_Waterfall");

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

    saveLayoutPlots(m_sliceLayout, "TargetSlice");
    saveLayoutPlots(m_lofarWaterfallLayout, "OfflineLofar");

    // 【新增】：将 Tab4 仪表盘上的所有图表也全部加入导出名单
    savePlot(m_plotTargetAccuracy, "Dashboard_Target_Accuracy");
    savePlot(m_plotBatchAccuracy, "Dashboard_Batch_Trend");
    if (m_plotTrueAzimuth) savePlot(m_plotTrueAzimuth, "Dashboard_TrueAzimuth_Trend");
    if (m_plotCalcAzimuth) savePlot(m_plotCalcAzimuth, "Dashboard_CalcAzimuth_Trend");

    if (m_tableTargetFeatures) {
        m_tableTargetFeatures->grab().save(plotsDirPath + "/Dashboard_1_Table.png");
    }
    if (m_lblStatTime && m_lblStatTime->parentWidget()) {
        m_lblStatTime->parentWidget()->grab().save(plotsDirPath + "/Dashboard_2_Card_Time.png");
    }
    if (m_lblStatTargets && m_lblStatTargets->parentWidget()) {
        m_lblStatTargets->parentWidget()->grab().save(plotsDirPath + "/Dashboard_3_Card_Targets.png");
    }
    if (m_lblStatAvgAcc && m_lblStatAvgAcc->parentWidget()) {
        m_lblStatAvgAcc->parentWidget()->grab().save(plotsDirPath + "/Dashboard_4_Card_AvgAccuracy.png");
    }

    if (m_mainTabWidget && m_mainTabWidget->count() >= 4) {
        QWidget* tab4 = m_mainTabWidget->widget(3);
        if (tab4) {
            tab4->grab().save(plotsDirPath + "/Dashboard_Full_Panel.png");
        }
    }

    appendLog(QString("\n>> 成功：分析报表已完整导出至 %1\n").arg(fileName));
    appendLog(QString(">> 成功：所有配套图表已导出至文件夹 %1\n").arg(plotsDirPath));

    QMessageBox::information(this, "导出成功",
                             QString("综合评估报表及运行日志已成功导出！\n\n此外，当前所有图表及面板也已自动高分辨率保存为图片，位于同级配套目录：\n%1").arg(plotsDirPath));
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
    m_plotTitle->setText(QString("宽带实时折线图 (第%1帧 | 时间: %2s)").arg(result.frameIndex).arg(result.timestamp));
    m_spatialPlot->replot();
    updatePlotOriginalRange(m_spatialPlot);

    for (double ang : result.detectedAngles) m_timeAzimuthPlot->graph(0)->addData(ang, result.timestamp);
    m_timeAzimuthPlot->yAxis->setRange(std::max(0.0, result.timestamp - 30.0), result.timestamp + 5.0);

    bool foundX;
    QCPRange xRange = m_timeAzimuthPlot->graph(0)->getKeyRange(foundX);
    if (foundX) {
        m_timeAzimuthPlot->xAxis->setRange(xRange.lower - 5.0, xRange.upper + 5.0);
    } else {
        m_timeAzimuthPlot->xAxis->setRange(0, 180);
    }
    m_timeAzimuthPlot->replot();
    updatePlotOriginalRange(m_timeAzimuthPlot);

    for (const TargetTrack& t : result.tracks) {
        // 【意见二】：目标指示灯常亮逻辑 (只对转正目标生成)
        if (t.isConfirmed) {
            if (!m_targetLights.contains(t.id)) {
                QLabel* light = new QLabel(this);
                light->setAlignment(Qt::AlignCenter);
                m_targetLightsLayout->addWidget(light);
                m_targetLights.insert(t.id, light);
            }

            QLabel* light = m_targetLights[t.id];
            QString displayName = m_targetNames.value(t.id, QString("T%1").arg(t.id));

            if (t.isActive) {
                light->setText(QString("🟢 %1").arg(displayName));
                light->setStyleSheet("background-color: #eafaf1; color: #27ae60; font-size: 13px; font-weight: bold; border: 1px solid #27ae60; border-radius: 4px; padding: 3px 6px; margin-right: 2px;");
            } else {
                light->setText(QString("⚪ %1 (熄灭)").arg(displayName));
                light->setStyleSheet("background-color: #f0f0f0; color: #7f8c8d; font-size: 13px; font-weight: bold; border: 1px solid #bdc3c7; border-radius: 4px; padding: 3px 6px; margin-right: 2px;");
            }
        }

        if (!m_lofarPlots.contains(t.id)) createTargetPlots(t.id);

        QCustomPlot* lsp = m_lsPlots[t.id]; QCustomPlot* lp = m_lofarPlots[t.id]; QCustomPlot* dp = m_demonPlots[t.id];
        QString statusStr = t.isActive ? "[跟踪中]" : "[已熄火]";
        QColor lsColor = t.isActive ? Qt::red : Qt::darkGray; QColor lofarColor = t.isActive ? Qt::blue : Qt::darkGray; QColor demonColor = t.isActive ? Qt::darkGreen : Qt::darkGray;
        QColor bgColor = t.isActive ? Qt::white : QColor(240, 240, 240); QColor textColor = t.isActive ? Qt::black : Qt::gray;

        lsp->setBackground(bgColor); lp->setBackground(bgColor); dp->setBackground(bgColor);

        QString displayName = m_targetNames.value(t.id, QString("目标%1").arg(t.id));

        QString t1 = QString("%1 (方位: %2°) 拾取线谱 (第%3帧)").arg(displayName).arg(t.currentAngle, 0, 'f', 1).arg(result.frameIndex);
        QString t2 = QString("%1 (方位: %2°) LOFAR %3").arg(displayName).arg(t.currentAngle, 0, 'f', 1).arg(statusStr);
        QString t3 = t.isActive ? QString("%1 (方位: %2°) 轴频: %3Hz").arg(displayName).arg(t.currentAngle, 0, 'f', 1).arg(t.shaftFreq, 0, 'f', 1)
                                : QString("%1 (方位: %2°) 轴频: --Hz").arg(displayName).arg(t.currentAngle, 0, 'f', 1);

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
    updateTab2Plots();
}


void MainWindow::appendLog(const QString& log) { m_logConsole->appendPlainText(log); m_logConsole->moveCursor(QTextCursor::End); }

void MainWindow::appendReport(const QString& report) {
    QString finalReport = report;

    if (report.contains("综合判别: [")) {
        QStringList lines = report.split('\n');
        int currentTarget = -1;
        double currentTrueDepth = 0.0;

        for (const QString& line : lines) {
            if (line.contains("▶ 目标 ")) {
                QRegularExpression re("▶ 目标 (\\d+)：");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) currentTarget = match.captured(1).toInt();
            }
            else if (line.contains("真实深度:")) {
                QRegularExpression re("真实深度:\\s*([\\d\\.]+)\\s*m");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) currentTrueDepth = match.captured(1).toDouble();
            }
            else if (line.contains("综合判别: [") && currentTarget != -1) {
                QRegularExpression re("综合判别:\\s*\\[(.*?)\\]\\s*->\\s*判别(正确|错误)");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) {
                    QString estClass = match.captured(1);
                    bool isCorrect = (match.captured(2) == "正确");

                    QString trueClass = (currentTrueDepth > 20.0) ? "水下潜艇" : "水面舰船";

                    TargetClassInfo info;
                    info.trueClass = trueClass;
                    info.estClass = estClass;
                    info.isCorrect = isCorrect;
                    m_targetClasses[currentTarget] = info;
                }
                currentTarget = -1;
            }
        }
    }

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

    closePopupsFromLayout(m_lofarWaterfallLayout);
    QLayoutItem* item;
    while ((item = m_lofarWaterfallLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    int col = 0;
    for (const auto& res : results) {
        QCustomPlot* pRaw = new QCustomPlot(m_lofarWaterfallWidget);
        pRaw->setObjectName(QString("offline_raw_%1").arg(res.targetId));
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
        pTpsw->setObjectName(QString("offline_tpsw_%1").arg(res.targetId));
        setupPlotInteraction(pTpsw);
        pTpsw->setMinimumSize(400, 250); m_lofarWaterfallLayout->addWidget(pTpsw, 1, col);
        pTpsw->plotLayout()->insertRow(0);
        pTpsw->plotLayout()->addElement(0, 0, new QCPTextElement(pTpsw, QString("目标%1 历史LOFAR谱 (TPSW背景均衡)").arg(res.targetId), QFont("sans", 10, QFont::Bold)));
        QCPColorMap *cmapTpsw = new QCPColorMap(pTpsw->xAxis, pTpsw->yAxis);
        cmapTpsw->data()->setSize(res.freqBins, res.timeFrames); cmapTpsw->data()->setRange(QCPRange(0, m_currentConfig.fs/2.0), QCPRange(res.minTime, res.maxTime));

        double tpswMaxVal = -9999.0;
        for(int t=0; t<res.timeFrames; ++t) {
            for(int f=0; f<res.freqBins; ++f) {
                double val = res.tpswLofarDb[t * res.freqBins + f];
                cmapTpsw->data()->setCell(f, t, val);
                if (val > tpswMaxVal) tpswMaxVal = val;
            }
        }
        cmapTpsw->setGradient(QCPColorGradient::gpJet); cmapTpsw->setInterpolate(true);

        double lowerBound = (tpswMaxVal > 15.0) ? (tpswMaxVal - 10.0) : 3.0;

        cmapTpsw->setDataRange(QCPRange(lowerBound, tpswMaxVal));
        cmapTpsw->setTightBoundary(true);

        pTpsw->xAxis->setLabel("频率/Hz"); pTpsw->yAxis->setLabel("物理时间/s");
        pTpsw->xAxis->setRange(res.displayFreqMin, res.displayFreqMax); pTpsw->yAxis->setRange(res.minTime, res.maxTime);
        updatePlotOriginalRange(pTpsw);

        QCustomPlot* pDp = new QCustomPlot(m_lofarWaterfallWidget);
        pDp->setObjectName(QString("offline_dp_%1").arg(res.targetId));
        setupPlotInteraction(pDp);
        pDp->setMinimumSize(400, 250); m_lofarWaterfallLayout->addWidget(pDp, 2, col);
        pDp->plotLayout()->insertRow(0);
        pDp->plotLayout()->addElement(0, 0, new QCPTextElement(pDp, QString("目标%1 专属线谱连续轨迹图 (DP寻优)").arg(res.targetId), QFont("sans", 10, QFont::Bold)));
        QCPColorMap *cmapDp = new QCPColorMap(pDp->xAxis, pDp->yAxis);
        cmapDp->data()->setSize(res.freqBins, res.timeFrames); cmapDp->data()->setRange(QCPRange(0, m_currentConfig.fs/2.0), QCPRange(res.minTime, res.maxTime));
        for(int t=0; t<res.timeFrames; ++t) for(int f=0; f<res.freqBins; ++f) cmapDp->data()->setCell(f, t, res.dpCounter[t * res.freqBins + f]);

        // ==========================================================
        // 【优化四】：强行开启双线性插值！搭配上方传来的 500 超高能量，
        // 将在降采样时完美融合为一根显眼的连续红线！
        // ==========================================================
        cmapDp->setGradient(QCPColorGradient::gpJet);
        cmapDp->setInterpolate(false);  // 这里由 false 改为了 true！
        cmapDp->setDataRange(QCPRange(0, 10));
        cmapDp->setTightBoundary(true);

        pDp->xAxis->setLabel("频率/Hz"); pDp->yAxis->setLabel("物理时间/s");
        pDp->xAxis->setRange(res.displayFreqMin, res.displayFreqMax); pDp->yAxis->setRange(res.minTime, res.maxTime);
        updatePlotOriginalRange(pDp);

        col++;
    }
}


void MainWindow::updateTab2Plots() {
    if (m_historyResults.isEmpty()) return;
    int num_frames = m_historyResults.size();
    double min_time = m_historyResults.first().timestamp;
    double max_time = m_historyResults.last().timestamp;
    if (std::abs(max_time - min_time) < 0.1) max_time = min_time + 3.0;

    int nx_uniform = 361;
    QCPColorMap *cmapCbf = nullptr;
    if (m_cbfWaterfallPlot->plottableCount() > 0) cmapCbf = qobject_cast<QCPColorMap*>(m_cbfWaterfallPlot->plottable(0));
    if (!cmapCbf) cmapCbf = new QCPColorMap(m_cbfWaterfallPlot->xAxis, m_cbfWaterfallPlot->yAxis);

    QCPColorMap *cmapDcv = nullptr;
    if (m_dcvWaterfallPlot->plottableCount() > 0) cmapDcv = qobject_cast<QCPColorMap*>(m_dcvWaterfallPlot->plottable(0));
    if (!cmapDcv) cmapDcv = new QCPColorMap(m_dcvWaterfallPlot->xAxis, m_dcvWaterfallPlot->yAxis);

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

    cmapCbf->setGradient(QCPColorGradient::gpJet); cmapCbf->setInterpolate(true);
    cmapCbf->setDataRange(QCPRange(cbf_max - 20.0, cbf_max)); cmapCbf->setTightBoundary(true);
    m_cbfWaterfallPlot->xAxis->setLabel("方位角/°"); m_cbfWaterfallPlot->yAxis->setLabel("物理时间/s");
    m_cbfWaterfallPlot->xAxis->setRange(0, 180); m_cbfWaterfallPlot->yAxis->setRange(min_time, max_time);
    m_cbfWaterfallPlot->replot(); updatePlotOriginalRange(m_cbfWaterfallPlot);

    cmapDcv->setGradient(QCPColorGradient::gpJet); cmapDcv->setInterpolate(true);
    cmapDcv->setDataRange(QCPRange(dcv_max - 20.0, dcv_max)); cmapDcv->setTightBoundary(true);
    m_dcvWaterfallPlot->xAxis->setLabel("方位角/°"); m_dcvWaterfallPlot->yAxis->setLabel("物理时间/s");
    m_dcvWaterfallPlot->xAxis->setRange(0, 180); m_dcvWaterfallPlot->yAxis->setRange(min_time, max_time);
    m_dcvWaterfallPlot->replot(); updatePlotOriginalRange(m_dcvWaterfallPlot);

    QSet<int> targetIds;
    for (const auto& frame : m_historyResults) {
        for (const auto& tr : frame.tracks) {
            if (tr.isConfirmed) targetIds.insert(tr.id);
        }
    }
    QList<int> sortedIds = targetIds.values(); std::sort(sortedIds.begin(), sortedIds.end());

    QList<QCustomPlot*> allPlots = m_sliceWidget->findChildren<QCustomPlot*>();
    for (QCustomPlot* plot : allPlots) {
        QString objName = plot->objectName();
        if (objName.startsWith("slice_")) {
            int plotTid = objName.split("_").last().toInt();
            if (!targetIds.contains(plotTid)) { m_sliceLayout->removeWidget(plot); plot->deleteLater(); }
        }
    }

    int col = 0;
    for (int tid : sortedIds) {
        int active_frames = 0; double sum_ang = 0.0;
        QVector<double> slice_cbf_sum; QVector<double> slice_dcv_sum;

        for (const auto& frame : m_historyResults) {
            for (const auto& tr : frame.tracks) {
                if (tr.id == tid && tr.isActive && !tr.lofarFullLinear.isEmpty() && !tr.cbfLofarFullLinear.isEmpty()) {
                    if (slice_dcv_sum.isEmpty()) {
                        slice_cbf_sum.resize(tr.cbfLofarFullLinear.size()); slice_dcv_sum.resize(tr.lofarFullLinear.size());
                        slice_cbf_sum.fill(0.0); slice_dcv_sum.fill(0.0);
                    }
                    for(int i=0; i<slice_dcv_sum.size(); ++i) {
                        slice_cbf_sum[i] += tr.cbfLofarFullLinear[i]; slice_dcv_sum[i] += tr.lofarFullLinear[i];
                    }
                    sum_ang += tr.currentAngle; active_frames++;
                    break;
                }
            }
        }

        if (active_frames > 0 && !slice_dcv_sum.isEmpty()) {
            double avg_ang = sum_ang / active_frames;
            std::vector<double> v_cbf(slice_cbf_sum.size()), v_dcv(slice_dcv_sum.size());
            for(int i=0; i<slice_dcv_sum.size(); ++i) {
                v_cbf[i] = slice_cbf_sum[i] / active_frames; v_dcv[i] = slice_dcv_sum[i] / active_frames;
            }
            double max_cbf = *std::max_element(v_cbf.begin(), v_cbf.end());
            double max_dcv = *std::max_element(v_dcv.begin(), v_dcv.end());

            QVector<double> f_axis(v_dcv.size()); QVector<double> cbf_db(v_cbf.size()); QVector<double> dcv_db(v_dcv.size());
            double df_calc = (v_dcv.size() > 1) ? (m_currentConfig.fs / 2.0) / (v_dcv.size() - 1) : 1.0;
            for(int i=0; i<v_dcv.size(); ++i) {
                f_axis[i] = i * df_calc;
                dcv_db[i] = std::max(-80.0, 10.0 * std::log10(v_dcv[i] / (max_dcv + 1e-12) + 1e-12));
                cbf_db[i] = std::max(-80.0, 10.0 * std::log10(v_cbf[i] / (max_cbf + 1e-12) + 1e-12));
            }

            QString cbfName = QString("slice_cbf_%1").arg(tid);
            QCustomPlot* pCbf = m_sliceWidget->findChild<QCustomPlot*>(cbfName);
            if (!pCbf) {
                pCbf = new QCustomPlot(m_sliceWidget); pCbf->setObjectName(cbfName); setupPlotInteraction(pCbf);
                pCbf->setMinimumSize(400, 250); pCbf->addGraph(); pCbf->graph(0)->setPen(QPen(Qt::gray, 2.0));
                pCbf->plotLayout()->insertRow(0); pCbf->plotLayout()->addElement(0, 0, new QCPTextElement(pCbf, "", QFont("sans", 10, QFont::Bold)));
                pCbf->xAxis->setRange(m_currentConfig.lofarMin, m_currentConfig.lofarMax); pCbf->yAxis->setRange(-80, 5);
                pCbf->xAxis->setVisible(false); m_sliceLayout->addWidget(pCbf, 0, col);
            }
            pCbf->graph(0)->setData(f_axis, cbf_db);
            if (auto* title = qobject_cast<QCPTextElement*>(pCbf->plotLayout()->element(0, 0))) title->setText(QString("目标%1 (约 %2°) - CBF").arg(tid).arg(avg_ang, 0, 'f', 1));
            pCbf->yAxis->setLabel(col == 0 ? "相对功率 / dB" : ""); pCbf->replot(); updatePlotOriginalRange(pCbf);

            QString dcvName = QString("slice_dcv_%1").arg(tid);
            QCustomPlot* pDcv = m_sliceWidget->findChild<QCustomPlot*>(dcvName);
            if (!pDcv) {
                pDcv = new QCustomPlot(m_sliceWidget); pDcv->setObjectName(dcvName); setupPlotInteraction(pDcv);
                pDcv->setMinimumSize(400, 250); pDcv->addGraph(); pDcv->graph(0)->setPen(QPen(Qt::red, 1.5));
                pDcv->plotLayout()->insertRow(0); pDcv->plotLayout()->addElement(0, 0, new QCPTextElement(pDcv, "", QFont("sans", 10, QFont::Bold)));
                pDcv->xAxis->setRange(m_currentConfig.lofarMin, m_currentConfig.lofarMax); pDcv->yAxis->setRange(-80, 5);
                pDcv->xAxis->setLabel("频率 / Hz"); m_sliceLayout->addWidget(pDcv, 1, col);
            }
            pDcv->graph(0)->setData(f_axis, dcv_db);

            // ========================================================
            // 【新增】：在 DCV 上画出累积提取的蓝点线谱
            // ========================================================
            if (pDcv->graphCount() < 2) {
                pDcv->addGraph();
                pDcv->graph(1)->setLineStyle(QCPGraph::lsNone);
                pDcv->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, Qt::blue, Qt::white, 6));
            }
            QVector<double> peakF, peakA;
            const TargetTrack* lastTr = nullptr;
            for(int k=m_historyResults.size()-1; k>=0; --k) {
                for(const auto& tr : m_historyResults[k].tracks) {
                    if (tr.id == tid) { lastTr = &tr; break; }
                }
                if (lastTr) break;
            }
            if (lastTr && !lastTr->lineSpectraDcv.empty()) {
                for(double f : lastTr->lineSpectraDcv) {
                    int bin = std::round(f / df_calc);
                    if(bin >= 0 && bin < dcv_db.size()) { peakF.append(f); peakA.append(dcv_db[bin]); }
                }
            }
            pDcv->graph(1)->setData(peakF, peakA);
            // ========================================================

            if (auto* title = qobject_cast<QCPTextElement*>(pDcv->plotLayout()->element(0, 0))) title->setText(QString("目标%1 (约 %2°) - DCV").arg(tid).arg(avg_ang, 0, 'f', 1));
            pDcv->yAxis->setLabel(col == 0 ? "相对功率 / dB" : ""); pDcv->replot(); updatePlotOriginalRange(pDcv);

            col++;
        }
    }
}

void MainWindow::onBatchAccuracyComputed(int batchIndex, double accuracy) {
    m_batchAccuracies.append(qMakePair(batchIndex, accuracy));

    QVector<double> batchX, batchY;
    for (const auto& pair : m_batchAccuracies) {
        batchX.append(pair.first);
        batchY.append(pair.second);
    }

    // 【强绘图检查】确保 Graph 存在，强制绑定数据并刷新界限
    if (m_plotBatchAccuracy && m_plotBatchAccuracy->graphCount() > 0) {
        m_plotBatchAccuracy->graph(0)->setData(batchX, batchY);
        m_plotBatchAccuracy->xAxis->setRange(0, batchX.isEmpty() ? 5 : batchX.last() + 1);
        m_plotBatchAccuracy->yAxis->setRange(0, 105); // 强制规范Y轴高度
        m_plotBatchAccuracy->replot();
        updatePlotOriginalRange(m_plotBatchAccuracy);
    }
}

QWidget* MainWindow::createCardWidget(QLabel* contentLabel, const QString& bgColor, const QString& title) {
    QFrame* frame = new QFrame();
    frame->setStyleSheet(QString("QFrame { background-color: %1; border-radius: 10px; border: 1px solid #dcdde1; }").arg(bgColor));
    QVBoxLayout* layout = new QVBoxLayout(frame);

    QLabel* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet("color: #7f8c8d; font-size: 14px; font-weight: bold; border: none;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    contentLabel->setStyleSheet("color: #2c3e50; font-size: 22px; font-weight: bold; border: none;");
    contentLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(contentLabel);

    return frame;
}


void MainWindow::onMfpResultReady(const QList<TargetEvaluation>& mfpResults) {
    disconnect(m_tableMfpResults, &QTableWidget::itemChanged, this, &MainWindow::onTargetNameChanged);
    m_tableMfpResults->setRowCount(0);

    for (int i = 0; i < mfpResults.size(); ++i) {
        const auto& mfp = mfpResults[i];
        m_latestMfpResults[mfp.targetId] = mfp;

        // 【核心修复】：先执行累加逻辑，再进行 UI 渲染
        // 这样可以确保当前这一批次的结果立即计入 n/m 统计
        m_mfpTotalCounts[mfp.targetId]++;
        if (mfp.isMfpCorrect) m_mfpCorrectCounts[mfp.targetId]++;

        m_tableMfpResults->insertRow(i);
        QString displayName = m_targetNames.value(mfp.targetId, QString("Target %1").arg(mfp.targetId));
        auto* nameItem = new QTableWidgetItem(displayName);
        nameItem->setData(Qt::UserRole, mfp.targetId);
        nameItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled);
        m_tableMfpResults->setItem(i, 0, nameItem);

        m_tableMfpResults->setItem(i, 1, new QTableWidgetItem(QString("%1 m").arg(mfp.estimatedDepth, 0, 'f', 1)));
        m_tableMfpResults->setItem(i, 2, new QTableWidgetItem(QString("%1 m").arg(mfp.trueDepth, 0, 'f', 1)));

        auto* trueClassItem = new QTableWidgetItem(mfp.trueClass);
        if (mfp.trueClass == "水下潜艇") trueClassItem->setForeground(QBrush(QColor(41, 128, 185)));
        else trueClassItem->setForeground(QBrush(QColor(142, 68, 173)));
        m_tableMfpResults->setItem(i, 3, trueClassItem);

        auto* sysClassItem = new QTableWidgetItem(mfp.targetClass);
        if (mfp.targetClass == "水下潜艇") sysClassItem->setForeground(QBrush(QColor(41, 128, 185)));
        else sysClassItem->setForeground(QBrush(QColor(142, 68, 173)));
        m_tableMfpResults->setItem(i, 4, sysClassItem);

        // 使用更新后的 m_mfpCorrectCounts 和 m_mfpTotalCounts 进行显示
        QString resStr = mfp.isMfpCorrect ?
            QString("✅ 正确 (%1/%2)").arg(m_mfpCorrectCounts[mfp.targetId]).arg(m_mfpTotalCounts[mfp.targetId]) :
            QString("❌ 错误 (%1/%2)").arg(m_mfpCorrectCounts[mfp.targetId]).arg(m_mfpTotalCounts[mfp.targetId]);
        QColor resColor = mfp.isMfpCorrect ? QColor(39, 174, 96) : Qt::red;

        auto* resItem = new QTableWidgetItem(resStr);
        resItem->setForeground(QBrush(resColor));
        QFont f = resItem->font(); f.setBold(true); resItem->setFont(f);
        m_tableMfpResults->setItem(i, 5, resItem);

        for(int col = 0; col < 6; ++col) {
            if(m_tableMfpResults->item(i, col)) m_tableMfpResults->item(i, col)->setTextAlignment(Qt::AlignCenter);
        }
    }
    connect(m_tableMfpResults, &QTableWidget::itemChanged, this, &MainWindow::onTargetNameChanged);
}



void MainWindow::onEvaluationResultReady(const SystemEvaluationResult& res) {
    m_lblStatTime->setText(QString("<span style='font-size:32px; color:#27ae60;'>%1</span>"
                                   "<span style='font-size:16px; color:#7f8c8d;'> s </span>"
                                   "<span style='font-size:14px; color:#7f8c8d;'>(实时 </span>"
                                   "<span style='font-size:18px; color:#2980b9;'>%2</span>"
                                   "<span style='font-size:14px; color:#7f8c8d;'> s | 批处理 </span>"
                                   "<span style='font-size:18px; color:#e67e22;'>%3</span>"
                                   "<span style='font-size:14px; color:#7f8c8d;'> s)</span>")
                           .arg(res.totalTimeSec, 0, 'f', 1)
                           .arg(res.realtimeTimeSec, 0, 'f', 1)
                           .arg(res.batchTimeSec, 0, 'f', 2));

    m_lblStatTargets->setText(QString("<span style='font-size:36px; color:#2980b9;'>%1</span> 艘").arg(res.confirmedTargetCount));

    disconnect(m_tableTargetFeatures, &QTableWidget::itemChanged, this, &MainWindow::onTargetNameChanged);

    m_tableTargetFeatures->setRowCount(0);
    m_tableMfpResults->setVisible(res.isMfpEnabled);

    double totalAccInstant = 0.0;
    double totalAccDcv = 0.0;
    int validAccCount = 0;
    bool anyHasTruth = false;

    QVector<double> ticks, accDataInst, accDataDcv;
    QVector<QString> labels;

    static double lastTotalTime = -1.0;
    static int currentBatchIndex = 0;
    bool isNewRun = (res.totalTimeSec < lastTotalTime);
    lastTotalTime = res.totalTimeSec;

    if (isNewRun) {
        currentBatchIndex = 0;
        m_plotBatchAccuracy->graph(0)->data()->clear();
        m_plotTrueAzimuth->clearGraphs();
        m_plotCalcAzimuth->clearGraphs();
        m_trueAzimuthGraphs.clear();
        m_calcAzimuthGraphs.clear();
        m_tableMfpResults->setRowCount(0);
        m_mfpCorrectCounts.clear();
        m_mfpTotalCounts.clear();
        m_latestMfpResults.clear();
    }
    currentBatchIndex++;

    QList<QColor> colorPalette = {QColor(41, 128, 185), QColor(39, 174, 96), QColor(211, 84, 0), QColor(142, 68, 173), QColor(192, 57, 43), QColor(22, 160, 133)};

    for (int i = 0; i < res.targetEvals.size(); ++i) {
        const auto& eval = res.targetEvals[i];
        QString displayName = m_targetNames.value(eval.targetId, QString("Target %1").arg(eval.targetId));

        m_tableTargetFeatures->insertRow(i);

        auto* nameItem = new QTableWidgetItem(displayName);
        nameItem->setData(Qt::UserRole, eval.targetId);
        nameItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled);
        m_tableTargetFeatures->setItem(i, 0, nameItem);

        m_tableTargetFeatures->setItem(i, 1, new QTableWidgetItem(eval.lineSpectraStr));

        auto* itemAccInst = new QTableWidgetItem(QString("%1%").arg(eval.accuracy, 0, 'f', 1));
        itemAccInst->setForeground(eval.accuracy >= 60.0 ? QBrush(QColor(46, 204, 113)) : QBrush(Qt::red));
        itemAccInst->setFont(QFont("sans", 10, QFont::Bold));
        m_tableTargetFeatures->setItem(i, 2, itemAccInst);

        m_tableTargetFeatures->setItem(i, 3, new QTableWidgetItem(eval.lineSpectraStrDcv));

        auto* itemAccDcv = new QTableWidgetItem(QString("%1%").arg(eval.accuracyDcv, 0, 'f', 1));
        itemAccDcv->setForeground(eval.accuracyDcv >= 60.0 ? QBrush(QColor(46, 204, 113)) : QBrush(Qt::red));
        itemAccDcv->setFont(QFont("sans", 10, QFont::Bold));
        m_tableTargetFeatures->setItem(i, 4, itemAccDcv);

        QString shaftStr = eval.shaftFreq > 0 ? QString("%1 Hz").arg(eval.shaftFreq, 0, 'f', 1) : "未检测到";
        m_tableTargetFeatures->setItem(i, 5, new QTableWidgetItem(shaftStr));

        QString trueAngStr = eval.hasTruth ? QString("%1° -> %2°").arg(eval.initialTrueAngle, 0, 'f', 1).arg(eval.currentTrueAngle, 0, 'f', 1) : "--";
        m_tableTargetFeatures->setItem(i, 6, new QTableWidgetItem(trueAngStr));

        QString calcAngStr = eval.initialCalcAngle >= 0 ? QString("%1° -> %2°").arg(eval.initialCalcAngle, 0, 'f', 1).arg(eval.currentCalcAngle, 0, 'f', 1) : "--";
        m_tableTargetFeatures->setItem(i, 7, new QTableWidgetItem(calcAngStr));

        double bestAcc = std::max(eval.accuracy, eval.accuracyDcv);
        QString judge; QColor judgeColor;
        if (bestAcc >= 80.0) { judge = "高可信"; judgeColor = QColor(46, 204, 113); }
        else if (bestAcc >= 60.0) { judge = "可信"; judgeColor = QColor(241, 196, 15); }
        else { judge = "弱特征"; judgeColor = Qt::red; }

        auto* itemJudge = new QTableWidgetItem(judge);
        itemJudge->setForeground(QBrush(judgeColor));
        QFont judgeFont = itemJudge->font(); judgeFont.setBold(true); itemJudge->setFont(judgeFont);
        m_tableTargetFeatures->setItem(i, 8, itemJudge);

        for(int col = 0; col < 9; ++col) {
            if(m_tableTargetFeatures->item(i, col)) {
                m_tableTargetFeatures->item(i, col)->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                m_tableTargetFeatures->item(i, col)->setTextAlignment(Qt::AlignCenter);
            }
        }
        m_tableTargetFeatures->item(i, 0)->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled);

        totalAccInstant += eval.accuracy;
        totalAccDcv += eval.accuracyDcv;
        validAccCount++;

        ticks.append(i + 1);
        labels.append(QString("T%1").arg(eval.targetId));
        accDataInst.append(eval.accuracy);
        accDataDcv.append(eval.accuracyDcv);

        if (eval.hasTruth) anyHasTruth = true;

        QColor tColor = colorPalette[eval.targetId % colorPalette.size()];
        if (!m_calcAzimuthGraphs.contains(eval.targetId)) {
            QCPGraph* gCalc = m_plotCalcAzimuth->addGraph();
            gCalc->setName(QString("目标 %1").arg(eval.targetId));
            gCalc->setPen(QPen(tColor, 2));
            gCalc->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, tColor, Qt::white, 6));
            m_calcAzimuthGraphs[eval.targetId] = gCalc;
            if (eval.initialCalcAngle >= 0) gCalc->addData(0, eval.initialCalcAngle);

            if (eval.hasTruth) {
                QCPGraph* gTrue = m_plotTrueAzimuth->addGraph();
                gTrue->setName(QString("目标 %1").arg(eval.targetId));
                gTrue->setPen(QPen(tColor, 2, Qt::DashLine));
                gTrue->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCross, tColor, Qt::white, 6));
                m_trueAzimuthGraphs[eval.targetId] = gTrue;
                gTrue->addData(0, eval.initialTrueAngle);
            }
        }
        if (eval.currentCalcAngle >= 0) m_calcAzimuthGraphs[eval.targetId]->addData(currentBatchIndex, eval.currentCalcAngle);
        if (eval.hasTruth && m_trueAzimuthGraphs.contains(eval.targetId)) m_trueAzimuthGraphs[eval.targetId]->addData(currentBatchIndex, eval.currentTrueAngle);
    }

    connect(m_tableTargetFeatures, &QTableWidget::itemChanged, this, &MainWindow::onTargetNameChanged);

    double trueMin = 360.0, trueMax = -360.0;
    bool hasTrueRange = false;
    for (auto graph : m_trueAzimuthGraphs) {
        bool found; QCPRange r = graph->getValueRange(found);
        if (found) { trueMin = std::min(trueMin, r.lower); trueMax = std::max(trueMax, r.upper); hasTrueRange = true; }
    }
    if (hasTrueRange) m_plotTrueAzimuth->yAxis->setRange(trueMin - 5.0, trueMax + 5.0);
    else m_plotTrueAzimuth->yAxis->setRange(0, 180);

    double calcMin = 360.0, calcMax = -360.0;
    bool hasCalcRange = false;
    for (auto graph : m_calcAzimuthGraphs) {
        bool found; QCPRange r = graph->getValueRange(found);
        if (found) { calcMin = std::min(calcMin, r.lower); calcMax = std::max(calcMax, r.upper); hasCalcRange = true; }
    }
    if (hasCalcRange) m_plotCalcAzimuth->yAxis->setRange(calcMin - 5.0, calcMax + 5.0);
    else m_plotCalcAzimuth->yAxis->setRange(0, 180);

    m_plotTrueAzimuth->setVisible(anyHasTruth);
    m_plotTrueAzimuth->xAxis->setRange(0, currentBatchIndex + 1.5);
    m_plotTrueAzimuth->replot();

    m_plotCalcAzimuth->xAxis->setRange(0, currentBatchIndex + 1.5);
    m_plotCalcAzimuth->replot();

    double avgAccInstant = validAccCount > 0 ? (totalAccInstant / validAccCount) : 0.0;
    double avgAccDcv = validAccCount > 0 ? (totalAccDcv / validAccCount) : 0.0;
    m_lblStatAvgAcc->setText(QString("<span style='font-size:22px; color:#7f8c8d;'>瞬时 </span>"
                                     "<span style='font-size:28px; color:#e67e22;'>%1%</span>"
                                     "<span style='font-size:22px; color:#7f8c8d;'> | DCV </span>"
                                     "<span style='font-size:28px; color:#27ae60;'>%2%</span>")
                             .arg(avgAccInstant, 0, 'f', 1).arg(avgAccDcv, 0, 'f', 1));

    QSharedPointer<QCPAxisTickerText> textTicker(new QCPAxisTickerText);
    textTicker->addTicks(ticks, labels);
    m_plotTargetAccuracy->xAxis->setTicker(textTicker);
    m_accuracyBars->setData(ticks, accDataInst);
    auto barsDcv = qobject_cast<QCPBars*>(m_plotTargetAccuracy->plottable(1));
    if (barsDcv) barsDcv->setData(ticks, accDataDcv);
    m_plotTargetAccuracy->xAxis->setRange(0, res.targetEvals.size() + 1);
    m_plotTargetAccuracy->replot();

    m_plotBatchAccuracy->graph(0)->data()->clear();
    double maxBatchX = 0;
    for (int j = 0; j < m_batchAccuracies.size(); ++j) {
        m_plotBatchAccuracy->graph(0)->addData(m_batchAccuracies[j].first, m_batchAccuracies[j].second);
        if (m_batchAccuracies[j].first > maxBatchX) maxBatchX = m_batchAccuracies[j].first;
    }
    if (maxBatchX == 0) maxBatchX = currentBatchIndex;
    m_plotBatchAccuracy->xAxis->setRange(0, maxBatchX + 1.5);
    m_plotBatchAccuracy->replot();

    // ==========================================================
    // 合并打印终极评估报告
    // ==========================================================
    if (res.isFinal) {
        QString finalReport = "\n=================================================================================\n";
        finalReport += "                               高 级 航 迹 管 理 终 极 评 估 报 告                               \n";
        finalReport += "=================================================================================\n";
        finalReport += "【系统级总体评价】\n";
        finalReport += QString("  ▶ 全流程总计耗时: %1 秒 (实时解算: %2 秒 | 离线寻优: %3 秒)\n").arg(res.totalTimeSec, 0, 'f', 2).arg(res.realtimeTimeSec, 0, 'f', 2).arg(res.batchTimeSec, 0, 'f', 2);
        finalReport += QString("  ▶ 稳定提取目标数: %1 个\n\n").arg(res.confirmedTargetCount);

        if (res.isMfpEnabled) finalReport += "【最终目标水上/水下分辨与特征提取总结表】\n";
        else finalReport += "【最终目标特征提取池总结表】\n";
        finalReport += "---------------------------------------------------------------------------------\n";

        int total_mfp = 0, correct_mfp = 0;
        for (const auto& eval : res.targetEvals) {
            QString displayName = m_targetNames.value(eval.targetId, QString("Target %1").arg(eval.targetId));
            finalReport += QString("[%1]\n").arg(displayName);

            // 【移除末尾的Hz】：因为 eval.lineSpectraStr 里自带 "124.7Hz(20/20)"
            finalReport += QString("  - 瞬时线谱: %1 (准度: %2%) | DCV线谱: %3 (准度: %4%)\n")
                        .arg(eval.lineSpectraStr).arg(eval.accuracy, 0, 'f', 1)
                        .arg(eval.lineSpectraStrDcv).arg(eval.accuracyDcv, 0, 'f', 1);

            QString shaftStr = eval.shaftFreq > 0 ? QString("%1 Hz").arg(eval.shaftFreq, 0, 'f', 1) : "-";
            QString angStr = eval.currentCalcAngle >= 0 ? QString("%1° -> %2°").arg(eval.initialCalcAngle, 0, 'f', 1).arg(eval.currentCalcAngle, 0, 'f', 1) : "-";
            finalReport += QString("  - 稳定轴频: %1 | 方位历程: %2\n").arg(shaftStr).arg(angStr);

            if (res.isMfpEnabled && m_latestMfpResults.contains(eval.targetId)) {
                auto mfp = m_latestMfpResults[eval.targetId];
                QString depthStr = mfp.estimatedDepth >= 0 ? QString("%1 m").arg(mfp.estimatedDepth, 0, 'f', 1) : "-";
                QString judgeStr = mfp.isMfpCorrect ? "✅ 判别正确" : "❌ 判别错误";
                if (mfp.trueDepth >= 0) { total_mfp++; if (mfp.isMfpCorrect) correct_mfp++; }
                finalReport += QString("  - 估计深度: %1 | 物理类别: %2 | 判别结果: %3\n").arg(depthStr, -8).arg(mfp.targetClass, -8).arg(judgeStr);
            } else if (!res.isMfpEnabled) {
                double bestAcc = std::max(eval.accuracy, eval.accuracyDcv);
                QString judge = (bestAcc >= 80.0) ? "高可信" : (bestAcc >= 60.0 ? "可信" : "弱特征");
                finalReport += QString("  - 综合判定: %1 (置信度得分: %2)\n").arg(judge).arg(bestAcc, 0, 'f', 1);
            }
            finalReport += "---------------------------------------------------------------------------------\n";
        }
        if (res.isMfpEnabled && total_mfp > 0) {
            double global_acc = (double)correct_mfp / total_mfp * 100.0;
            finalReport += QString("【最终验收结论】全局 MFP 水上/水下判别正确率: %1%\n").arg(global_acc, 0, 'f', 2);
            finalReport += "=================================================================================\n\n";
        }

        appendReport(finalReport);
    }
}



void MainWindow::onTargetNameChanged(QTableWidgetItem* item) {
    if (!item || item->column() != 0) return; // 只关心第一列（目标名称列）

    // 取出我们在填充表格时偷偷绑定的真实 ID
    int targetId = item->data(Qt::UserRole).toInt();
    if (targetId > 0) {
        m_targetNames[targetId] = item->text();
        appendLog(QString(">> 目标重命名：Target %1 已重命名为 [%2]\n").arg(targetId).arg(item->text()));
    }
}
// 3. 实现槽函数：勾选时弹出文件选择框
void MainWindow::onDepthResolveToggled(bool checked) {
    if (checked) {
        QString path = QFileDialog::getOpenFileName(this,
                                                    "选择 Kraken 拷贝场文件",
                                                    m_currentDir,
                                                    "RAW Files (*.raw);;All Files (*)");
        if (path.isEmpty()) {
            // 用户取消了选择，恢复未勾选状态
            m_chkDepthResolve->blockSignals(true);
            m_chkDepthResolve->setChecked(false);
            m_chkDepthResolve->blockSignals(false);
        } else {
            m_krakenRawPath = path;
            appendLog(QString("✅ 已加载 MFP 拷贝场库: %1").arg(path));
        }
    } else {
        m_krakenRawPath.clear();
        appendLog("⛔ 已关闭 MFP 深度分辨功能。");
    }
}




#include "MainWindow.moc"
