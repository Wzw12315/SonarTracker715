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
#include <QApplication>
#include <QScreen>
#include <QDoubleSpinBox>

namespace {
// 【核心黑科技】：图表外骨骼包装器
QWidget* wrapPlotWithRangeControl(QCustomPlot* plot, const QString& labelText, double minVal, double maxVal, double defaultMin, double defaultMax) {
    QWidget* wrapper = new QWidget(plot->parentWidget());
    wrapper->setObjectName(plot->objectName() + "_wrapper");

    QVBoxLayout* layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(2);

    QWidget* ctrlBar = new QWidget(wrapper);
    ctrlBar->setStyleSheet("background-color: #1e1e1e; border-radius: 3px; border: 1px solid #333;");
    ctrlBar->setFixedHeight(26);
    QHBoxLayout* ctrlLayout = new QHBoxLayout(ctrlBar);
    ctrlLayout->setContentsMargins(5, 0, 5, 0);

    QLabel* lbl = new QLabel(labelText, ctrlBar);
    lbl->setStyleSheet("color: #a0a0a0; font-size: 11px; font-weight: bold; border: none;");

    QDoubleSpinBox* spinMin = new QDoubleSpinBox(ctrlBar);
    spinMin->setRange(minVal, maxVal); spinMin->setValue(defaultMin);
    spinMin->setDecimals(1); spinMin->setSingleStep(10.0);
    spinMin->setStyleSheet("QDoubleSpinBox { background: #121212; color: #2ecc71; border: 1px solid #555; padding: 1px; font-size: 11px; }");

    QDoubleSpinBox* spinMax = new QDoubleSpinBox(ctrlBar);
    spinMax->setRange(minVal, maxVal); spinMax->setValue(defaultMax);
    spinMax->setDecimals(1); spinMax->setSingleStep(10.0);
    spinMax->setStyleSheet("QDoubleSpinBox { background: #121212; color: #2ecc71; border: 1px solid #555; padding: 1px; font-size: 11px; }");

    // 【安全修复】：加入 plot 和 spinMin 作为 context 对象。如果目标被销毁，信号将自动断开，绝不闪退！
    QObject::connect(spinMin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), plot, [plot, spinMax](double v){
        if (v < spinMax->value()) { plot->xAxis->setRange(v, spinMax->value()); plot->replot(); }
    });
    QObject::connect(spinMax, QOverload<double>::of(&QDoubleSpinBox::valueChanged), plot, [plot, spinMin](double v){
        if (v > spinMin->value()) { plot->xAxis->setRange(spinMin->value(), v); plot->replot(); }
    });
    QObject::connect(plot->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged), spinMin, [spinMin, spinMax](const QCPRange& range){
        spinMin->blockSignals(true); spinMax->blockSignals(true);
        spinMin->setValue(range.lower); spinMax->setValue(range.upper);
        spinMin->blockSignals(false); spinMax->blockSignals(false);
    });

    ctrlLayout->addWidget(lbl); ctrlLayout->addWidget(spinMin);
    ctrlLayout->addWidget(new QLabel("-", ctrlBar)); ctrlLayout->addWidget(spinMax);
    ctrlLayout->addStretch();

    layout->addWidget(ctrlBar);
    plot->setParent(wrapper);
    layout->addWidget(plot, 1);
    // ========================================================
    // 【核心修复 1】：在包装器诞生时，立刻将图表的 X 轴锁定为默认的 min 和 max
    // 彻底消灭 QCustomPlot 烦人的 0~5 初始幽灵范围！
    // ========================================================
    plot->xAxis->setRange(defaultMin, defaultMax);
    return wrapper;
}
}


// ==================== UDP 网络配置窗口类 ====================
class UdpConfigDialog : public QDialog {
    //    Q_OBJECT
public:
    explicit UdpConfigDialog(const QString& locIp, quint16 locPort,
                             const QString& remIp, quint16 remPort, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("⚙️ UDP 网络双向通信配置");
        resize(400, 260); // 稍微加高一点，留出舒适的边距

        // ==========================================
        // 【新增】：UDP 弹窗专属高级暗黑样式，彻底修复边框与字体截断
        // ==========================================
        this->setStyleSheet(
                    "QDialog, QWidget { background-color: #121212; color: #e0e0e0; font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif; font-size: 13px; }"
                    "QLabel { color: #dcdde1; min-height: 26px; }"
                    "QLineEdit { background-color: #1a1a1a; border: 1px solid #444; padding: 4px; color: #2ecc71; font-weight: bold; border-radius: 3px; min-height: 22px; }"
                    "QPushButton { background-color: #1e1e1e; border: 1px solid #444; border-radius: 4px; padding: 6px 15px; color: #e0e0e0; font-weight: bold; min-height: 24px; }"
                    "QPushButton:hover { background-color: #2c3e50; border-color: #3498db; }"
                    "QPushButton:pressed { background-color: #2980b9; color: #ffffff; }"
                    /* 修复 QGroupBox 标题被截断的终极方案 */
                    "QGroupBox { border: 1px solid #333333; border-radius: 6px; margin-top: 22px; font-weight: bold; color: #2ecc71; padding-top: 15px; padding-bottom: 5px; }"
                    "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 5px; left: 10px; top: 0px; }"
                    );

        QVBoxLayout* layout = new QVBoxLayout(this);

        QGroupBox* grpLocal = new QGroupBox("📡 本地数据接收 (侦听)");
        QFormLayout* formLocal = new QFormLayout(grpLocal);
        // 增加行间距，避免文字拥挤
        formLocal->setVerticalSpacing(10);
        editLocIp = new QLineEdit(locIp, this);
        editLocPort = new QLineEdit(QString::number(locPort), this);
        formLocal->addRow("本地绑定 IP:", editLocIp);
        formLocal->addRow("接收数据端口:", editLocPort);

        QGroupBox* grpRemote = new QGroupBox("🕹️ 远端阵列控制 (发送指令)");
        QFormLayout* formRemote = new QFormLayout(grpRemote);
        // 增加行间距
        formRemote->setVerticalSpacing(10);
        editRemIp = new QLineEdit(remIp, this);
        editRemPort = new QLineEdit(QString::number(remPort), this);
        formRemote->addRow("目标发射端 IP:", editRemIp);
        formRemote->addRow("目标命令端口:", editRemPort);

        layout->addWidget(grpLocal);
        layout->addWidget(grpRemote);

        QHBoxLayout* btns = new QHBoxLayout();
        QPushButton* btnOk = new QPushButton("应用配置", this);
        QPushButton* btnCancel = new QPushButton("取消", this);

        // 保留你原本让“应用配置”按钮高亮的设定，并适配了暗黑边框
        btnOk->setStyleSheet("background-color: #3498db; color: white; font-weight: bold; border: 1px solid #2980b9;");

        btns->addStretch();
        btns->addWidget(btnOk);
        btns->addWidget(btnCancel);
        layout->addLayout(btns);

        connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QString getLocIp() const { return editLocIp->text().trimmed(); }
    quint16 getLocPort() const { return editLocPort->text().toUShort(); }
    QString getRemIp() const { return editRemIp->text().trimmed(); }
    quint16 getRemPort() const { return editRemPort->text().toUShort(); }

private:
    QLineEdit* editLocIp; QLineEdit* editLocPort;
    QLineEdit* editRemIp; QLineEdit* editRemPort;
};


// ==================== 独立真值配置与导入窗口类 ====================
class TruthInputDialog : public QDialog {
    //    Q_OBJECT
public:
    explicit TruthInputDialog(QWidget *parent = nullptr) : QDialog(parent) {
        // 【新增】：真值弹窗专门的高级暗黑样式 (修复 QGroupBox 标题截断)
        this->setStyleSheet(
                    "QDialog, QWidget { background-color: #121212; color: #e0e0e0; font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif; font-size: 13px; }"
                    "QLabel { color: #dcdde1; min-height: 26px; }"
                    "QLineEdit { background-color: #1a1a1a; border: 1px solid #444; padding: 4px; color: #2ecc71; font-weight: bold; border-radius: 3px; }"
                    "QPushButton { background-color: #1e1e1e; border: 1px solid #444; border-radius: 4px; padding: 6px 15px; color: #e0e0e0; font-weight: bold; }"
                    "QPushButton:hover { background-color: #2c3e50; border-color: #3498db; }"
                    /* 【核心修复 1】：margin-top 改为 22px，腾出顶部空间 */
                    "QGroupBox { border: 1px solid #333333; border-radius: 6px; margin-top: 22px; font-weight: bold; color: #2ecc71; padding-top: 10px; }"
                    /* 【核心修复 2】：top 改为 0px，不使用负数，增加 padding 防止紧贴边框 */
                    "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 5px; left: 10px; top: 0px; }"
                    "QScrollArea { border: none; background-color: transparent; }"
                    );
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
        //        group->setStyleSheet("QGroupBox { border: 1px solid #bdc3c7; border-radius: 5px; margin-top: 2ex; } QGroupBox::title { subcontrol-origin: margin; left: 10px; color: #2c3e50; font-weight: bold; }");
        QGridLayout* grid = new QGridLayout(group);
        grid->setVerticalSpacing(12); // 【新增】：拉开行间距，防止拥挤
        QPushButton* btnDel = new QPushButton("❌ 移除此目标");
        // 【修改后】
        btnDel->setStyleSheet("QPushButton { color: #e74c3c; border: none; font-weight: bold; background: transparent; } QPushButton:hover { color: #c0392b; text-decoration: underline; background: transparent; }");
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


// ==================== 独立多屏无边框弹出窗口类 ====================
class FramelessPopupWindow : public QWidget {
public:
    explicit FramelessPopupWindow(QWidget *contentWidget, const QString& titleText, QWidget *parent = nullptr) : QWidget(parent) {
        this->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint);
        this->setObjectName("framelessPopup");

        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(1, 1, 1, 1);
        mainLayout->setSpacing(0);

        m_titleBar = new QWidget(this);
        m_titleBar->setFixedHeight(36);
        m_titleBar->setStyleSheet("QWidget { background-color: #1a1a1a; border-bottom: 1px solid #333333; }");

        QHBoxLayout* titleLayout = new QHBoxLayout(m_titleBar);
        titleLayout->setContentsMargins(15, 0, 0, 0);
        titleLayout->setSpacing(0);

        QLabel* iconLabel = new QLabel("🪟", m_titleBar);
        iconLabel->setStyleSheet("border: none; background: transparent; font-size: 16px; min-height:36px; padding-right: 8px;");
        titleLayout->addWidget(iconLabel);

        QLabel* titleLabel = new QLabel(titleText, m_titleBar);
        titleLabel->setStyleSheet("color: #dcdde1; font-weight: bold; font-size: 14px; border: none; background: transparent; min-height:36px;");
        titleLayout->addWidget(titleLabel);
        titleLayout->addStretch();

        QString btnStyle = "QPushButton { border: none; background: transparent; color: #a0a0a0; font-size: 16px; min-height: 36px; min-width: 46px; border-radius: 0px; padding: 0; }"
                           "QPushButton:hover { background-color: #333333; color: #ffffff; }";
        QString closeBtnStyle = "QPushButton { border: none; background: transparent; color: #a0a0a0; font-size: 16px; min-height: 36px; min-width: 46px; border-radius: 0px; padding: 0; }"
                                "QPushButton:hover { background-color: #e74c3c; color: #ffffff; }";

        QPushButton* btnMin = new QPushButton("—", m_titleBar); btnMin->setStyleSheet(btnStyle);
        m_btnMax = new QPushButton("□", m_titleBar); m_btnMax->setStyleSheet(btnStyle);
        QPushButton* btnClose = new QPushButton("✕", m_titleBar); btnClose->setStyleSheet(closeBtnStyle);

        titleLayout->addWidget(btnMin);
        titleLayout->addWidget(m_btnMax);
        titleLayout->addWidget(btnClose);

        connect(btnMin, &QPushButton::clicked, this, [this]() {
            if (this->isMinimized()) this->setWindowState(this->windowState() & ~Qt::WindowMinimized);
            this->showMinimized();
        });
        connect(btnClose, &QPushButton::clicked, this, &QWidget::close);

        // 【核心修复1：防多屏乱跑】：强制使用纯净的 geometry() 替代 frameGeometry()
        connect(m_btnMax, &QPushButton::clicked, this, [this, mainLayout]() {
            if (this->property("isCustomMax").toBool()) {
                QRect normalRect = this->property("normalGeometry").toRect();
                this->setGeometry(normalRect);
                this->setProperty("isCustomMax", false);
                m_btnMax->setText("□");
                mainLayout->setContentsMargins(1, 1, 1, 1);
            } else {
                this->setProperty("normalGeometry", this->geometry());
                this->setProperty("isCustomMax", true);
                QScreen *screen = QApplication::screenAt(this->geometry().center());
                if (!screen) screen = QApplication::primaryScreen();
                this->setGeometry(screen->availableGeometry());
                m_btnMax->setText("❐");
                mainLayout->setContentsMargins(0, 0, 0, 0);
            }
        });

        mainLayout->addWidget(m_titleBar);

        QWidget* contentContainer = new QWidget(this);
        QVBoxLayout* contentLayout = new QVBoxLayout(contentContainer);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->addWidget(contentWidget);
        mainLayout->addWidget(contentContainer);

        QList<QWidget*> allWidgets = this->findChildren<QWidget*>();
        for (QWidget* w : allWidgets) {
            w->setMouseTracking(true);
            w->installEventFilter(this);
        }
        this->setMouseTracking(true);
        this->installEventFilter(this);
    }

    void showCentered(int w, int h) {
        this->resize(w, h);
        QScreen *screen = QApplication::screenAt(QCursor::pos());
        if (!screen) screen = QApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->availableGeometry();
            int x = screenGeom.center().x() - w / 2;
            int y = screenGeom.center().y() - h / 2;
            this->setGeometry(x, y, w, h);
            // 【核心修复】：保存准确的 geometry，防止多屏幕还原坐标跑偏
            this->setProperty("normalGeometry", this->geometry());
        }
        this->show();
    }

    void safeShowMaximized() {
        this->showCentered(1200, 800);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::MouseMove) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            QPoint localPos = this->mapFromGlobal(me->globalPos());

            if (me->buttons() == Qt::NoButton) {
                updateCursorShape(localPos);
            } else if (m_isResizing && (me->buttons() & Qt::LeftButton)) {
                doResize(me->globalPos());
                return true;
            } else if (m_isDragging && (me->buttons() & Qt::LeftButton)) {
                this->move(me->globalPos() - m_dragPos);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && !this->property("isCustomMax").toBool()) {
                QPoint localPos = this->mapFromGlobal(me->globalPos());

                // 【核心修复2：左边界漂移】：拦截逻辑必须优先判定拉伸！如果命中边缘，立刻return true吞噬事件，杜绝拖拽触发。
                m_resizeDir = getResizeDirection(localPos);
                if (m_resizeDir != 0) {
                    m_isResizing = true;
                    m_resizeStartPos = me->globalPos();
                    m_resizeStartGeometry = this->geometry();
                    return true;
                }

                // 只有明确不在边缘，且处于标题栏高度范围内时，才判定为移动拖拽
                if (localPos.y() < 36) {
                    QWidget* clickedWidget = this->childAt(localPos);
                    if (qobject_cast<QPushButton*>(clickedWidget)) {
                        return false;
                    }
                    m_isDragging = true;
                    // ========================================================
                    // 【核心修复】：使用 pos() 替代 geometry().topLeft()
                    // 彻底消除 Windows 系统隐形阴影边框造成的计算跳跃误差！
                    // ========================================================
                    m_dragPos = me->globalPos() - this->pos();
                    return true;
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_isDragging = false;
                m_isResizing = false;
                QPoint localPos = this->mapFromGlobal(me->globalPos());
                updateCursorShape(localPos);
            }
        }
        return QWidget::eventFilter(obj, event);
    }

private:
    int getResizeDirection(const QPoint &pos) {
        int padding = 6;
        int w = this->width();
        int h = this->height();
        int x = pos.x();
        int y = pos.y();

        if (x < 0 || x > w || y < 0 || y > h) return 0;

        bool left = x < padding;
        bool right = x > w - padding;
        bool top = y < padding;
        bool bottom = y > h - padding;

        if (left && top) return 1; if (right && top) return 2;
        if (left && bottom) return 3; if (right && bottom) return 4;
        if (left) return 5; if (right) return 6;
        if (top) return 7; if (bottom) return 8;
        return 0;
    }

    void updateCursorShape(const QPoint &pos) {
        if (this->property("isCustomMax").toBool()) { this->unsetCursor(); return; }
        switch (getResizeDirection(pos)) {
        case 1: case 4: this->setCursor(Qt::SizeFDiagCursor); break;
        case 2: case 3: this->setCursor(Qt::SizeBDiagCursor); break;
        case 5: case 6: this->setCursor(Qt::SizeHorCursor); break;
        case 7: case 8: this->setCursor(Qt::SizeVerCursor); break;
        default: this->unsetCursor(); break;
        }
    }

    void doResize(const QPoint &globalPos) {
        QPoint diff = globalPos - m_resizeStartPos;
        QRect rect = m_resizeStartGeometry;

        switch (m_resizeDir) {
        case 5: rect.setLeft(rect.left() + diff.x()); break;
        case 6: rect.setRight(rect.right() + diff.x()); break;
        case 7: rect.setTop(rect.top() + diff.y()); break;
        case 8: rect.setBottom(rect.bottom() + diff.y()); break;
        case 1: rect.setTopLeft(rect.topLeft() + diff); break;
        case 2: rect.setTopRight(rect.topRight() + diff); break;
        case 3: rect.setBottomLeft(rect.bottomLeft() + diff); break;
        case 4: rect.setBottomRight(rect.bottomRight() + diff); break;
        }

        if (rect.width() >= 600 && rect.height() >= 400) {
            this->setGeometry(rect);
        }
    }

    QWidget* m_titleBar;
    QPushButton* m_btnMax;
    bool m_isDragging = false;
    QPoint m_dragPos;
    bool m_isResizing = false;
    int m_resizeDir = 0;
    QPoint m_resizeStartPos;
    QRect m_resizeStartGeometry;
};




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

    // ==========================================
    // 【修改】：统一配置所有绘图区为暗黑雷达模式
    // ==========================================
    plot->setBackground(QBrush(QColor("#0a0a0a"))); // 图表外围纯黑
    plot->axisRect()->setBackground(QBrush(QColor("#141414"))); // 图表内部深灰黑

    QPen axisPen(QColor("#555555"), 1); // 坐标轴使用暗灰色
    QPen gridPen(QColor("#222222"), 1, Qt::DashLine); // 网格线使用极暗的虚线

    plot->xAxis->setBasePen(axisPen); plot->yAxis->setBasePen(axisPen);
    plot->xAxis->setTickPen(axisPen); plot->yAxis->setTickPen(axisPen);
    plot->xAxis->setSubTickPen(axisPen); plot->yAxis->setSubTickPen(axisPen);

    plot->xAxis->grid()->setPen(gridPen); plot->yAxis->grid()->setPen(gridPen);
    plot->xAxis->grid()->setZeroLinePen(axisPen); plot->yAxis->grid()->setZeroLinePen(axisPen);

    plot->xAxis->setTickLabelColor(QColor("#a0a0a0")); // 刻度数字使用浅灰
    plot->yAxis->setTickLabelColor(QColor("#a0a0a0"));

    plot->xAxis->setLabelColor(QColor("#2ecc71")); // 坐标轴名字使用亮绿色
    plot->yAxis->setLabelColor(QColor("#2ecc71"));
}

void MainWindow::updatePlotOriginalRange(QCustomPlot* plot) {
    if (!plot) return;

    // 【核心修复】：如果已经记录过初始范围，直接跳过。
    // 防止逐帧刷新时把用户手动放大后的坐标当成“原始坐标”给覆盖掉。
    if (plot->property("hasOrigRange").toBool()) {
        return;
    }
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

    // 【修改】：使用我们刚刚定义的无边框独立窗口类
    FramelessPopupWindow* popupWindow = new FramelessPopupWindow(plot, "图表独立查看 (关闭或最小化即可还原)");
    popupWindow->setProperty("isPopup", true);
    popupWindow->setMinimumSize(800, 600);

    // 完美继承主界面的暗黑样式和 1px 深色边框
    popupWindow->setStyleSheet(this->styleSheet() + " #framelessPopup { background-color: #121212; border: 1px solid #333333; }");

    m_popupPlots.insert(popupWindow, qMakePair(plot, info));
    popupWindow->installEventFilter(this);
    popupWindow->setAttribute(Qt::WA_DeleteOnClose);
    //    popupWindow->show();
    // 【核心修复 3】：抛弃原始 show，使用居中接口，固定宽800高600
    popupWindow->showCentered(800, 600);
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

    // 1. 拦截 Tab 独立窗口的关闭事件，将其还原
    if (widget && widget->property("isTabPopup").toBool()) {
        if (event->type() == QEvent::Close) {
            restoreTab(widget);
        }
        return QMainWindow::eventFilter(obj, event);
    }

    // 2. 原本的单个图表弹出的拦截逻辑
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

    // ==========================================
    // 【关键修复 3】：全局拦截鼠标移动事件，完美解决子控件遮挡边缘探测的问题
    // ==========================================
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->buttons() == Qt::NoButton) {
            // 【核心护盾】：只处理属于主界面自己的控件，绝不干扰独立弹窗！
            QWidget* w = qobject_cast<QWidget*>(obj);
            if (w && w->window() == this) {
                QPoint localPos = this->mapFromGlobal(mouseEvent->globalPos());
                updateCursorShape(localPos);
            }
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onPlotContextMenu(const QPoint &pos) {
    QCustomPlot* plot = qobject_cast<QCustomPlot*>(sender());
    if (!plot) return;

    QMenu menu(this);
    // ==========================================
    // 【修改】：右键悬浮菜单的高级暗黑适配
    // ==========================================
    menu.setStyleSheet(
                "QMenu { background-color: #1e1e1e; border: 1px solid #444444; color: #dcdde1; padding: 5px; border-radius: 4px; }"
                "QMenu::item { padding: 6px 25px 6px 20px; margin: 2px 4px; border-radius: 3px; }"
                "QMenu::item:selected { background-color: #2c3e50; color: #2ecc71; font-weight: bold; }"
                "QMenu::separator { height: 1px; background: #444444; margin: 5px 10px; }"
                );
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

    // 【体验优化】：针对包含固定物理边界和滚动时间轴的特殊图表，采用智能自适应复原
    if (plot == m_spatialPlot || plot == m_timeAzimuthPlot ||
            plot == m_cbfWaterfallPlot || plot == m_dcvWaterfallPlot) {

        plot->xAxis->setRange(0, 180); // X轴始终恢复到标准的 0~180 度全景

        if (plot == m_spatialPlot) {
            plot->yAxis->setRange(-40, 5); // 空间谱恢复固定动态范围
        } else {
            plot->yAxis->rescale();        // 历程图和瀑布图的物理时间轴自适应贴合到最新进度
        }
    }
    // 其余静态切片图和频谱图，恢复最初始记录的坐标范围
    else {
        if (plot->property("hasOrigRange").toBool()) {
            plot->xAxis->setRange(plot->property("origXMin").toDouble(), plot->property("origXMax").toDouble());
            plot->yAxis->setRange(plot->property("origYMin").toDouble(), plot->property("origYMax").toDouble());
        } else {
            plot->rescaleAxes();
        }
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

    // ====== 【清理常亮指示灯】 ======
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

    // ====== 【安全清理 Tab 1 的图表及外骨骼】 ======
    auto removeTab1Plot = [this](QMap<int, QCustomPlot*>& plotMap, int tid) {
        if (plotMap.contains(tid)) {
            QCustomPlot* p = plotMap.take(tid);
            for (auto it = m_popupPlots.begin(); it != m_popupPlots.end(); ++it) {
                if (it.value().first == p) { it.key()->close(); break; }
            }
            // 【安全修复】：无论图表去哪了，通过名字找出外骨骼并连根拔起
            QWidget* wrapper = this->findChild<QWidget*>(p->objectName() + "_wrapper");
            if (wrapper) {
                wrapper->hide(); wrapper->deleteLater();
            } else {
                p->hide(); p->deleteLater();
            }
        }
    };
    removeTab1Plot(m_lsPlots, targetId);
    removeTab1Plot(m_lofarPlots, targetId);
    removeTab1Plot(m_demonPlots, targetId);

    // ====== 【清理 Tab 2 的切片图表及其外骨骼包装器】 ======
    if (m_sliceWidget) {
        QList<QString> suffixes = {"cbf", "dcv"};
        for (const QString& suf : suffixes) {
            QString name = QString("slice_%1_%2").arg(suf).arg(targetId);
            if (QCustomPlot* p = m_sliceWidget->findChild<QCustomPlot*>(name)) {
                QWidget* wrapper = p->parentWidget();
                if (wrapper && wrapper->objectName() == p->objectName() + "_wrapper") {
                    m_sliceLayout->removeWidget(wrapper);
                    wrapper->hide();
                    wrapper->deleteLater();
                } else {
                    m_sliceLayout->removeWidget(p);
                    p->hide();
                    p->deleteLater();
                }
            }
        }
    }

    updateTab2Plots();

    // ====== 【清理 Tab 3 的离线瀑布图表及其外骨骼包装器】 ======
    if (m_lofarWaterfallWidget) {
        QList<QString> prefixes = {"offline_raw", "offline_tpsw", "offline_dp"};
        for (const QString& pref : prefixes) {
            QString name = QString("%1_%2").arg(pref).arg(targetId);
            if (QCustomPlot* p = m_lofarWaterfallWidget->findChild<QCustomPlot*>(name)) {
                QWidget* wrapper = p->parentWidget();
                if (wrapper && wrapper->objectName() == p->objectName() + "_wrapper") {
                    m_lofarWaterfallLayout->removeWidget(wrapper);
                    wrapper->hide();
                    wrapper->deleteLater();
                } else {
                    m_lofarWaterfallLayout->removeWidget(p);
                    p->hide();
                    p->deleteLater();
                }
            }
        }
    }

    // ====== 【清理表格与特征曲线】 ======
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


void MainWindow::onStartClicked() {
    // 【修改】：如果不是 UDP 模式，才需要检查文件是否为空
    if (!m_chkUdpMode->isChecked() && m_selectedFiles.isEmpty()) {
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
    m_chkUdpMode->setEnabled(false); // 运行时禁止切换模式
    m_btnPauseResume->setEnabled(true); m_btnStop->setEnabled(true);
    m_mainTabWidget->setCurrentIndex(0);

    m_lblSysInfo->setText(QString("状态: 运行中\n任务: %1\n开始时间: %2").arg(m_editTaskName->text()).arg(QDateTime::currentDateTime().toString("HH:mm:ss")));

    m_historyResults.clear();
    m_batchAccuracies.clear();
    m_targetClasses.clear();

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
    m_validator->setDepthResolveEnabled(m_currentConfig.enableDepthResolve);

    if (truths.empty()) {
        m_lblModeIndicator->setText("🔴 实战盲测模式 (无先验真值)");
        m_lblModeIndicator->setStyleSheet("background-color: #fdeaea; color: #e74c3c; font-size: 14px; font-weight: bold; border: 1px solid #e74c3c; border-radius: 5px; padding: 6px;");
        m_logConsole->appendPlainText("[系统提示] 未加载先验真值数据，系统进入【实战盲测模式】。Tab4正确率将显示为特征稳定度。\n");

        // 【新增】：盲测模式下直接隐藏这两个没有意义的真值图表
        m_plotTrueAzimuth->setVisible(false);
        m_plotBatchAccuracy->setVisible(false);
    } else {
        m_lblModeIndicator->setText(QString("🟢 算法仿真评估模式 (真值:%1个)").arg(truths.size()));
        m_lblModeIndicator->setStyleSheet("background-color: #eafaf1; color: #27ae60; font-size: 14px; font-weight: bold; border: 1px solid #27ae60; border-radius: 5px; padding: 6px;");
        m_logConsole->appendPlainText(QString("[系统提示] 已加载 %1 个先验真值目标，系统进入【算法仿真评估模式】。\n").arg(truths.size()));

        // 【新增】：仿真模式下恢复显示
        m_plotTrueAzimuth->setVisible(true);
        m_plotBatchAccuracy->setVisible(true);
    }

    // =======================================================
    // 【全新逻辑】：根据模式启动对应的数据引擎
    // =======================================================

    // 确保启动前线程是干净的
    if (m_worker->isRunning()) {
        m_worker->stop();
        m_worker->wait();
    }

    if (m_chkUdpMode->isChecked()) {
            // ----------------- UDP 实时模式 -----------------
            if (!m_dataBuffer) m_dataBuffer = new DataBuffer(100);
            m_dataBuffer->clear();

            if (m_udpReceiver) {
                m_udpReceiver->stop();
                m_udpReceiver->wait();
                m_udpReceiver->deleteLater();
                m_udpReceiver = nullptr;
            }

            m_udpReceiver = new UdpReceiver(m_udpBindAddress, m_udpListenPort, m_dataBuffer, "");

            // ========================================================
            // 【核心新增】：将 UDP 线程解析出的遥测信息实时刷到顶端仪表盘！
            // ========================================================
            connect(m_udpReceiver, &UdpReceiver::navInfoReceived, this, [this](QString lon, QString lat, QString hdg){
                if(m_lblLongitude) m_lblLongitude->setText(QString("🧭 经度: %1").arg(lon));
                if(m_lblLatitude) m_lblLatitude->setText(QString("📍 纬度: %1").arg(lat));
                if(m_lblHeading) m_lblHeading->setText(QString("🚢 艏向角: %1").arg(hdg));
            }, Qt::QueuedConnection);

            m_udpReceiver->start();

            m_worker->setWorkMode(WorkMode::MODE_UDP);
            m_worker->setDataBuffer(m_dataBuffer);

            appendLog(QString("\n>> [系统模式] 🚀 已切换为 UDP 实时侦听模式，监听地址: %1:%2\n").arg(m_udpBindAddress).arg(m_udpListenPort));
        } else {
        // ----------------- 本地文件回放模式 -----------------
        m_worker->setWorkMode(WorkMode::MODE_FILE);
        appendLog("\n>> [系统模式] 📂 已切换为离线文件回放模式\n");
    }

    // 真正启动核心算法线程
    m_worker->start();
    // 【新增】：如果是 UDP 模式，向远端下发开始投递数据的指令
    if (m_chkUdpMode->isChecked() && m_cmdSocket) {
        QByteArray cmd = "CMD:START";
        m_cmdSocket->writeDatagram(cmd, QHostAddress(m_udpRemoteAddress), m_udpRemotePort);
        appendLog(QString(">> 📡 已向阵列远端发送 [START] 开始投递指令！\n"));
    }
}

void MainWindow::onStopClicked() {
    m_worker->stop();
    m_btnStart->setEnabled(true); m_btnPauseResume->setEnabled(false); m_btnStop->setEnabled(false);
    m_btnSelectFiles->setEnabled(!m_chkUdpMode->isChecked());
    m_chkUdpMode->setEnabled(true);
    m_lblSysInfo->setText("状态: 已手动终止");
    appendLog("\n>> 操作已被用户手动终止。");

    // 【新增向远端发指令】
    if (m_chkUdpMode->isChecked() && m_cmdSocket) {
        m_cmdSocket->writeDatagram("CMD:STOP", QHostAddress(m_udpRemoteAddress), m_udpRemotePort);
        appendLog(">> 📡 已向阵列远端发送 [STOP] 停止投递指令！\n");
    }
}
void MainWindow::setupUi() {
    // 【核心修复 1】：隐藏原生边框，但必须保留系统的最小化跟踪权限，防止状态假死！
    this->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint);

    QWidget* centralWidget = new QWidget(this);

    // 【新增】：开启全局鼠标悬浮追踪，用于实时侦测边缘缩放
    this->setMouseTracking(true);
    centralWidget->setMouseTracking(true);

    // 因为没有了原生边框，我们需要给主窗口加一个 1px 的边框，否则边缘会和黑色桌面融为一体
    centralWidget->setObjectName("mainCentralWidget");
    this->setStyleSheet(
                "#mainCentralWidget { border: 1px solid #333333; background-color: #121212; }"
                // ... (把你之前的全局样式表直接贴在这里面) ...
                "QMainWindow, QWidget { background-color: #121212; color: #e0e0e0; font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif; font-size: 13px; }"
                "QGroupBox { border: 1px solid #333333; border-radius: 6px; margin-top: 22px; font-weight: bold; color: #2ecc71; padding-top: 10px; padding-bottom: 5px; }"
                "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 5px; left: 10px; top: 0px; }"
                "QPushButton { background-color: #1e1e1e; border: 1px solid #444; border-radius: 4px; padding: 6px 15px; color: #e0e0e0; font-weight: bold; min-height: 24px; }"
                "QPushButton:hover { background-color: #2c3e50; border-color: #3498db; }"
                "QPushButton:pressed { background-color: #2980b9; color: #ffffff; }"
                "QLineEdit, QSpinBox, QComboBox { background-color: #1a1a1a; border: 1px solid #444; padding: 4px 6px; color: #2ecc71; font-weight: bold; border-radius: 3px; min-height: 22px; }"
                "QCheckBox { color: #bdc3c7; font-weight: bold; min-height: 24px; }"
                "QSplitter::handle { background-color: #222222; }"
                "QScrollArea { border: none; background-color: transparent; }"
                "QLabel { color: #dcdde1; min-height: 26px; }"
                "QTableWidget { background-color: #1e1e1e; color: #dcdde1; gridline-color: #333333; border: none; alternate-background-color: #252526; }"
                "QHeaderView::section { background-color: #252526; color: #2ecc71; font-weight: bold; border: 1px solid #333; padding: 4px; }"
                "QTableWidget::item:selected { background-color: #2c3e50; color: #ffffff; }"
                );

    QVBoxLayout* mainVLayout = new QVBoxLayout(centralWidget);
    mainVLayout->setContentsMargins(0, 0, 0, 0); // 【核心修复 2】：取消主布局边距，让标题栏贴边
    mainVLayout->setSpacing(0);

    // ==========================================
    // 【新增】：构建暗黑风自定义标题栏
    // ==========================================
    m_customTitleBar = new QWidget(centralWidget);
    m_customTitleBar->setFixedHeight(36); // 标题栏高度
    m_customTitleBar->setStyleSheet("QWidget { background-color: #1a1a1a; border-bottom: 1px solid #333333; }");

    QHBoxLayout* titleLayout = new QHBoxLayout(m_customTitleBar);
    titleLayout->setContentsMargins(15, 0, 0, 0);
    titleLayout->setSpacing(0);

    QLabel* iconLabel = new QLabel("⚓", m_customTitleBar);
    iconLabel->setStyleSheet("border: none; background: transparent; font-size: 16px; min-height:36px; padding-right: 8px;");
    titleLayout->addWidget(iconLabel);


    m_titleLabel = new QLabel("SonarTracker Pro - 高级被动声纳显控终端", m_customTitleBar);
        m_titleLabel->setStyleSheet("color: #dcdde1; font-weight: bold; font-size: 14px; border: none; background: transparent; min-height:36px;");
        titleLayout->addWidget(m_titleLabel);

        // =========================================================
        // 【意见六】：在软件顶端添加本舰经纬度、艏向角信息 (固定值演示)
        // =========================================================
        titleLayout->addSpacing(50); // 与左侧的主标题拉开一段安全的距离

        // 定义仪表盘风格的深色边框、亮黄色等宽字体 (Consolas)
        QString shipInfoStyle = "QLabel { "
                                "color: #f1c40f; "               // 亮黄色字体
                                "font-weight: bold; "
                                "font-size: 13px; "
                                "font-family: Consolas; "        // 使用等宽字体，更像仪表盘数据
                                "background-color: #252526; "    // 略浅于标题栏的深灰底色
                                "border: 1px solid #f39c12; "    // 橙黄色边框
                                "border-radius: 4px; "
                                "padding: 2px 10px; "
                                "margin-top: 6px; "
                                "margin-bottom: 6px; "
                                "}";

        m_lblLongitude = new QLabel("🧭 经度: ---°--'--\" -", m_customTitleBar);
        m_lblLongitude->setStyleSheet(shipInfoStyle);
        titleLayout->addWidget(m_lblLongitude);

        titleLayout->addSpacing(10); // 标签之间的间距

     m_lblLatitude = new QLabel("🧭 纬度: --°--'--\" -", m_customTitleBar);
        m_lblLatitude->setStyleSheet(shipInfoStyle);
        titleLayout->addWidget(m_lblLatitude);

        titleLayout->addSpacing(10); // 标签之间的间距

        m_lblHeading = new QLabel("🚢 艏向角: ---.-°", m_customTitleBar);
        m_lblHeading->setStyleSheet(shipInfoStyle);
        titleLayout->addWidget(m_lblHeading);

        // =========================================================

        titleLayout->addStretch(); // 弹簧会把右侧的最小化、关闭按钮挤到最右边


    // 定义按钮样式 (普通按键暗灰，关闭按键悬浮变红)
    QString btnStyle = "QPushButton { border: none; background: transparent; color: #a0a0a0; font-size: 16px; min-height: 36px; min-width: 46px; border-radius: 0px; padding: 0; }"
                       "QPushButton:hover { background-color: #333333; color: #ffffff; }";
    QString closeBtnStyle = "QPushButton { border: none; background: transparent; color: #a0a0a0; font-size: 16px; min-height: 36px; min-width: 46px; border-radius: 0px; padding: 0; }"
                            "QPushButton:hover { background-color: #e74c3c; color: #ffffff; }";

    m_btnMinimize = new QPushButton("—", m_customTitleBar);
    m_btnMinimize->setStyleSheet(btnStyle);
    m_btnMaximize = new QPushButton("□", m_customTitleBar);
    m_btnMaximize->setStyleSheet(btnStyle);
    m_btnClose = new QPushButton("✕", m_customTitleBar);
    m_btnClose->setStyleSheet(closeBtnStyle);

    titleLayout->addWidget(m_btnMinimize);
    titleLayout->addWidget(m_btnMaximize);
    titleLayout->addWidget(m_btnClose);

    // 绑定系统控制信号
    // 绑定系统控制信号
    connect(m_btnMinimize, &QPushButton::clicked, this, [this]() {
        // 【核心修复】：如果 Qt 内部卡在了最小化状态，强行将其剥离，绕过不同步 Bug
        if (this->isMinimized()) {
            this->setWindowState(this->windowState() & ~Qt::WindowMinimized);
        }
        this->showMinimized();
    });
    // 【关键修复 1】：补回你之前不小心删掉的关闭按钮信号绑定！
    connect(m_btnClose, &QPushButton::clicked, this, &QMainWindow::close);
    // ==========================================
    // 【终极修复】：完美自定义多屏全屏与还原机制
    // ==========================================
    connect(m_btnMaximize, &QPushButton::clicked, this, [this]() {
        if (this->property("isCustomMax").toBool()) {
            // 1. 还原操作：使用 move 和 resize 强行重置尺寸，绕过系统阻塞
            QRect normalRect = this->property("normalGeometry").toRect();
            this->move(normalRect.topLeft());
            this->resize(normalRect.size());

            this->setProperty("isCustomMax", false);
            m_btnMaximize->setText("□");
        } else {
            // 2. 最大化操作：先保存当前尺寸位置
            this->setProperty("normalGeometry", this->geometry());
            this->setProperty("isCustomMax", true);

            // 获取当前所在的显示器（完美支持多块屏幕）
            QScreen *screen = QApplication::screenAt(this->geometry().center());
            if (!screen) {
                screen = QApplication::primaryScreen();
            }

            // availableGeometry 会自动避开 Windows 底部的任务栏高度
            this->setGeometry(screen->availableGeometry());
            m_btnMaximize->setText("❐");
        }
    });
    // 将自定义标题栏塞入主布局的最顶层
    mainVLayout->addWidget(m_customTitleBar);

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
    leftPanel->setMinimumWidth(380);
    leftPanel->setMaximumWidth(600);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0,0,0,0);

    QGroupBox* groupButtons = new QGroupBox("系统控制指令区", leftPanel);
    QVBoxLayout* btnLayout = new QVBoxLayout(groupButtons);

    // 【新增】：UDP 实时侦听模式开关
    m_chkUdpMode = new QCheckBox("📡 开启 UDP 实时接收", this);
    m_chkUdpMode->setStyleSheet("QCheckBox { color: #d63031; font-weight: bold; } QCheckBox:hover { color: #ff7675; }");
    // 在 setupUi 某处初始化发送套接字
    m_cmdSocket = new QUdpSocket(this);
    // 【新增】网络配置按钮
    m_btnUdpConfig = new QPushButton("⚙️ 网络配置", this);
    m_btnUdpConfig->setToolTip("修改监听IP和端口");
    m_btnUdpConfig->setStyleSheet("QPushButton { font-weight: bold; color: #2980b9; padding: 2px 8px; }");
    connect(m_btnUdpConfig, &QPushButton::clicked, this, &MainWindow::onUdpConfigClicked);

    QHBoxLayout* udpLayout = new QHBoxLayout();
    udpLayout->addWidget(m_chkUdpMode);
    udpLayout->addStretch();
    udpLayout->addWidget(m_btnUdpConfig);

    // 把原来的 btnLayout->addWidget(m_chkUdpMode); 替换成：
    btnLayout->addLayout(udpLayout);

    // 1. 常规配置按钮：仅使用 Emoji，保持默认的暗黑文字颜色
    m_btnSelectFiles = new QPushButton("📂 数据文件输入...", this);
    m_btnManualTruth = new QPushButton("📝 目标先验真值配置窗口...", this);

    // 2. 核心控制按钮：Emoji + 专属主题色（常规状态下文字带颜色，鼠标悬浮时背景变色）
    m_btnStart       = new QPushButton("▶️ 开始算法处理", this);
    m_btnStart->setStyleSheet("QPushButton { color: #2ecc71; font-weight: bold; } "
                              "QPushButton:hover { background-color: #27ae60; color: white; }");

    m_btnPauseResume = new QPushButton("⏸️ 暂停/继续", this);
    m_btnPauseResume->setStyleSheet("QPushButton { color: #f39c12; font-weight: bold; } "
                                    "QPushButton:hover { background-color: #d68910; color: white; }");

    m_btnStop        = new QPushButton("⏹️ 终止算法", this);
    m_btnStop->setStyleSheet("QPushButton { color: #e74c3c; font-weight: bold; } "
                             "QPushButton:hover { background-color: #c0392b; color: white; }");

    m_btnExport      = new QPushButton("📸 一键导出日志图片", this);
    m_btnExport->setStyleSheet("QPushButton { color: #3498db; font-weight: bold; } "
                               "QPushButton:hover { background-color: #2980b9; color: white; }");


    // ==========================================
    // 【漏掉的代码补回】：图表自适应布局控制下拉框
    // ==========================================
    QComboBox* m_cmbLayoutMode = new QComboBox(this);
    m_cmbLayoutMode->setObjectName("cmbLayoutMode");
    m_cmbLayoutMode->addItem("🔳 布局模式: 固定尺寸 (允许滚动)");
    m_cmbLayoutMode->addItem("🗜️ 布局模式: 全局挤压 (单屏全显)");
    m_cmbLayoutMode->setStyleSheet("QComboBox { background-color: #1a1a1a; color: #f1c40f; border: 1px solid #f39c12; font-weight: bold; border-radius: 4px; padding: 4px; }");
    btnLayout->addWidget(m_cmbLayoutMode);

    // =========================================================
            // 【终极修复】：绑定下拉框切换事件 (彻底消灭所有 Tab 的滚动条)
            // =========================================================
            connect(m_cmbLayoutMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index){
                bool isAutoFit = (index == 1);
                Qt::Alignment align = isAutoFit ? Qt::Alignment() : Qt::AlignTop;
                if (m_targetLayout) m_targetLayout->setAlignment(align);
                if (m_sliceLayout) m_sliceLayout->setAlignment(align);
                if (m_lofarWaterfallLayout) m_lofarWaterfallLayout->setAlignment(align);

                auto updateHeight = [isAutoFit](QCustomPlot* p, int h) {
                    if (!p) return;
                    p->setMinimumSize(0, isAutoFit ? 0 : h);
                    if (QWidget* wrapper = p->parentWidget()) {
                        if (wrapper->objectName().endsWith("_wrapper")) {
                            wrapper->setMinimumSize(0, isAutoFit ? 0 : h + 30);
                        }
                    }
                    p->updateGeometry(); p->replot();
                };

                // 【核心修复】：把 Tab 1 顶部的两大实时图表也纳入全局挤压管理！
                updateHeight(m_timeAzimuthPlot, 200);
                updateHeight(m_spatialPlot, 250);

                // Tab 1 的目标队列图表
                for (QCustomPlot* p : m_lsPlots.values()) updateHeight(p, 200);
                for (QCustomPlot* p : m_lofarPlots.values()) updateHeight(p, 200);
                for (QCustomPlot* p : m_demonPlots.values()) updateHeight(p, 200);

                // 将 Tab 2 顶部的两大瀑布图也纳入自适应挤压管理！
                updateHeight(m_cbfWaterfallPlot, 550);
                updateHeight(m_dcvWaterfallPlot, 550);
                // 【核心修复】：在全显模式下，强制 Splitter 给上方热力图分配绝大部分空间
                if (QSplitter* split2 = m_cbfWaterfallPlot->nativeParentWidget()->findChild<QSplitter*>()) {
                    split2->setStretchFactor(0, isAutoFit ? 10 : 1); // 0是上方热力图列，权重拉满
                }

                // Tab 2 切片
                if (m_sliceWidget) {
                    for (QCustomPlot* p : m_sliceWidget->findChildren<QCustomPlot*>()) {
                        if (p->objectName().startsWith("slice_")) updateHeight(p, 250);
                    }
                }

                // Tab 3
                if (m_lofarWaterfallWidget) {
                    for (QCustomPlot* p : m_lofarWaterfallWidget->findChildren<QCustomPlot*>()) {
                        if (p->objectName().startsWith("offline_")) updateHeight(p, 250);
                    }
                }

                // 通过对象名精确锁定并关闭三大 Tab 的滚动轴
                if (QScrollArea* sa1 = this->findChild<QScrollArea*>("scrollTab1"))
                    sa1->setVerticalScrollBarPolicy(isAutoFit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
                if (QScrollArea* sa2 = this->findChild<QScrollArea*>("scrollTab2"))
                    sa2->setVerticalScrollBarPolicy(isAutoFit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
                if (QScrollArea* sa3 = this->findChild<QScrollArea*>("scrollTab3"))
                    sa3->setVerticalScrollBarPolicy(isAutoFit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);

                // 强迫父容器重新计算身形
                if (m_targetLayout) { m_targetLayout->invalidate(); if (m_targetPanelWidget) { m_targetPanelWidget->updateGeometry(); m_targetPanelWidget->adjustSize(); } }
                if (m_sliceLayout) { m_sliceLayout->invalidate(); if (m_sliceWidget) { m_sliceWidget->updateGeometry(); m_sliceWidget->adjustSize(); } }
                if (m_lofarWaterfallLayout) { m_lofarWaterfallLayout->invalidate(); if (m_lofarWaterfallWidget) { m_lofarWaterfallWidget->updateGeometry(); m_lofarWaterfallWidget->adjustSize(); } }

                QCoreApplication::processEvents();
            });



    m_chkDepthResolve = new QCheckBox("🌊 启用 MFP 水上/水下目标分辨", this);
    m_chkDepthResolve->setStyleSheet("QCheckBox { color: #0984e3; font-weight: bold; } QCheckBox:hover { color: #74b9ff; }");
    connect(m_chkDepthResolve, &QCheckBox::toggled, this, &MainWindow::onDepthResolveToggled);
    m_chkDepthResolve->setChecked(false);

    // 2. 找到原来的 m_chkUdpMode toggled (大概在 392 行)，把文本换成动态变量：
    connect(m_chkUdpMode, &QCheckBox::toggled, this, [this](bool checked){
        m_btnSelectFiles->setEnabled(!checked);
        m_btnStart->setEnabled(checked || !m_selectedFiles.isEmpty());

        if (checked) {
            m_lblSysInfo->setText(QString("状态: 就绪\n模式: UDP网络直连\n监听: %1:%2").arg(m_udpBindAddress).arg(m_udpListenPort));
        } else {
            m_lblSysInfo->setText("状态: 就绪\n模式: 离线文件回放\n待导入数据...");
        }
    });

    m_editDeleteTargetId = new QLineEdit(this);
    m_editDeleteTargetId->setPlaceholderText("要剔除的目标ID...");

    // 【优化】：弃用垃圾桶黑白图标，强化按钮警示感
    m_btnDeleteTarget = new QPushButton("🗑️ 剔除虚假目标", this);
    m_btnDeleteTarget->setStyleSheet("QPushButton { color: #ff7675; font-weight: bold; border: 1px solid #d63031; } "
                                     "QPushButton:hover { background-color: #d63031; color: white; }");
    connect(m_btnDeleteTarget, &QPushButton::clicked, this, &MainWindow::onDeleteTargetClicked);

    QHBoxLayout* delLayout = new QHBoxLayout();
    delLayout->addWidget(m_editDeleteTargetId);
    delLayout->addWidget(m_btnDeleteTarget);

    m_btnStart->setEnabled(false); m_btnPauseResume->setEnabled(false); m_btnStop->setEnabled(false);

    m_editTaskName = new QLineEdit(this);
    m_editTaskName->setPlaceholderText("导入数据后自动识别，可自定义...");
    btnLayout->addWidget(new QLabel("当前任务名称:", leftPanel));
    btnLayout->addWidget(m_editTaskName);

    // 把 UDP 开关加到按钮区最上方
    //    btnLayout->addWidget(m_chkUdpMode);

    btnLayout->addWidget(m_btnSelectFiles); btnLayout->addWidget(m_btnManualTruth);
    btnLayout->addWidget(m_btnStart); btnLayout->addWidget(m_btnPauseResume);
    btnLayout->addWidget(m_btnStop); btnLayout->addWidget(m_btnExport);
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
    // 中间：主图表展示区 (可撕裂 Tab 架构)
    // ==========================================
    m_mainTabWidget = new QTabWidget(topMainSplitter);
    // 【核心修复 1】：强制将展示区的最小宽度限制设为 100。
    // 这样它就再也不会阻挡主窗口缩小了！当空间不够时，它会自动召唤右上角的 < > 滚动箭头。
    m_mainTabWidget->setMinimumWidth(100);
    m_mainTabWidget->setElideMode(Qt::ElideNone);
    m_mainTabWidget->setUsesScrollButtons(true);

    // 【终极修复】：增加了 min-width: 240px，强制撑开宽度，绝不压缩文字！
    // 增加了 text-align: center，让字数较少的标题也能完美居中对齐。
    m_mainTabWidget->setStyleSheet(
                "QTabBar::tab { min-width: 240px; padding: 10px 0px; text-align: center; font-weight: bold; font-size: 14px; background: #1e1e1e; border: 1px solid #333; border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 2px; color: #7f8c8d; }"
                "QTabBar::tab:selected { background: #252526; color: #2ecc71; border-bottom: 2px solid #2ecc71; }"
                "QTabWidget::pane { border: 1px solid #333; top: -1px; background-color: #121212; }"
                );
    topMainSplitter->addWidget(m_mainTabWidget);

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

    // 1. 模式指示器 (放在最上面)
    m_lblModeIndicator = new QLabel("⚪ 当前模式: 待就绪", this);
    m_lblModeIndicator->setAlignment(Qt::AlignCenter);
    m_lblModeIndicator->setStyleSheet("background-color: #ecf0f1; color: #7f8c8d; font-size: 14px; font-weight: bold; border: 1px solid #bdc3c7; border-radius: 5px; padding: 6px;");
    logLayout->addWidget(m_lblModeIndicator);

    // 2. 指示灯展示区 (放在模式指示器下方，装在一个固定高度且带背景的 QScrollArea 里)
    QScrollArea* lightsScrollArea = new QScrollArea(this);
    lightsScrollArea->setFixedHeight(45); // 固定高度，刚好容纳一排指示灯
    lightsScrollArea->setWidgetResizable(true);
    lightsScrollArea->setFrameShape(QFrame::Box); // 去掉难看的深色边框
    lightsScrollArea->setStyleSheet("QScrollArea { background-color: transparent; }");

    // 真正的指示灯容器 Widget
    m_targetLightsWidget = new QWidget(lightsScrollArea);
    m_targetLightsWidget->setStyleSheet("background-color: #1e1e1e; border: 1px solid #333333; border-radius: 4px;");

    // 指示灯水平排列
    m_targetLightsLayout = new QHBoxLayout(m_targetLightsWidget);
    m_targetLightsLayout->setContentsMargins(5, 0, 5, 0); // 留点内边距
    m_targetLightsLayout->setSpacing(5);
    m_targetLightsLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    lightsScrollArea->setWidget(m_targetLightsWidget);
    logLayout->addWidget(lightsScrollArea);

    m_lblSysInfo = new QLabel("引擎初始化完成，参数已就绪。\n等待注入探测数据...");
    m_lblSysInfo->setStyleSheet("color: #dcdde1; font-weight: bold; font-size: 13px;");
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
    topMainSplitter->setSizes(QList<int>() << 380 << 940 << 380);

    // ================== TAB 1 ==================
        QWidget* tab1 = new QWidget();
        QHBoxLayout* tab1Layout = new QHBoxLayout(tab1);
        QSplitter* horizontalSplitter = new QSplitter(Qt::Horizontal, tab1);

        QWidget* midPanel = new QWidget(horizontalSplitter);
        QVBoxLayout* midLayout = new QVBoxLayout(midPanel);
        m_timeAzimuthPlot = new QCustomPlot(midPanel);
        m_timeAzimuthPlot->setObjectName("timeAzimuthPlot");
        m_timeAzimuthPlot->setMinimumSize(0, 200); // 宽度限制改为 0
        setupPlotInteraction(m_timeAzimuthPlot);
        m_timeAzimuthPlot->addGraph();
        m_timeAzimuthPlot->graph(0)->setLineStyle(QCPGraph::lsNone);
        m_timeAzimuthPlot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, Qt::red, Qt::black, 7));
        m_timeAzimuthPlot->plotLayout()->insertRow(0);
        m_timeAzimuthPlot->plotLayout()->addElement(0, 0, new QCPTextElement(m_timeAzimuthPlot, "宽带实时方位检测提取结果", QFont("sans", 12, QFont::Bold)));
        m_timeAzimuthPlot->xAxis->setLabel("方位角/°"); m_timeAzimuthPlot->yAxis->setLabel("物理时间/s");
        m_timeAzimuthPlot->xAxis->setRange(0, 180);
        m_timeAzimuthPlot->yAxis->setRangeReversed(true);
        midLayout->addWidget(wrapPlotWithRangeControl(m_timeAzimuthPlot, "聚焦方位角(°):", 0, 360, 0, 180));

        QWidget* rightPanel = new QWidget(horizontalSplitter);
        QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);

        m_spatialPlot = new QCustomPlot(rightPanel);
        m_spatialPlot->setObjectName("spatialPlot");
        m_spatialPlot->setMinimumSize(0, 250); // 宽度限制改为 0
        m_spatialPlot->setMaximumHeight(350);
        setupPlotInteraction(m_spatialPlot);
        m_spatialPlot->addGraph(); m_spatialPlot->graph(0)->setName("CBF (常规波束)"); m_spatialPlot->graph(0)->setPen(QPen(Qt::gray, 2, Qt::DashLine));
        m_spatialPlot->addGraph(); m_spatialPlot->graph(1)->setName("DCV (高分辨)"); m_spatialPlot->graph(1)->setPen(QPen(Qt::blue, 2));
        m_spatialPlot->plotLayout()->insertRow(0);
        m_plotTitle = new QCPTextElement(m_spatialPlot, "宽带空间谱实时折线图", QFont("sans", 12, QFont::Bold));
        m_spatialPlot->plotLayout()->addElement(0, 0, m_plotTitle);
        m_spatialPlot->xAxis->setLabel("方位角/°"); m_spatialPlot->yAxis->setLabel("归一化功率/dB");
        m_spatialPlot->xAxis->setRange(0, 180); m_spatialPlot->yAxis->setRange(-40, 5); m_spatialPlot->legend->setVisible(true);
        m_spatialPlot->legend->setBrush(QColor(20, 20, 20, 200));
        m_spatialPlot->legend->setTextColor(QColor("#dcdde1"));
        rightLayout->addWidget(wrapPlotWithRangeControl(m_spatialPlot, "聚焦方位角(°):", 0, 360, 0, 180));

        // Tab 1 目标特征多列标签头
        QWidget* tab1Header = new QWidget(rightPanel);
        QHBoxLayout* tab1HeaderLayout = new QHBoxLayout(tab1Header);
        tab1HeaderLayout->setContentsMargins(0, 10, 15, 5);

        QLabel* lblLs = new QLabel("🎯 瞬时滤波线谱 (LS)", tab1Header);
        lblLs->setAlignment(Qt::AlignCenter);
        lblLs->setStyleSheet("QLabel { background-color: rgba(231, 76, 60, 0.15); color: #ff7675; font-weight: bold; font-size: 13px; padding: 6px; border-radius: 4px; border: 1px solid #d63031; }");

        QLabel* lblLofar = new QLabel("🌊 LOFAR 谱历程", tab1Header);
        lblLofar->setAlignment(Qt::AlignCenter);
        lblLofar->setStyleSheet("QLabel { background-color: rgba(52, 152, 219, 0.15); color: #74b9ff; font-weight: bold; font-size: 13px; padding: 6px; border-radius: 4px; border: 1px solid #2980b9; }");

        QLabel* lblDemon = new QLabel("🔊 DEMON 轴频包络", tab1Header);
        lblDemon->setAlignment(Qt::AlignCenter);
        lblDemon->setStyleSheet("QLabel { background-color: rgba(46, 204, 113, 0.15); color: #55efc4; font-weight: bold; font-size: 13px; padding: 6px; border-radius: 4px; border: 1px solid #27ae60; }");

        tab1HeaderLayout->addWidget(lblLs);
        tab1HeaderLayout->addWidget(lblLofar);
        tab1HeaderLayout->addWidget(lblDemon);
        tab1HeaderLayout->setStretch(0, 1); tab1HeaderLayout->setStretch(1, 1); tab1HeaderLayout->setStretch(2, 1);
        rightLayout->addWidget(tab1Header);

        QScrollArea* scrollArea = new QScrollArea(rightPanel);
        scrollArea->setObjectName("scrollTab1"); // 【新增户口】
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
        tab1->setProperty("absoluteIndex", 0);
        m_mainTabWidget->addTab(tab1, "实时处理：目标探测与关联");

        // ================== TAB 2 ==================
        QWidget* tab2 = new QWidget();
        QVBoxLayout* tab2MainLayout = new QVBoxLayout(tab2);
        QScrollArea* tab2Scroll = new QScrollArea(tab2);
        tab2Scroll->setObjectName("scrollTab2"); // 【新增户口】
        tab2Scroll->setWidgetResizable(true);
        tab2Scroll->setFrameShape(QFrame::NoFrame);
        QWidget* tab2Container = new QWidget(tab2Scroll);
        QVBoxLayout* tab2ContainerLayout = new QVBoxLayout(tab2Container);

        QSplitter* waterfallsSplitter = new QSplitter(Qt::Horizontal, tab2Container);

        // 构建左侧 CBF
        QWidget* cbfColWidget = new QWidget(waterfallsSplitter);
        QVBoxLayout* cbfColLayout = new QVBoxLayout(cbfColWidget);
        cbfColLayout->setContentsMargins(0, 0, 0, 0);

        QComboBox* cmbCbfColor = new QComboBox(cbfColWidget);
        cmbCbfColor->setObjectName("cmbColorTab2_CBF");
        cmbCbfColor->addItems({"🌈 经典热力 (Jet)", "🔥 高温火焰 (Hot)", "🌌 医疗伪影 (Thermal)", "⚫ 灰度声图 (Grayscale)", "❄️ 极寒冷色 (Polar)"});
        cmbCbfColor->setStyleSheet("QComboBox { background-color: #1a1a1a; color: #3498db; border: 1px solid #2980b9; font-weight: bold; border-radius: 4px; padding: 4px; }");
        cbfColLayout->addWidget(cmbCbfColor);

        m_cbfWaterfallPlot = new QCustomPlot(cbfColWidget);
        m_cbfWaterfallPlot->setObjectName("cbfWaterfallPlot");
        m_cbfWaterfallPlot->setMinimumSize(0, 550); // 【关键修改】：打破 300 宽度的物理限制
        setupPlotInteraction(m_cbfWaterfallPlot);
        m_cbfWaterfallPlot->plotLayout()->insertRow(0); m_cbfWaterfallPlot->plotLayout()->addElement(0, 0, new QCPTextElement(m_cbfWaterfallPlot, "常规波束形成(CBF) 空间谱历程", QFont("sans", 12, QFont::Bold)));
        cbfColLayout->addWidget(wrapPlotWithRangeControl(m_cbfWaterfallPlot, "聚焦方位角(°):", 0, 360, 0, 180));

        connect(cmbCbfColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index){
            QCPColorGradient grad = QCPColorGradient::gpJet;
            if(index==1) grad = QCPColorGradient::gpHot; else if(index==2) grad = QCPColorGradient::gpThermal; else if(index==3) grad = QCPColorGradient::gpGrayscale; else if(index==4) grad = QCPColorGradient::gpPolar;
            if (m_cbfWaterfallPlot && m_cbfWaterfallPlot->plottableCount() > 0) {
                qobject_cast<QCPColorMap*>(m_cbfWaterfallPlot->plottable(0))->setGradient(grad);
                m_cbfWaterfallPlot->replot();
            }
        });

        // 构建右侧 DCV
        QWidget* dcvColWidget = new QWidget(waterfallsSplitter);
        QVBoxLayout* dcvColLayout = new QVBoxLayout(dcvColWidget);
        dcvColLayout->setContentsMargins(0, 0, 0, 0);

        QComboBox* cmbDcvColor = new QComboBox(dcvColWidget);
        cmbDcvColor->setObjectName("cmbColorTab2_DCV");
        cmbDcvColor->addItems({"🌈 经典热力 (Jet)", "🔥 高温火焰 (Hot)", "🌌 医疗伪影 (Thermal)", "⚫ 灰度声图 (Grayscale)", "❄️ 极寒冷色 (Polar)"});
        cmbDcvColor->setStyleSheet("QComboBox { background-color: #1a1a1a; color: #e74c3c; border: 1px solid #c0392b; font-weight: bold; border-radius: 4px; padding: 4px; }");
        dcvColLayout->addWidget(cmbDcvColor);

        m_dcvWaterfallPlot = new QCustomPlot(dcvColWidget);
        m_dcvWaterfallPlot->setObjectName("dcvWaterfallPlot");
        m_dcvWaterfallPlot->setMinimumSize(0, 550); // 【关键修改】：打破 300 宽度的物理限制
        setupPlotInteraction(m_dcvWaterfallPlot);
        m_dcvWaterfallPlot->plotLayout()->insertRow(0); m_dcvWaterfallPlot->plotLayout()->addElement(0, 0, new QCPTextElement(m_dcvWaterfallPlot, "高分辨反卷积(DCV) 全方位时空谱历程", QFont("sans", 12, QFont::Bold)));
        dcvColLayout->addWidget(wrapPlotWithRangeControl(m_dcvWaterfallPlot, "聚焦方位角(°):", 0, 360, 0, 180));

        connect(cmbDcvColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index){
            QCPColorGradient grad = QCPColorGradient::gpJet;
            if(index==1) grad = QCPColorGradient::gpHot; else if(index==2) grad = QCPColorGradient::gpThermal; else if(index==3) grad = QCPColorGradient::gpGrayscale; else if(index==4) grad = QCPColorGradient::gpPolar;
            if (m_dcvWaterfallPlot && m_dcvWaterfallPlot->plottableCount() > 0) {
                qobject_cast<QCPColorMap*>(m_dcvWaterfallPlot->plottable(0))->setGradient(grad);
                m_dcvWaterfallPlot->replot();
            }
        });

        waterfallsSplitter->addWidget(cbfColWidget);
        waterfallsSplitter->addWidget(dcvColWidget);
        waterfallsSplitter->setStretchFactor(0, 1); waterfallsSplitter->setStretchFactor(1, 1);
        waterfallsSplitter->setSizes(QList<int>() << 1000 << 1000);
        tab2ContainerLayout->addWidget(waterfallsSplitter);

        // Tab 2 底部切片图表的多列标签头
        QWidget* tab2SliceHeader = new QWidget(tab2Container);
        QHBoxLayout* tab2SliceHeaderLayout = new QHBoxLayout(tab2SliceHeader);
        tab2SliceHeaderLayout->setContentsMargins(0, 15, 0, 5);

        QLabel* lblCbfSlice = new QLabel("✂️ 常规波束 (CBF) 方位切片跟踪", tab2SliceHeader);
        lblCbfSlice->setAlignment(Qt::AlignCenter);
        // 修改 Tab 2 CBF 切片标签样式：由灰色改为醒目的青色系
        lblCbfSlice->setStyleSheet("QLabel { "
                                   "background-color: rgba(0, 206, 201, 0.15); " // 使用青色 (0, 206, 201) 的 15% 透明背景
                                   "color: #00cec9; "                            // 字体颜色保持亮青色
                                   "font-weight: bold; "
                                   "font-size: 13px; "
                                   "padding: 6px; "
                                   "border-radius: 4px; "
                                   "border: 1px solid #00cec9; "                 // 边框改为亮青色，取代原有的灰色 (#7f8c8d)
                                   "}");

        QLabel* lblDcvSlice = new QLabel("✂️ 高分辨 (DCV) 方位切片跟踪", tab2SliceHeader);
        lblDcvSlice->setAlignment(Qt::AlignCenter);
        lblDcvSlice->setStyleSheet("QLabel { background-color: rgba(231, 76, 60, 0.15); color: #ff7675; font-weight: bold; font-size: 13px; padding: 6px; border-radius: 4px; border: 1px solid #d63031; }");

        tab2SliceHeaderLayout->addWidget(lblCbfSlice);
        tab2SliceHeaderLayout->addWidget(lblDcvSlice);
        tab2SliceHeaderLayout->setStretch(0, 1); tab2SliceHeaderLayout->setStretch(1, 1);
        tab2ContainerLayout->addWidget(tab2SliceHeader);

        m_sliceWidget = new QWidget(tab2Container);
        m_sliceLayout = new QGridLayout(m_sliceWidget);
        m_sliceLayout->setAlignment(Qt::AlignTop);
        tab2ContainerLayout->addWidget(m_sliceWidget);

        // 【核心修复 3】：分配挤压比例，确保瀑布图和切片图完美瓜分屏幕，不会互挤到消失
        tab2ContainerLayout->setStretchFactor(waterfallsSplitter, 10);
        tab2ContainerLayout->setStretchFactor(m_sliceWidget, 1);

        tab2Scroll->setWidget(tab2Container);
        tab2MainLayout->addWidget(tab2Scroll);
        tab2->setProperty("absoluteIndex", 1);
        m_mainTabWidget->addTab(tab2, "实时处理：CBF/DCV全景与切片");

        // ================== TAB 3 ==================
        QWidget* tab3 = new QWidget();
        QVBoxLayout* tab3Layout = new QVBoxLayout(tab3);

        // Tab 3 离线瀑布图的多列标签头 (固定在顶部)
        QWidget* tab3HeaderBar = new QWidget(tab3);
        QHBoxLayout* tab3HeaderLayout = new QHBoxLayout(tab3HeaderBar);
        tab3HeaderLayout->setContentsMargins(0, 10, 15, 0);

        QLabel* lblRaw = new QLabel("📊 原始 LOFAR 谱 (RAW)", tab3HeaderBar);
        lblRaw->setAlignment(Qt::AlignCenter);
        lblRaw->setStyleSheet("QLabel { background-color: rgba(155, 89, 182, 0.15); color: #a29bfe; font-weight: bold; font-size: 13px; padding: 6px; border-radius: 4px; border: 1px solid #8e44ad; }");

        QLabel* lblTpsw = new QLabel("🎛️ TPSW 背景均衡", tab3HeaderBar);
        lblTpsw->setAlignment(Qt::AlignCenter);
        lblTpsw->setStyleSheet("QLabel { background-color: rgba(52, 152, 219, 0.15); color: #74b9ff; font-weight: bold; font-size: 13px; padding: 6px; border-radius: 4px; border: 1px solid #2980b9; }");

        QLabel* lblDp = new QLabel("📈 DP 线谱寻优", tab3HeaderBar);
        lblDp->setAlignment(Qt::AlignCenter);
        lblDp->setStyleSheet("QLabel { background-color: rgba(230, 126, 34, 0.15); color: #fab1a0; font-weight: bold; font-size: 13px; padding: 6px; border-radius: 4px; border: 1px solid #d35400; }");

        tab3HeaderLayout->addWidget(lblRaw);
        tab3HeaderLayout->addWidget(lblTpsw);
        tab3HeaderLayout->addWidget(lblDp);
        tab3HeaderLayout->setStretch(0, 1); tab3HeaderLayout->setStretch(1, 1); tab3HeaderLayout->setStretch(2, 1);
        tab3Layout->addWidget(tab3HeaderBar);

        // Tab 3 控制横幅 (包含三列的下拉框)
        QWidget* tab3ControlBar = new QWidget(tab3);
        QHBoxLayout* tab3ControlLayout = new QHBoxLayout(tab3ControlBar);
        tab3ControlLayout->setContentsMargins(0, 5, 15, 5);

        QString cmbStyle = "QComboBox { background-color: #1a1a1a; color: #f1c40f; border: 1px solid #f39c12; font-weight: bold; border-radius: 4px; padding: 4px; }";

        QComboBox* cmbRaw = new QComboBox(tab3ControlBar); cmbRaw->setObjectName("cmbColorTab3_RAW");
        cmbRaw->addItems({"🌈 RAW 风格: Jet", "🔥 RAW 风格: Hot", "🌌 RAW 风格: Thermal", "⚫ RAW 风格: Grayscale", "❄️ RAW 风格: Polar"}); cmbRaw->setStyleSheet(cmbStyle);

        QComboBox* cmbTpsw = new QComboBox(tab3ControlBar); cmbTpsw->setObjectName("cmbColorTab3_TPSW");
        cmbTpsw->addItems({"🌈 TPSW 风格: Jet", "🔥 TPSW 风格: Hot", "🌌 TPSW 风格: Thermal", "⚫ TPSW 风格: Grayscale", "❄️ TPSW 风格: Polar"}); cmbTpsw->setStyleSheet(cmbStyle);

        QComboBox* cmbDp = new QComboBox(tab3ControlBar); cmbDp->setObjectName("cmbColorTab3_DP");
        cmbDp->addItems({"🌈 DP 风格: Jet", "🔥 DP 风格: Hot", "🌌 DP 风格: Thermal", "⚫ DP 风格: Grayscale", "❄️ DP 风格: Polar"}); cmbDp->setStyleSheet(cmbStyle);

        tab3ControlLayout->addWidget(cmbRaw);
        tab3ControlLayout->addWidget(cmbTpsw);
        tab3ControlLayout->addWidget(cmbDp);
        tab3ControlLayout->setStretch(0, 1); tab3ControlLayout->setStretch(1, 1); tab3ControlLayout->setStretch(2, 1);
        tab3Layout->addWidget(tab3ControlBar);

        auto connectTab3Combo = [this](QComboBox* cmb, const QString& prefix) {
            connect(cmb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, prefix](int index){
                QCPColorGradient grad = QCPColorGradient::gpJet;
                if(index==1) grad = QCPColorGradient::gpHot; else if(index==2) grad = QCPColorGradient::gpThermal; else if(index==3) grad = QCPColorGradient::gpGrayscale; else if(index==4) grad = QCPColorGradient::gpPolar;
                if (m_lofarWaterfallWidget) {
                    for (QCustomPlot* p : m_lofarWaterfallWidget->findChildren<QCustomPlot*>()) {
                        if (p->objectName().startsWith(prefix)) {
                            if (p->plottableCount() > 0) {
                                qobject_cast<QCPColorMap*>(p->plottable(0))->setGradient(grad);
                                p->replot();
                            }
                        }
                    }
                }
            });
        };
        connectTab3Combo(cmbRaw, "offline_raw_");
        connectTab3Combo(cmbTpsw, "offline_tpsw_");
        connectTab3Combo(cmbDp, "offline_dp_");

        QScrollArea* lofarScroll = new QScrollArea(tab3);
        lofarScroll->setObjectName("scrollTab3"); // 【新增户口】
        lofarScroll->setWidgetResizable(true);
        m_lofarWaterfallWidget = new QWidget(lofarScroll);
        m_lofarWaterfallLayout = new QGridLayout(m_lofarWaterfallWidget);
        m_lofarWaterfallLayout->setAlignment(Qt::AlignTop);
        lofarScroll->setWidget(m_lofarWaterfallWidget);
        tab3Layout->addWidget(lofarScroll);
        tab3->setProperty("absoluteIndex", 2);
        m_mainTabWidget->addTab(tab3, "批处理：TPSW均衡与DP寻优");

    // ================== TAB 4 ==================
    QWidget* tab4 = new QWidget();
    // 【修改 1】：原有的 tab4Layout 降级为主布局，只用来装滚动条
    QVBoxLayout* tab4MainLayout = new QVBoxLayout(tab4);

    // 【新增】：给 Tab 4 增加全局滚动区域
    QScrollArea* tab4Scroll = new QScrollArea(tab4);
    tab4Scroll->setWidgetResizable(true);
    tab4Scroll->setFrameShape(QFrame::NoFrame);
    tab4Scroll->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");

    // 【新增】：创建真正的内层容器，所有的表格和图表都将挂载在这个容器上
    QWidget* tab4Container = new QWidget(tab4Scroll);
    QVBoxLayout* tab4Layout = new QVBoxLayout(tab4Container);

    // 【修改 2】：挂载父级改为 tab4Container
    QWidget* cardsWidget = new QWidget(tab4Container);
    QHBoxLayout* cardsLayout = new QHBoxLayout(cardsWidget);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_lblStatTime = new QLabel("--"); m_lblStatTargets = new QLabel("--"); m_lblStatAvgAcc = new QLabel("--");
    // 修改为深灰色底
    cardsLayout->addWidget(createCardWidget(m_lblStatTime, "#1e1e1e", "系统全流程解算耗时分布"));
    cardsLayout->addWidget(createCardWidget(m_lblStatTargets, "#1e1e1e", "稳定识别/锁定目标总数"));
    cardsLayout->addWidget(createCardWidget(m_lblStatAvgAcc, "#1e1e1e", "全局平均线谱提取正确率"));
    tab4Layout->addWidget(cardsWidget);

    // 【修改 3】：挂载父级改为 tab4Container
    QSplitter* tab4ContentSplitter = new QSplitter(Qt::Vertical, tab4Container);

    QWidget* tablesContainer = new QWidget(tab4ContentSplitter);
    QVBoxLayout* tablesLayout = new QVBoxLayout(tablesContainer);
    tablesLayout->setContentsMargins(0, 0, 0, 0);
    tablesLayout->setSpacing(10);

    m_tableTargetFeatures = new QTableWidget(tablesContainer);
    m_tableTargetFeatures->setColumnCount(9);
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
    // 替换 m_tableTargetFeatures 的样式
    m_tableTargetFeatures->setStyleSheet("QTableWidget { background-color: #1e1e1e; color: #dcdde1; border-radius: 8px; border: 1px solid #333; alternate-background-color: #252526; } QHeaderView::section { background-color: #2c3e50; color: #2ecc71; font-weight: bold; border: none; padding: 6px; }");
    tablesLayout->addWidget(m_tableTargetFeatures);

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
    // 替换 m_tableMfpResults 的样式
    m_tableMfpResults->setStyleSheet("QTableWidget { background-color: #1e1e1e; color: #dcdde1; border-radius: 8px; border: 1px solid #333; alternate-background-color: #252526; } QHeaderView::section { background-color: #8e44ad; color: #ffffff; font-weight: bold; border: none; padding: 6px; }");
    m_tableMfpResults->setVisible(false);
    tablesLayout->addWidget(m_tableMfpResults);

    tab4ContentSplitter->addWidget(tablesContainer);

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
    // 针对 Tab 4 的三个图表，替换原有的 setBrush 代码：
    m_plotTrueAzimuth->legend->setBrush(QColor(20, 20, 20, 200));
    m_plotTrueAzimuth->legend->setTextColor(QColor("#dcdde1"));
    midPlotsSplitter->addWidget(m_plotTrueAzimuth);

    m_plotCalcAzimuth = new QCustomPlot(midPlotsSplitter);
    m_plotCalcAzimuth->setMinimumSize(300, 200); m_plotCalcAzimuth->setStyleSheet("background-color: white; border-radius: 8px;");
    m_plotCalcAzimuth->plotLayout()->insertRow(0); m_plotCalcAzimuth->plotLayout()->addElement(0, 0, new QCPTextElement(m_plotCalcAzimuth, "解算跟踪方位轨迹演变", QFont("sans", 12, QFont::Bold)));
    m_plotCalcAzimuth->xAxis->setLabel("监控周期 (批次)"); m_plotCalcAzimuth->yAxis->setLabel("解算方位角 (°)");
    m_plotCalcAzimuth->yAxis->setRange(0, 180);
    setupPlotInteraction(m_plotCalcAzimuth);
    m_plotCalcAzimuth->legend->setVisible(true);
    m_plotCalcAzimuth->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignBottom | Qt::AlignLeft);
    m_plotCalcAzimuth->legend->setBrush(QColor(20, 20, 20, 200));
    m_plotCalcAzimuth->legend->setTextColor(QColor("#dcdde1"));
    midPlotsSplitter->addWidget(m_plotCalcAzimuth);

    tab4ContentSplitter->addWidget(midPlotsSplitter);

    QSplitter* bottomPlotsSplitter = new QSplitter(Qt::Horizontal, tab4ContentSplitter);
    m_plotTargetAccuracy = new QCustomPlot(bottomPlotsSplitter);
    m_plotTargetAccuracy->setMinimumSize(300, 200); m_plotTargetAccuracy->setStyleSheet("background-color: white; border-radius: 8px;");
    m_plotTargetAccuracy->plotLayout()->insertRow(0); m_plotTargetAccuracy->plotLayout()->addElement(0, 0, new QCPTextElement(m_plotTargetAccuracy, "各独立目标特征提取正确率", QFont("sans", 12, QFont::Bold)));
    m_plotTargetAccuracy->legend->setBrush(QColor(20, 20, 20, 200));
    m_plotTargetAccuracy->legend->setTextColor(QColor("#dcdde1"));
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
    m_plotTargetAccuracy->legend->setBrush(QColor(20, 20, 20, 200));
    m_plotTargetAccuracy->legend->setTextColor(QColor("#dcdde1"));
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
    tab4Layout->addWidget(tab4ContentSplitter, 1);

    // 【新增】：将装备完毕的内层容器放入滚动区，最后塞进 Tab 的主布局里
    tab4Scroll->setWidget(tab4Container);
    tab4MainLayout->addWidget(tab4Scroll);

    tab4->setProperty("absoluteIndex", 3); // 【新增】：赋予绝对顺序编号 3
    m_mainTabWidget->addTab(tab4, "批处理：综合效能成果");





    // ==========================================
    // 底部：评估报告终端
    // ==========================================
    verticalSplitter->addWidget(topWidget);
    QGroupBox* groupReport = new QGroupBox("综合处理评估报告终端", verticalSplitter);
    QVBoxLayout* reportLayout = new QVBoxLayout(groupReport);
    m_reportConsole = new QPlainTextEdit(this);
    m_reportConsole->setStyleSheet("background-color: #0a0a0a; color: #00ff00; font-family: Consolas; font-size: 13px; border: 1px solid #333;");
    reportLayout->addWidget(m_reportConsole);
    verticalSplitter->addWidget(groupReport);
    verticalSplitter->setStretchFactor(0, 4); verticalSplitter->setStretchFactor(1, 1);




    // ==========================================
    // 全局视图控制工具栏 (带有多屏弹出功能)
    // ==========================================
    QToolBar* viewToolBar = new QToolBar(this); // 挂在主窗口上
    viewToolBar->setMovable(false);
    viewToolBar->setStyleSheet("QToolBar { background-color: #1a1a1a; border-bottom: 1px solid #333; padding: 2px; }");
    this->addToolBar(Qt::TopToolBarArea, viewToolBar);

    QPushButton* toggleLeftBtn = new QPushButton("◀ 隐藏控制栏", this);
    toggleLeftBtn->setCheckable(true); toggleLeftBtn->setCursor(Qt::PointingHandCursor);
    toggleLeftBtn->setStyleSheet("QPushButton { border: none; padding: 4px 10px; color: #dcdde1; font-weight: bold; } QPushButton:hover { color: #3498db; }");
    connect(toggleLeftBtn, &QPushButton::toggled, this, [leftPanel, toggleLeftBtn](bool checked){
        leftPanel->setVisible(!checked); toggleLeftBtn->setText(checked ? "▶ 展开控制栏" : "◀ 隐藏控制栏");
    });
    viewToolBar->addWidget(toggleLeftBtn);

    // 弹簧占位符
    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    viewToolBar->addWidget(spacer);

    // 【新增】：独立大屏弹出按钮
    QPushButton* btnPopOutTab = new QPushButton("🪟 弹出当前模块为独立大屏", this);
    btnPopOutTab->setCursor(Qt::PointingHandCursor);
    btnPopOutTab->setStyleSheet("QPushButton { border: 1px solid #2980b9; border-radius: 4px; padding: 4px 15px; background-color: #ebf5fb; color: #2980b9; font-weight: bold; font-size: 13px; margin-right: 15px; } QPushButton:hover { background-color: #3498db; color: white; }");
    connect(btnPopOutTab, &QPushButton::clicked, this, &MainWindow::popOutCurrentTab);
    viewToolBar->addWidget(btnPopOutTab);

    QPushButton* toggleBottomBtn = new QPushButton("▼ 隐藏报告栏", this);
    toggleBottomBtn->setCheckable(true); toggleBottomBtn->setCursor(Qt::PointingHandCursor);
    toggleBottomBtn->setStyleSheet("QPushButton { border: none; padding: 4px 10px; color: #dcdde1; font-weight: bold; } QPushButton:hover { color: #3498db; }");
    connect(toggleBottomBtn, &QPushButton::toggled, this, [groupReport, toggleBottomBtn](bool checked){
        groupReport->setVisible(!checked); toggleBottomBtn->setText(checked ? "▲ 展开报告栏" : "▼ 隐藏报告栏");
    });
    viewToolBar->addWidget(toggleBottomBtn);
    // 【新增】：软件启动时，默认触发该按钮的选中状态（即触发隐藏逻辑）
    toggleBottomBtn->setChecked(true);

    QPushButton* toggleRightBtn = new QPushButton("隐藏终端栏 ▶", this);
    toggleRightBtn->setCheckable(true); toggleRightBtn->setCursor(Qt::PointingHandCursor);
    toggleRightBtn->setStyleSheet("QPushButton { border: none; padding: 4px 10px; color: #dcdde1; font-weight: bold; } QPushButton:hover { color: #3498db; }");
    connect(toggleRightBtn, &QPushButton::toggled, this, [rightSidePanel, toggleRightBtn](bool checked){
        rightSidePanel->setVisible(!checked); toggleRightBtn->setText(checked ? "◀ 展开终端栏" : "隐藏终端栏 ▶");
    });
    viewToolBar->addWidget(toggleRightBtn);



    mainVLayout->addWidget(verticalSplitter);
    setCentralWidget(centralWidget);
    resize(1600, 1000);
    setWindowTitle("SonarTracker");
    // 【新增】：在 UI 初始化最后，挂载浮动通知层
    setupNotificationArea();
    fixAllPlotTitles();
    // ==========================================
    // 【关键修复 4】：打通无边框窗口的全局鼠标追踪引擎
    // ==========================================
    // 强制唤醒界面上所有的子控件鼠标追踪能力
    QList<QWidget*> allWidgets = this->findChildren<QWidget*>();
    for (QWidget* w : allWidgets) {
        w->setMouseTracking(true);
    }
    // 将 MainWindow 注册为全局事件过滤器（开启上帝视角）
    qApp->installEventFilter(this);
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
    if (m_worker->isPaused()) {
        m_worker->resume();
        m_lblSysInfo->setText(QString("状态: 运行中\n任务: %1\n开始时间: %2").arg(m_editTaskName->text()).arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
        appendLog("\n>> 算法已继续执行。");

        // 【新增向远端发指令】
        if (m_chkUdpMode->isChecked() && m_cmdSocket) {
            m_cmdSocket->writeDatagram("CMD:RESUME", QHostAddress(m_udpRemoteAddress), m_udpRemotePort);
            appendLog(">> 📡 已向阵列远端发送 [RESUME] 恢复投递指令！\n");
        }
    } else {
        m_worker->pause();
        m_lblSysInfo->setText("状态: 已挂起 (暂停)");
        appendLog("\n>> 算法已挂起暂停。");

        // 【新增向远端发指令】
        if (m_chkUdpMode->isChecked() && m_cmdSocket) {
            m_cmdSocket->writeDatagram("CMD:PAUSE", QHostAddress(m_udpRemoteAddress), m_udpRemotePort);
            appendLog(">> 📡 已向阵列远端发送 [PAUSE] 暂停投递指令！\n");
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

    // =========================================================
    // 【优化】：一键导出四个 Tab 面板的整体截图 (兼容独立弹窗及超长滚动图)
    // =========================================================
    int originalTabIndex = m_mainTabWidget ? m_mainTabWidget->currentIndex() : -1;

    QStringList tabFileNames = {
        "Tab1_实时探测与关联全景.png",
        "Tab2_常规与高分辨时空全景切片.png",
        "Tab3_批处理TPSW与DP线谱寻优.png",
        "Tab4_综合效能成果仪表盘.png"
    };

    // 1. 收集所有的 Tab，无论它是在主界面的 Tab 栏里，还是已经被单独弹出了
        QMap<int, QWidget*> allTabs;
        if (m_mainTabWidget) {
            for (int i = 0; i < m_mainTabWidget->count(); ++i) {
                QWidget* w = m_mainTabWidget->widget(i);
                allTabs[w->property("absoluteIndex").toInt()] = w;
            }
        }
        for (QWidget* w : m_popupTabs.values()) {
            allTabs[w->property("absoluteIndex").toInt()] = w;
        }

        // =========================================================
        // 【魔法截图函数重构】：完美支持 Tab 1 双面板结构及 Tab 2/3 长图
        // =========================================================
        auto captureFullContent = [this](QWidget* container) -> QPixmap {
            if (!container) return QPixmap();

            int absIdx = container->property("absoluteIndex").toInt();

            // --- 情况一：处理 Tab 1 (双面板结构：左侧固定 + 右侧滚动) ---
            if (absIdx == 0) {
                // 找到 Tab 1 右侧那个装载目标的 GridLayout 所在的 Widget (m_targetPanelWidget)
                if (m_targetPanelWidget && m_targetPanelWidget->parentWidget()) {
                    // 向上追溯找到它的滚动容器
                    QScrollArea* sa = qobject_cast<QScrollArea*>(m_targetPanelWidget->parentWidget()->parentWidget());
                    if (sa) {
                        // 计算出底部目标由于滚动而被隐藏的高度差
                        int extraHeight = m_targetPanelWidget->height() - sa->viewport()->height();
                        if (extraHeight > 0) {
                            QSize origSize = container->size();
                            // 核心黑科技：瞬间拉伸整个 Tab 1 容器，迫使内部所有元素（含左侧方位图）同步伸长
                            container->resize(origSize.width(), origSize.height() + extraHeight);
                            QCoreApplication::processEvents(); // 强制布局刷新
                            QPixmap pix = container->grab();
                            container->resize(origSize);       // 瞬间还原
                            return pix;
                        }
                    }
                }
                return container->grab(); // 如果不需要滚动则直接截取
            }

            // --- 情况二：处理 Tab 2, 3, 4 (统一滚动容器结构) ---
            // 尝试寻找容器内的 ScrollArea (处理主界面状态) 或本身就是 ScrollArea (处理弹窗状态)
            QScrollArea* scrollArea = container->findChild<QScrollArea*>();
            if (!scrollArea) scrollArea = qobject_cast<QScrollArea*>(container);

            if (scrollArea && scrollArea->widget()) {
                // 直接截取 ScrollArea 内部那个被撑得很长的透明画布 Widget
                return scrollArea->widget()->grab();
            }

            // 兜底：如果都没有，截取当前可见区域
            return container->grab();
        };

        // 2. 依次按绝对顺序进行截图保存
        for (int i = 0; i < 4; ++i) {
            if (!allTabs.contains(i)) continue;

            QWidget* targetTab = allTabs[i];
            // 判定该 Tab 是在主窗口内还是在弹出的独立窗口里
            int indexInTabWidget = m_mainTabWidget ? m_mainTabWidget->indexOf(targetTab) : -1;

            if (indexInTabWidget != -1) {
                // A. 在主界面中：强行切到该页（不切过去抓不到隐藏的布局），刷新后截图
                m_mainTabWidget->setCurrentIndex(indexInTabWidget);
                QCoreApplication::processEvents();
                captureFullContent(targetTab).save(plotsDirPath + "/" + tabFileNames[i]);
            } else {
                // B. 在独立弹窗中：直接对其所在的窗口对象应用魔法截图
                captureFullContent(targetTab).save(plotsDirPath + "/" + tabFileNames[i]);
            }
        }

        // 截图完毕后，切回用户原本所在的 Tab
        if (m_mainTabWidget && originalTabIndex != -1) {
            m_mainTabWidget->setCurrentIndex(originalTabIndex);
        }

    appendLog(QString("\n>> 成功：分析报表及四大全景面板已完整导出至 %1\n").arg(fileName));
    appendLog(QString(">> 成功：所有配套图表已导出至文件夹 %1\n").arg(plotsDirPath));

    QMessageBox::information(this, "导出成功",
                             QString("综合评估报表及运行日志已成功导出！\n\n此外，当前所有图表及面板也已自动高分辨率保存为图片，位于同级配套目录：\n%1").arg(plotsDirPath));
}



void MainWindow::createTargetPlots(int targetId) {
    // 获取当前布局模式
    QComboBox* cmb = this->findChild<QComboBox*>("cmbLayoutMode");
    bool isAutoFit = (cmb && cmb->currentIndex() == 1);
    int minHeight = isAutoFit ? 0 : 200;

    QCustomPlot* lsPlot = new QCustomPlot(this);
    lsPlot->setObjectName(QString("ls_%1").arg(targetId));
    setupPlotInteraction(lsPlot);
    lsPlot->addGraph(); lsPlot->graph(0)->setPen(QPen(Qt::red, 1.5));
    lsPlot->xAxis->setLabel("频率/Hz"); lsPlot->yAxis->setLabel("功率/dB");
    // 恢复瞬时线谱的 Y 轴范围
    lsPlot->yAxis->setRange(-60, 40);
    lsPlot->plotLayout()->insertRow(0); lsPlot->plotLayout()->addElement(0, 0, new QCPTextElement(lsPlot, "", QFont("sans", 9, QFont::Bold)));

    QCustomPlot* lofarPlot = new QCustomPlot(this);
    lofarPlot->setObjectName(QString("lofar_%1").arg(targetId));
    setupPlotInteraction(lofarPlot);
    lofarPlot->addGraph(); lofarPlot->graph(0)->setPen(QPen(Qt::blue, 1.5));
    lofarPlot->xAxis->setLabel("频率/Hz"); lofarPlot->yAxis->setLabel("功率/dB");
    // 恢复 LOFAR 的 Y 轴范围
    lofarPlot->yAxis->setRange(-60, 40);
    lofarPlot->plotLayout()->insertRow(0); lofarPlot->plotLayout()->addElement(0, 0, new QCPTextElement(lofarPlot, "", QFont("sans", 9, QFont::Bold)));

    QCustomPlot* demonPlot = new QCustomPlot(this);
    demonPlot->setObjectName(QString("demon_%1").arg(targetId));
    setupPlotInteraction(demonPlot);
    demonPlot->addGraph(); demonPlot->graph(0)->setPen(QPen(Qt::darkGreen, 1.5));
    demonPlot->xAxis->setLabel("频率/Hz"); demonPlot->yAxis->setLabel("归一幅度");
    // 恢复 DEMON 轴频的 Y 轴归一化范围 (0~1.1)
    demonPlot->yAxis->setRange(0, 1.1);
    demonPlot->plotLayout()->insertRow(0); demonPlot->plotLayout()->addElement(0, 0, new QCPTextElement(demonPlot, "", QFont("sans", 9, QFont::Bold)));

    // 【关键修改 1】：使用 setMinimumSize(0, ...) 彻底破除 QCustomPlot 底层默认的 50x50 尺寸锁定
    lsPlot->setMinimumSize(0, minHeight);
    lofarPlot->setMinimumSize(0, minHeight);
    demonPlot->setMinimumSize(0, minHeight);

    m_lsPlots.insert(targetId, lsPlot); m_lofarPlots.insert(targetId, lofarPlot); m_demonPlots.insert(targetId, demonPlot);

    int row = targetId - 1;

    QWidget* wLs = wrapPlotWithRangeControl(lsPlot, "滤波频段(Hz):", 0, m_currentConfig.fs/2.0, m_currentConfig.lofarMin, m_currentConfig.lofarMax);
    // 【关键修改 2】：外骨骼容器也必须用 setMinimumSize，全显模式下高度为 0，允许无限挤压
    wLs->setMinimumSize(0, isAutoFit ? 0 : minHeight + 30);
    m_targetLayout->addWidget(wLs, row, 0);

    QWidget* wLofar = wrapPlotWithRangeControl(lofarPlot, "LOFAR频段(Hz):", 0, m_currentConfig.fs/2.0, m_currentConfig.lofarMin, m_currentConfig.lofarMax);
    wLofar->setMinimumSize(0, isAutoFit ? 0 : minHeight + 30);
    m_targetLayout->addWidget(wLofar, row, 1);

    // 【关键修改 3】：将 DEMON（轴频）的默认视野上限动态绑定为你界面输入的 demonMax！
    QWidget* wDemon = wrapPlotWithRangeControl(demonPlot, "轴频段(Hz):", 0.0, m_currentConfig.fs/2.0, 0.0, 50);
    wDemon->setMinimumSize(0, isAutoFit ? 0 : minHeight + 30);
    m_targetLayout->addWidget(wDemon, row, 2);
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
    // 【修改】：注释掉自动追踪，防止与上方的手动控制器打架
    // if (foundX) {
    //     m_timeAzimuthPlot->xAxis->setRange(xRange.lower - 5.0, xRange.upper + 5.0);
    // } else {
    //     m_timeAzimuthPlot->xAxis->setRange(0, 180);
    // }
    m_timeAzimuthPlot->replot();
    updatePlotOriginalRange(m_timeAzimuthPlot);

    for (const TargetTrack& t : result.tracks) {
        // 【意见二】：目标指示灯常亮逻辑 (只对转正目标生成)
        if (t.isConfirmed) {
            QString displayName = m_targetNames.value(t.id, QString("T%1").arg(t.id));

            if (!m_targetLights.contains(t.id)) {
                QLabel* light = new QLabel(this);
                light->setAlignment(Qt::AlignCenter);
                m_targetLightsLayout->addWidget(light);
                m_targetLights.insert(t.id, light);

                // ========================================================
                // 【新增】：当发现系统之前没有这个目标的指示灯时，说明是新锁定的目标
                // 此时触发右上角的浮动 Toast 通知！
                // ========================================================
                QString notifyMsg = QString("系统自动跟踪算法已锁定新目标：\n● 名称: %1\n● 方位: %2°")
                        .arg(displayName)
                        .arg(t.currentAngle, 0, 'f', 1);

                // 提取最强的一根线谱作为特征展示（如果有的话）
                if (!t.lineSpectra.empty()) {
                    notifyMsg += QString("\n● 特征: %1 Hz").arg(t.lineSpectra.front(), 0, 'f', 1);
                }

                showNotification("🎯 发现并锁定新目标", notifyMsg);
                // ========================================================
            }

            QLabel* light = m_targetLights[t.id];

            if (t.isActive) {
                light->setText(QString("🟢 %1").arg(displayName));
                // 激活状态：墨绿底色，亮绿文字
                light->setStyleSheet("background-color: #1a3a26; color: #2ecc71; font-size: 13px; font-weight: bold; border: 1px solid #2ecc71; border-radius: 4px; padding: 3px 6px; margin-right: 2px;");
            } else {
                light->setText(QString("⚪ %1 (熄灭)").arg(displayName));
                // 熄灭状态：深灰底色，暗灰文字
                light->setStyleSheet("background-color: #222222; color: #7f8c8d; font-size: 13px; font-weight: bold; border: 1px solid #555555; border-radius: 4px; padding: 3px 6px; margin-right: 2px;");
            }
        }

        // ... 后面的画图逻辑保持完全不变 ...
        if (!m_lofarPlots.contains(t.id)) createTargetPlots(t.id);
        // ...
        QCustomPlot* lsp = m_lsPlots[t.id]; QCustomPlot* lp = m_lofarPlots[t.id]; QCustomPlot* dp = m_demonPlots[t.id];
        QString statusStr = t.isActive ? "[跟踪中]" : "[已熄火]";
        QColor lsColor = t.isActive ? Qt::red : Qt::darkGray; QColor lofarColor = t.isActive ? Qt::blue : Qt::darkGray; QColor demonColor = t.isActive ? Qt::darkGreen : Qt::darkGray;
        // 【修改】：使用暗黑背景和绿色醒目字体
        QColor bgColor = t.isActive ? QColor("#141414") : QColor("#0a0a0a");
        // 【关键修复】：无论是否激活，动态曲线图的标题都强制使用白色
        QColor textColor = t.isActive ? QColor("#dcdde1") : QColor("#7f8c8d");

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
    fixAllPlotTitles();
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

    // 获取当前下拉框的布局模式状态
    QComboBox* cmbLayout = this->findChild<QComboBox*>("cmbLayoutMode");
    int minHeight = (cmbLayout && cmbLayout->currentIndex() == 1) ? 0 : 250;

    // 【终极修复】：辅助获取 Tab3 三列专属色棒风格的 Lambda 函数，增加多屏穿透能力
    auto getGrad = [this](const QString& objName) -> QCPColorGradient {
        QComboBox* cmb = nullptr;
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            cmb = widget->findChild<QComboBox*>(objName);
            if (cmb) break;
        }

        if (!cmb) return QCPColorGradient::gpJet;

        int idx = cmb->currentIndex();
        if(idx == 1) return QCPColorGradient::gpHot;
        if(idx == 2) return QCPColorGradient::gpThermal;
        if(idx == 3) return QCPColorGradient::gpGrayscale;
        if(idx == 4) return QCPColorGradient::gpPolar;
        return QCPColorGradient::gpJet;
    };

    // 获取当前三列分别选中的颜色梯度
    QCPColorGradient gradRaw = getGrad("cmbColorTab3_RAW");
    QCPColorGradient gradTpsw = getGrad("cmbColorTab3_TPSW");
    QCPColorGradient gradDp = getGrad("cmbColorTab3_DP");

    int row = 0; // 将 col 改为 row
    for (const auto& res : results) {
        // ==================== 1. RAW 图表构建 ====================
        QCustomPlot* pRaw = new QCustomPlot(m_lofarWaterfallWidget);
        pRaw->setObjectName(QString("offline_raw_%1").arg(res.targetId));
        setupPlotInteraction(pRaw);

        // 【核心修复】：为 RAW 图表套上外骨骼包装器！
        QWidget* wRaw = wrapPlotWithRangeControl(pRaw, "聚焦频段(Hz):", 0, m_currentConfig.fs/2.0, res.displayFreqMin, res.displayFreqMax);
        wRaw->setMinimumSize(0, minHeight);
        m_lofarWaterfallLayout->addWidget(wRaw, row, 0);

        pRaw->plotLayout()->insertRow(0);
        QCPTextElement* titleRaw = new QCPTextElement(pRaw, QString("目标%1 原始LOFAR谱 (随批次积累)").arg(res.targetId), QFont("sans", 10, QFont::Bold));
        titleRaw->setTextColor(QColor("#dcdde1"));
        pRaw->plotLayout()->addElement(0, 0, titleRaw);

        QCPColorMap *cmapRaw = new QCPColorMap(pRaw->xAxis, pRaw->yAxis);
        cmapRaw->data()->setSize(res.freqBins, res.timeFrames);
        cmapRaw->data()->setRange(QCPRange(0, m_currentConfig.fs/2.0), QCPRange(res.minTime, res.maxTime));
        double rmax = -999;
        for(double v : res.rawLofarDb) if(v > rmax) rmax = v;
        for(int t=0; t<res.timeFrames; ++t)
            for(int f=0; f<res.freqBins; ++f)
                cmapRaw->data()->setCell(f, t, res.rawLofarDb[t * res.freqBins + f] - rmax);

        cmapRaw->setGradient(gradRaw);
        cmapRaw->setInterpolate(true);
        cmapRaw->setDataRange(QCPRange(-40.0, 0)); cmapRaw->setTightBoundary(true);
        pRaw->xAxis->setLabel("频率/Hz"); pRaw->yAxis->setLabel("物理时间/s");
        // pRaw->xAxis->setRange(res.displayFreqMin, res.displayFreqMax); // 交给包装器控制
        pRaw->yAxis->setRange(res.minTime, res.maxTime);
        updatePlotOriginalRange(pRaw);

        // ==================== 2. TPSW 图表构建 ====================
        QCustomPlot* pTpsw = new QCustomPlot(m_lofarWaterfallWidget);
        pTpsw->setObjectName(QString("offline_tpsw_%1").arg(res.targetId));
        setupPlotInteraction(pTpsw);

        // 【核心修复】：为 TPSW 图表套上外骨骼包装器！
        QWidget* wTpsw = wrapPlotWithRangeControl(pTpsw, "聚焦频段(Hz):", 0, m_currentConfig.fs/2.0, res.displayFreqMin, res.displayFreqMax);
        wTpsw->setMinimumSize(0, minHeight);
        m_lofarWaterfallLayout->addWidget(wTpsw, row, 1);

        pTpsw->plotLayout()->insertRow(0);
        QCPTextElement* titleTpsw = new QCPTextElement(pTpsw, QString("目标%1 历史LOFAR谱 (TPSW背景均衡)").arg(res.targetId), QFont("sans", 10, QFont::Bold));
        titleTpsw->setTextColor(QColor("#dcdde1"));
        pTpsw->plotLayout()->addElement(0, 0, titleTpsw);

        QCPColorMap *cmapTpsw = new QCPColorMap(pTpsw->xAxis, pTpsw->yAxis);
        cmapTpsw->data()->setSize(res.freqBins, res.timeFrames);
        cmapTpsw->data()->setRange(QCPRange(0, m_currentConfig.fs/2.0), QCPRange(res.minTime, res.maxTime));

        double tpswMaxVal = -9999.0;
        for(int t=0; t<res.timeFrames; ++t) {
            for(int f=0; f<res.freqBins; ++f) {
                double val = res.tpswLofarDb[t * res.freqBins + f];
                cmapTpsw->data()->setCell(f, t, val);
                if (val > tpswMaxVal) tpswMaxVal = val;
            }
        }

        cmapTpsw->setGradient(gradTpsw);
        cmapTpsw->setInterpolate(true);

        double lowerBound = (tpswMaxVal > 15.0) ? (tpswMaxVal - 10.0) : 3.0;

        cmapTpsw->setDataRange(QCPRange(lowerBound, tpswMaxVal));
        cmapTpsw->setTightBoundary(true);

        pTpsw->xAxis->setLabel("频率/Hz"); pTpsw->yAxis->setLabel("物理时间/s");
        // pTpsw->xAxis->setRange(res.displayFreqMin, res.displayFreqMax); // 交给包装器控制
        pTpsw->yAxis->setRange(res.minTime, res.maxTime);
        updatePlotOriginalRange(pTpsw);

        // ==================== 3. DP 图表构建 ====================
        QCustomPlot* pDp = new QCustomPlot(m_lofarWaterfallWidget);
        pDp->setObjectName(QString("offline_dp_%1").arg(res.targetId));
        setupPlotInteraction(pDp);

        // 【核心修复】：为 DP 图表套上外骨骼包装器！
        QWidget* wDp = wrapPlotWithRangeControl(pDp, "聚焦频段(Hz):", 0, m_currentConfig.fs/2.0, res.displayFreqMin, res.displayFreqMax);
        wDp->setMinimumSize(0, minHeight);
        m_lofarWaterfallLayout->addWidget(wDp, row, 2);

        pDp->plotLayout()->insertRow(0);
        QCPTextElement* titleDp = new QCPTextElement(pDp, QString("目标%1 专属线谱连续轨迹图 (DP寻优)").arg(res.targetId), QFont("sans", 10, QFont::Bold));
        titleDp->setTextColor(QColor("#dcdde1"));
        pDp->plotLayout()->addElement(0, 0, titleDp);

        QCPColorMap *cmapDp = new QCPColorMap(pDp->xAxis, pDp->yAxis);
        cmapDp->data()->setSize(res.freqBins, res.timeFrames);
        cmapDp->data()->setRange(QCPRange(0, m_currentConfig.fs/2.0), QCPRange(res.minTime, res.maxTime));
        for(int t=0; t<res.timeFrames; ++t)
            for(int f=0; f<res.freqBins; ++f)
                cmapDp->data()->setCell(f, t, res.dpCounter[t * res.freqBins + f]);

        cmapDp->setGradient(gradDp);
        cmapDp->setInterpolate(true);
        cmapDp->setDataRange(QCPRange(0, 10));
        cmapDp->setTightBoundary(true);

        pDp->xAxis->setLabel("频率/Hz"); pDp->yAxis->setLabel("物理时间/s");
        // pDp->xAxis->setRange(res.displayFreqMin, res.displayFreqMax); // 交给包装器控制
        pDp->yAxis->setRange(res.minTime, res.maxTime);
        updatePlotOriginalRange(pDp);

        row++; // 本目标处理完毕，行数加 1
    }

    fixAllPlotTitles();
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

    // ========================================================
    // 【终极修复】：每次刷新帧时，动态读取各自下拉框当前的颜色状态，避免重置
    // 增加多屏穿透能力：无论下拉框是在主窗口还是弹出的独立窗口中，都能精确找到！
    // ========================================================
    auto getGrad = [this](const QString& objName) -> QCPColorGradient {
        // 第一步：先在全局应用程序范围的所有窗口里搜寻这个名字的 ComboBox
        QComboBox* cmb = nullptr;
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            cmb = widget->findChild<QComboBox*>(objName);
            if (cmb) break;
        }

        // 如果实在是找不到，才返回默认值
        if (!cmb) return QCPColorGradient::gpJet;

        int idx = cmb->currentIndex();
        if(idx == 1) return QCPColorGradient::gpHot;
        if(idx == 2) return QCPColorGradient::gpThermal;
        if(idx == 3) return QCPColorGradient::gpGrayscale;
        if(idx == 4) return QCPColorGradient::gpPolar;
        return QCPColorGradient::gpJet;
    };

    cmapCbf->setGradient(getGrad("cmbColorTab2_CBF")); // 动态读取 CBF 专属颜色

    cmapCbf->setInterpolate(true);
    cmapCbf->setDataRange(QCPRange(cbf_max - 20.0, cbf_max)); cmapCbf->setTightBoundary(true);
    m_cbfWaterfallPlot->xAxis->setLabel("方位角/°"); m_cbfWaterfallPlot->yAxis->setLabel("物理时间/s");
    m_cbfWaterfallPlot->yAxis->setRange(min_time, max_time);
    m_cbfWaterfallPlot->replot(); updatePlotOriginalRange(m_cbfWaterfallPlot);

    cmapDcv->setGradient(getGrad("cmbColorTab2_DCV")); // 动态读取 DCV 专属颜色
    cmapDcv->setInterpolate(true);
    cmapDcv->setDataRange(QCPRange(dcv_max - 20.0, dcv_max)); cmapDcv->setTightBoundary(true);
    m_dcvWaterfallPlot->xAxis->setLabel("方位角/°"); m_dcvWaterfallPlot->yAxis->setLabel("物理时间/s");
    m_dcvWaterfallPlot->yAxis->setRange(min_time, max_time);
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
            if (!targetIds.contains(plotTid)) {
                // 【安全修复】：全局寻找并销毁外骨骼
                QWidget* wrapper = this->findChild<QWidget*>(objName + "_wrapper");
                if (wrapper) {
                    wrapper->hide(); wrapper->deleteLater();
                } else {
                    plot->hide(); plot->deleteLater();
                }
            }
        }
    }

    // 【新增】：获取当前下拉框的布局模式状态
    QComboBox* cmb = this->findChild<QComboBox*>("cmbLayoutMode");
    bool isAutoFit = (cmb && cmb->currentIndex() == 1); // 提取是否自动挤压的布尔值
    int minHeight = isAutoFit ? 0 : 250;

    int row = 0;
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

            // ==== 修复 Tab2 CBF切片标题 ====
                        QString cbfName = QString("slice_cbf_%1").arg(tid);
                        QCustomPlot* pCbf = m_sliceWidget->findChild<QCustomPlot*>(cbfName);
                        if (!pCbf) {
                            pCbf = new QCustomPlot(m_sliceWidget); pCbf->setObjectName(cbfName); setupPlotInteraction(pCbf);

                            // 【核心修改】：将原本黯淡的 Qt::gray 替换为极具穿透力的高亮青色（Cyan）
                            pCbf->addGraph(); pCbf->graph(0)->setPen(QPen(QColor("#00cec9"), 2.0));

                            QCPTextElement* titleCbf = new QCPTextElement(pCbf, "", QFont("sans", 10, QFont::Bold));
                            titleCbf->setTextColor(QColor("#dcdde1"));
                pCbf->plotLayout()->insertRow(0);
                pCbf->plotLayout()->addElement(0, 0, titleCbf);

                pCbf->xAxis->setRange(m_currentConfig.lofarMin, m_currentConfig.lofarMax); pCbf->yAxis->setRange(-80, 5);
                pCbf->xAxis->setVisible(true);
                pCbf->xAxis->setLabel("频率 / Hz");

                // 【核心修改 1】：强制废除 QCustomPlot 底层的 50x50 默认尺寸
                pCbf->setMinimumSize(0, minHeight);

                // 【核心修改 2】：用包装器挂载，并动态加入 30px 高度补偿
                QWidget* wCbf = wrapPlotWithRangeControl(pCbf, "切片频段(Hz):", 0, m_currentConfig.fs/2.0, m_currentConfig.lofarMin, m_currentConfig.lofarMax);
                wCbf->setMinimumSize(0, isAutoFit ? 0 : minHeight + 30);
                m_sliceLayout->addWidget(wCbf, row, 0);
            }
            pCbf->graph(0)->setData(f_axis, cbf_db);
            if (auto* title = qobject_cast<QCPTextElement*>(pCbf->plotLayout()->element(0, 0))) {
                title->setText(QString("目标%1 (约 %2°) - CBF").arg(tid).arg(avg_ang, 0, 'f', 1));
                title->setTextColor(QColor("#dcdde1"));
            }
            pCbf->yAxis->setLabel("相对功率 / dB");
            pCbf->replot(); updatePlotOriginalRange(pCbf);

            // ==== 修复 Tab2 DCV切片标题 ====
            QString dcvName = QString("slice_dcv_%1").arg(tid);
            QCustomPlot* pDcv = m_sliceWidget->findChild<QCustomPlot*>(dcvName);
            if (!pDcv) {
                pDcv = new QCustomPlot(m_sliceWidget); pDcv->setObjectName(dcvName); setupPlotInteraction(pDcv);
                pDcv->addGraph(); pDcv->graph(0)->setPen(QPen(Qt::red, 1.5));

                QCPTextElement* titleDcv = new QCPTextElement(pDcv, "", QFont("sans", 10, QFont::Bold));
                titleDcv->setTextColor(QColor("#dcdde1"));
                pDcv->plotLayout()->insertRow(0);
                pDcv->plotLayout()->addElement(0, 0, titleDcv);

                pDcv->xAxis->setRange(m_currentConfig.lofarMin, m_currentConfig.lofarMax); pDcv->yAxis->setRange(-80, 5);
                pDcv->xAxis->setLabel("频率 / Hz");

                // 【核心修改 3】：强制废除 QCustomPlot 底层的 50x50 默认尺寸
                pDcv->setMinimumSize(0, minHeight);

                // 【核心修改 4】：用包装器挂载，并动态加入 30px 高度补偿
                QWidget* wDcv = wrapPlotWithRangeControl(pDcv, "切片频段(Hz):", 0, m_currentConfig.fs/2.0, m_currentConfig.lofarMin, m_currentConfig.lofarMax);
                wDcv->setMinimumSize(0, isAutoFit ? 0 : minHeight + 30);
                m_sliceLayout->addWidget(wDcv, row, 1);
            }
            pDcv->graph(0)->setData(f_axis, dcv_db);

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

            if (auto* title = qobject_cast<QCPTextElement*>(pDcv->plotLayout()->element(0, 0))) {
                title->setText(QString("目标%1 (约 %2°) - DCV").arg(tid).arg(avg_ang, 0, 'f', 1));
                title->setTextColor(QColor("#dcdde1"));
            }
            pDcv->yAxis->setLabel("相对功率 / dB");
            pDcv->replot(); updatePlotOriginalRange(pDcv);

            row++;
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
    // 边框改为暗色 #333
    frame->setStyleSheet(QString("QFrame { background-color: %1; border-radius: 10px; border: 1px solid #333; }").arg(bgColor));
    QVBoxLayout* layout = new QVBoxLayout(frame);

    QLabel* titleLabel = new QLabel(title);
    // 标题文字改为亮灰色 #a0a0a0
    titleLabel->setStyleSheet("color: #a0a0a0; font-size: 14px; font-weight: bold; border: none;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // 核心数据文字改为荧光绿 #2ecc71
    contentLabel->setStyleSheet("color: #2ecc71; font-size: 22px; font-weight: bold; border: none;");
    contentLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(contentLabel);

    return frame;
}





void MainWindow::onEvaluationResultReady(const SystemEvaluationResult& res) {
    // 【新增】：计算空闲/等待时间
    double idleTimeSec = res.totalTimeSec - res.realtimeTimeSec - res.batchTimeSec;
    if (idleTimeSec < 0) idleTimeSec = 0.0; // 防止浮点数精度导致的微小负数

    m_lblStatTime->setText(QString("<span style='font-size:32px; color:#27ae60;'>%1</span>"
                                   "<span style='font-size:16px; color:#7f8c8d;'> s </span>"
                                   "<span style='font-size:14px; color:#7f8c8d;'>(实时 </span>"
                                   "<span style='font-size:18px; color:#2980b9;'>%2</span>"
                                   "<span style='font-size:14px; color:#7f8c8d;'> s | 批处理 </span>"
                                   "<span style='font-size:18px; color:#e67e22;'>%3</span>"
                                   "<span style='font-size:14px; color:#7f8c8d;'> s | 等待 </span>"
                                   "<span style='font-size:18px; color:#8e44ad;'>%4</span>"
                                   "<span style='font-size:14px; color:#7f8c8d;'> s)</span>")
                           .arg(res.totalTimeSec, 0, 'f', 1)
                           .arg(res.realtimeTimeSec, 0, 'f', 1)
                           .arg(res.batchTimeSec, 0, 'f', 2)
                           .arg(idleTimeSec, 0, 'f', 1)); // 【新增】绑定位位等待时间

    m_lblStatTargets->setText(QString("<span style='font-size:36px; color:#2980b9;'>%1</span> 艘").arg(res.confirmedTargetCount));
    // ... 下面的代码保持不变 ...

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

    // 找到这一行代码：
    m_plotTrueAzimuth->setVisible(anyHasTruth);
    m_plotTrueAzimuth->xAxis->setRange(0, currentBatchIndex + 1.5);
    m_plotTrueAzimuth->replot();

    m_plotCalcAzimuth->xAxis->setRange(0, currentBatchIndex + 1.5);
    m_plotCalcAzimuth->replot();

    // =====================================
    // 【新增下面这一行代码】：让批次正确率图表也跟随有无真值来决定是否显示
    m_plotBatchAccuracy->setVisible(anyHasTruth);
    // =====================================

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
        finalReport += "【系统级总体评价】\n";

        // 【修改】：在报告中也加上挂起/传输等待时间
        double idleTimeSec = res.totalTimeSec - res.realtimeTimeSec - res.batchTimeSec;
        if (idleTimeSec < 0) idleTimeSec = 0.0;

        finalReport += QString("  ▶ 全流程总计耗时: %1 秒 (实时解算: %2 秒 | 离线寻优: %3 秒 | 网络传输与挂起: %4 秒)\n")
                .arg(res.totalTimeSec, 0, 'f', 2)
                .arg(res.realtimeTimeSec, 0, 'f', 2)
                .arg(res.batchTimeSec, 0, 'f', 2)
                .arg(idleTimeSec, 0, 'f', 2);

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

    // ==========================================================
    // 【修改】：永远触发表格高度自适应更新，彻底消除内部滚动条
    // ==========================================================
    int totalHeight = m_tableTargetFeatures->horizontalHeader()->height() + 10;
    for (int i = 0; i < m_tableTargetFeatures->rowCount(); ++i) {
        totalHeight += m_tableTargetFeatures->rowHeight(i);
    }
    m_tableTargetFeatures->setMinimumHeight(totalHeight > 100 ? totalHeight : 100);
    m_tableTargetFeatures->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}


void MainWindow::onMfpResultReady(const QList<TargetEvaluation>& mfpResults) {
    disconnect(m_tableMfpResults, &QTableWidget::itemChanged, this, &MainWindow::onTargetNameChanged);
    m_tableMfpResults->setRowCount(0);

    for (int i = 0; i < mfpResults.size(); ++i) {
        const auto& mfp = mfpResults[i];
        m_latestMfpResults[mfp.targetId] = mfp;

        // 保证历史记录精准累加 n/m
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

    // ==========================================================
    // 【修改】：永远触发 MFP 表格高度更新，彻底消除内部滚动条
    // ==========================================================
    int totalHeight = m_tableMfpResults->horizontalHeader()->height() + 10;
    for (int i = 0; i < m_tableMfpResults->rowCount(); ++i) {
        totalHeight += m_tableMfpResults->rowHeight(i);
    }
    m_tableMfpResults->setMinimumHeight(totalHeight > 100 ? totalHeight : 100);
    m_tableMfpResults->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

void MainWindow::setupNotificationArea() {
    // 将 MainWindow 设为父对象，使其悬浮在所有其他控件之上
    m_notificationContainer = new QWidget(this);
    // 【核心修复】：让这个容器完全忽略鼠标事件，允许鼠标穿透点击下方的滚动条
    m_notificationContainer->setAttribute(Qt::WA_TransparentForMouseEvents);
    // 容器背景设为全透明，只显示内部的卡片
    m_notificationContainer->setStyleSheet("background: transparent;");

    // 使用垂直布局，从上往下堆叠通知卡片
    m_notificationLayout = new QVBoxLayout(m_notificationContainer);
    m_notificationLayout->setContentsMargins(0, 0, 0, 0);
    m_notificationLayout->setSpacing(10);
    m_notificationLayout->addStretch(); // 底部加入弹簧，让通知紧贴顶部排列

    // 提升容器的层级，确保不被图表遮挡
    m_notificationContainer->raise();
}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);

    if (m_notificationContainer) {
        // 动态计算右上角位置
        // 宽度固定 320，高度占满窗口（留出顶部边距），靠右侧 20px
        int containerWidth = 320;
        int paddingRight = 20;
        int paddingTop = 60; // 避开最顶部的工具栏

        m_notificationContainer->setGeometry(
                    this->width() - containerWidth - paddingRight,
                    paddingTop,
                    containerWidth,
                    this->height() - paddingTop
                    );
    }
}
void MainWindow::showNotification(const QString& title, const QString& message) {
    // 1. 同步将通知内容格式化，并永久写入右侧/底部的终端日志中！
    // 这样哪怕卡片消失了，你也可以在日志里随时翻看。
    QString logText = QString("\n[%1]\n%2").arg(title).arg(message);
    appendLog(logText);

    // 2. 正常的右上角弹窗逻辑
    if (!m_notificationContainer) return;

    NotificationWidget* toast = new NotificationWidget(title, message, m_notificationContainer);
    m_notificationLayout->insertWidget(0, toast);

    toast->raise();
    m_notificationContainer->raise();

    QTimer::singleShot(6000, toast, [toast](){
        if (toast) {
            emit toast->closed();
            toast->deleteLater();
        }
    });
}

// 1. 新增弹窗处理逻辑
void MainWindow::onUdpConfigClicked() {
    UdpConfigDialog dlg(m_udpBindAddress, m_udpListenPort, m_udpRemoteAddress, m_udpRemotePort, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_udpBindAddress = dlg.getLocIp();
        m_udpListenPort = dlg.getLocPort();
        m_udpRemoteAddress = dlg.getRemIp();
        m_udpRemotePort = dlg.getRemPort();

        appendLog(QString(">> [网络配置] 更新完成 | 监听: %1:%2 | 发令向: %3:%4\n")
                  .arg(m_udpBindAddress).arg(m_udpListenPort)
                  .arg(m_udpRemoteAddress).arg(m_udpRemotePort));

        if (m_chkUdpMode->isChecked()) {
            m_lblSysInfo->setText(QString("状态: 就绪\n模式: UDP网络直连\n侦听:%1\n控端:%2")
                                  .arg(m_udpListenPort).arg(m_udpRemoteAddress));
        }
    }
}


// ==========================================
// 【新增功能】：原生窗口多屏弹出机制
// ==========================================
void MainWindow::popOutCurrentTab() {
    int index = m_mainTabWidget->currentIndex();
    if (index < 0) return;

    QWidget* tabContent = m_mainTabWidget->widget(index);
    QString tabName = m_mainTabWidget->tabText(index);

    // 1. 从主界面剥离
    m_mainTabWidget->removeTab(index);

    // 2. 【修改】：使用自定义的无边框独立窗口类
    QString titleStr = tabName + " - 独立多屏显示 (关闭此窗口即可自动还原至主界面)";
    FramelessPopupWindow* popupWindow = new FramelessPopupWindow(tabContent, titleStr);
    popupWindow->setProperty("isTabPopup", true);
    popupWindow->setProperty("tabName", tabName);
    popupWindow->setProperty("tabIndex", index);
    popupWindow->setMinimumSize(1200, 800);

    // 完美继承主界面的所有表格、按钮暗黑样式，并加上深色边缘线条
    popupWindow->setStyleSheet(this->styleSheet() + " #framelessPopup { background-color: #121212; border: 1px solid #333333; }");

    // 强制唤醒被 QTabWidget 自动隐藏的内部部件
    tabContent->show();
    m_popupTabs.insert(popupWindow, tabContent);
    popupWindow->installEventFilter(this); // 拦截关闭事件用于恢复
    popupWindow->setAttribute(Qt::WA_DeleteOnClose);

    popupWindow->setAttribute(Qt::WA_DeleteOnClose);

    // 3. 【核心修复 4】：不再强行锁死全屏状态，而是以 1200x800 的绝佳比例居中弹出
    popupWindow->showCentered(1200, 800);

    appendLog(QString(">> 已将模块 [%1] 剥离并弹出为独立全屏窗口。\n").arg(tabName));
}

void MainWindow::restoreTab(QWidget* popupWindow) {
    if (!m_popupTabs.contains(popupWindow)) return;

    QWidget* tabContent = m_popupTabs.take(popupWindow);
    QString tabName = popupWindow->property("tabName").toString();

    // 取出该 Tab 与生俱来的“绝对编号”
    int absIndex = tabContent->property("absoluteIndex").toInt();

    // 1. 将模块物归原主
    tabContent->setParent(m_mainTabWidget);

    // 2. 【核心修复 3：保证 Tab 顺序不乱】
    // 遍历当前主界面上还剩下的所有 Tab，找到第一个绝对编号比自己大的位置，插在它前面
    int insertPos = m_mainTabWidget->count();
    for (int i = 0; i < m_mainTabWidget->count(); ++i) {
        QWidget* existingTab = m_mainTabWidget->widget(i);
        int existingAbsIndex = existingTab->property("absoluteIndex").toInt();
        if (existingAbsIndex > absIndex) {
            insertPos = i;
            break;
        }
    }

    m_mainTabWidget->insertTab(insertPos, tabContent, tabName);
    m_mainTabWidget->setCurrentWidget(tabContent);
    appendLog(QString(">> 模块 [%1] 已成功吸附还原至主窗口。\n").arg(tabName));
}


void MainWindow::fixAllPlotTitles() {
    // 自动扫描界面上所有的图表，把它们的标题颜色强行刷成亮灰色
    QList<QCustomPlot*> allPlots = this->findChildren<QCustomPlot*>();
    for (QCustomPlot* plot : allPlots) {
        if (plot->plotLayout()->rowCount() > 0 && plot->plotLayout()->columnCount() > 0) {
            if (auto* title = qobject_cast<QCPTextElement*>(plot->plotLayout()->element(0, 0))) {
                title->setTextColor(QColor("#dcdde1"));
            }
        }
    }
}

// ==========================================
// 【终极方案】：无边框窗口全向边缘缩放与拖拽引擎
// ==========================================
MainWindow::ResizeDir MainWindow::getResizeDirection(const QPoint &pos) {
    int padding = 6; // 边缘判定灵敏度
    int w = this->width();
    int h = this->height();
    int x = pos.x();
    int y = pos.y();

    // 【关键修复 1】：必须严格限定在窗口内部，防止鼠标移到外部窗口也乱变形
    if (x < 0 || x > w || y < 0 || y > h) {
        return NoResize;
    }

    bool left = x < padding;
    bool right = x > w - padding;
    bool top = y < padding;
    bool bottom = y > h - padding;

    if (left && top) return TopLeft;
    if (right && top) return TopRight;
    if (left && bottom) return BottomLeft;
    if (right && bottom) return BottomRight;
    if (left) return Left;
    if (right) return Right;
    if (top) return Top;
    if (bottom) return Bottom;
    return NoResize;
}


void MainWindow::updateCursorShape(const QPoint &pos) {
    // 全屏模式下不允许缩放
    if (this->isMaximized() || this->property("isCustomMax").toBool()) {
        this->unsetCursor(); // 【关键修复 2】：使用 unsetCursor 还原，绝对不能覆盖内部按钮的手型指针！
        return;
    }

    // 根据所在的边缘位置，切换成不同的系统伸缩箭头
    switch (getResizeDirection(pos)) {
    case TopLeft:
    case BottomRight: this->setCursor(Qt::SizeFDiagCursor); break;
    case TopRight:
    case BottomLeft:  this->setCursor(Qt::SizeBDiagCursor); break;
    case Left:
    case Right:       this->setCursor(Qt::SizeHorCursor); break;
    case Top:
    case Bottom:      this->setCursor(Qt::SizeVerCursor); break;
    default:          this->unsetCursor(); break; // 【关键修复 2】：同上，离开边缘时释放控制权
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !this->property("isCustomMax").toBool()) {
        // 1. 优先判断是否按在了边缘（准备缩放）
        m_resizeDir = getResizeDirection(event->pos());
        if (m_resizeDir != NoResize) {
            m_isResizing = true;
            m_resizeStartPos = event->globalPos();
            m_resizeStartGeometry = this->geometry();
            event->accept();
            return;
        }

        // 2. 坐标系转换：判断是否按在了标题栏（准备拖拽移动）
        QWidget* central = centralWidget();
        if (central && m_customTitleBar) {
            // 【关键修复 2】：将 MainWindow 的全局坐标转换为 centralWidget 的局部坐标，消除 Toolbar 带来的误差
            QPoint posInCentral = central->mapFrom(this, event->pos());

            if (m_customTitleBar->geometry().contains(posInCentral)) {
                // 获取鼠标到底点在了标题栏的哪个零件上
                QWidget* clickedWidget = central->childAt(posInCentral);

                // 如果点到的是系统按钮 (QPushButton)，绝对不拦截，放行给按钮触发原生点击！
                if (qobject_cast<QPushButton*>(clickedWidget)) {
                    QMainWindow::mousePressEvent(event);
                    return;
                }

                // 如果点的确实是标题栏空白处，则开始拖拽
                m_isDragging = true;
                m_dragPosition = event->globalPos() - frameGeometry().topLeft();
                event->accept();
                return;
            }
        }
    }
    QMainWindow::mousePressEvent(event);
}


void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    // 1. 鼠标仅仅悬浮移动时，动态更新指针形状
    if (event->buttons() == Qt::NoButton) {
        updateCursorShape(event->pos());
    }
    // 2. 处理 8 向边缘拉伸
    else if ((event->buttons() & Qt::LeftButton) && m_isResizing) {
        QPoint diff = event->globalPos() - m_resizeStartPos;
        QRect rect = m_resizeStartGeometry;

        switch (m_resizeDir) {
        case Left:        rect.setLeft(rect.left() + diff.x()); break;
        case Right:       rect.setRight(rect.right() + diff.x()); break;
        case Top:         rect.setTop(rect.top() + diff.y()); break;
        case Bottom:      rect.setBottom(rect.bottom() + diff.y()); break;
        case TopLeft:     rect.setTopLeft(rect.topLeft() + diff); break;
        case TopRight:    rect.setTopRight(rect.topRight() + diff); break;
        case BottomLeft:  rect.setBottomLeft(rect.bottomLeft() + diff); break;
        case BottomRight: rect.setBottomRight(rect.bottomRight() + diff); break;
        default: break;
        }

        // 绝对安全锁：防止被过度压缩导致布局引擎崩溃
        if (rect.width() >= 800 && rect.height() >= 600) {
            this->setGeometry(rect);
        }
    }
    // 3. 处理窗口移动拖拽
    else if ((event->buttons() & Qt::LeftButton) && m_isDragging) {
        this->move(event->globalPos() - m_dragPosition);
    }
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
    // 鼠标松开，重置所有行为状态
    m_isDragging = false;
    m_isResizing = false;
    m_resizeDir = NoResize;
    updateCursorShape(event->pos());
    QMainWindow::mouseReleaseEvent(event);
}

//#include "MainWindow.moc"
