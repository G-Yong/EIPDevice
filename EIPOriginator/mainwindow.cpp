/*******************************************************************************
 * mainwindow.cpp - EIP Scanner Qt GUI Implementation
 ******************************************************************************/

#include "mainwindow.h"
#include "eipclient.h"
#include "edsparser.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QMessageBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_client = new EipClient(this);

    connect(m_client, &EipClient::connected, this, &MainWindow::onConnected);
    connect(m_client, &EipClient::disconnected, this, &MainWindow::onDisconnected);
    connect(m_client, &EipClient::connectionError, this, &MainWindow::onConnectionError);
    connect(m_client, &EipClient::deviceDiscovered, this, &MainWindow::onDeviceDiscovered);
    connect(m_client, &EipClient::discoverFinished, this, &MainWindow::onDiscoverFinished);
    connect(m_client, &EipClient::ioDataReceived, this, &MainWindow::onIODataReceived);
    connect(m_client, &EipClient::ioError, this, [this](const QString &err){
        appendLog(QStringLiteral("[IO Error] %1").arg(err));
    });
    connect(m_client, &EipClient::logMessage, this, &MainWindow::appendLog);

    createUI();
    updateConnectionStatus(false);
    setWindowTitle(QStringLiteral("EtherNet/IP Scanner - EIP 主站测试工具"));
    resize(960, 720);
}

MainWindow::~MainWindow()
{
    if (m_pollTimer) m_pollTimer->stop();
    if (m_ioRefreshTimer) m_ioRefreshTimer->stop();
}

/* ============================================================
 * UI Creation
 * ============================================================ */
void MainWindow::createUI()
{
    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);

    // Connection bar
    mainLayout->addWidget(createConnectionBar());

    // Splitter: tabs + log
    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(createDiscoverTab(),  QStringLiteral("设备发现"));
    m_tabWidget->addTab(createIdentityTab(),  QStringLiteral("设备信息"));
    m_tabWidget->addTab(createAssemblyTab(),  QStringLiteral("Assembly 读写"));
    m_tabWidget->addTab(createIOTab(),        QStringLiteral("I/O 连接"));
    splitter->addWidget(m_tabWidget);

    // Log
    auto *logGroup = new QGroupBox(QStringLiteral("日志"), this);
    auto *logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(4, 4, 4, 4);
    m_txtLog = new QTextEdit(this);
    m_txtLog->setReadOnly(true);
    m_txtLog->setFont(QFont(QStringLiteral("Consolas"), 9));
    logLayout->addWidget(m_txtLog);
    splitter->addWidget(logGroup);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter);
}

QWidget* MainWindow::createConnectionBar()
{
    auto *group = new QGroupBox(QStringLiteral("连接"), this);
    auto *layout = new QHBoxLayout(group);
    layout->setContentsMargins(6, 4, 6, 4);

    layout->addWidget(new QLabel(QStringLiteral("目标 IP:"), this));
    m_editIp = new QLineEdit(QStringLiteral("192.168.0.5"), this);
    m_editIp->setFixedWidth(150);
    layout->addWidget(m_editIp);

    m_btnDiscover = new QPushButton(QStringLiteral("发现设备"), this);
    connect(m_btnDiscover, &QPushButton::clicked, this, &MainWindow::onBtnDiscover);
    layout->addWidget(m_btnDiscover);

    m_btnConnect = new QPushButton(QStringLiteral("连接"), this);
    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onBtnConnect);
    layout->addWidget(m_btnConnect);

    m_btnDisconnect = new QPushButton(QStringLiteral("断开"), this);
    m_btnDisconnect->setEnabled(false);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &MainWindow::onBtnDisconnect);
    layout->addWidget(m_btnDisconnect);

    m_btnLoadEds = new QPushButton(QStringLiteral("加载 EDS"), this);
    connect(m_btnLoadEds, &QPushButton::clicked, this, &MainWindow::onBtnLoadEds);
    layout->addWidget(m_btnLoadEds);

    m_lblStatus = new QLabel(this);
    layout->addWidget(m_lblStatus);

    m_lblEdsStatus = new QLabel(this);
    m_lblEdsStatus->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(m_lblEdsStatus);

    layout->addStretch();
    return group;
}

QWidget* MainWindow::createDiscoverTab()
{
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);

    auto *ctrlLayout = new QHBoxLayout;
    ctrlLayout->addWidget(new QLabel(QStringLiteral("发现到的 EIP 设备 (双击选择):"), this));
    ctrlLayout->addStretch();
    layout->addLayout(ctrlLayout);

    m_deviceTable = new QTableWidget(0, 7, this);
    m_deviceTable->setHorizontalHeaderLabels({
        QStringLiteral("IP 地址"),
        QStringLiteral("产品名称"),
        QStringLiteral("厂商 ID"),
        QStringLiteral("设备类型"),
        QStringLiteral("产品代码"),
        QStringLiteral("版本"),
        QStringLiteral("序列号")
    });
    m_deviceTable->horizontalHeader()->setStretchLastSection(true);
    m_deviceTable->setSelectionBehavior(QTableWidget::SelectRows);
    m_deviceTable->setEditTriggers(QTableWidget::NoEditTriggers);
    connect(m_deviceTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::onDeviceTableDoubleClicked);
    layout->addWidget(m_deviceTable);

    return w;
}

QWidget* MainWindow::createIdentityTab()
{
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);

    auto *btnRead = new QPushButton(QStringLiteral("读取 Identity"), this);
    connect(btnRead, &QPushButton::clicked, this, &MainWindow::onBtnReadIdentity);
    layout->addWidget(btnRead, 0, Qt::AlignLeft);

    auto *form = new QFormLayout;
    m_lblVendorId    = new QLabel(QStringLiteral("--"), this);
    m_lblDeviceType  = new QLabel(QStringLiteral("--"), this);
    m_lblProductCode = new QLabel(QStringLiteral("--"), this);
    m_lblRevision    = new QLabel(QStringLiteral("--"), this);
    m_lblDevStatus   = new QLabel(QStringLiteral("--"), this);
    m_lblSerialNum   = new QLabel(QStringLiteral("--"), this);
    m_lblProductName = new QLabel(QStringLiteral("--"), this);
    m_lblRawData     = new QLabel(QStringLiteral("--"), this);
    m_lblRawData->setWordWrap(true);

    QFont boldFont;
    boldFont.setBold(true);
    for (auto *lbl : {m_lblVendorId, m_lblDeviceType, m_lblProductCode,
                      m_lblRevision, m_lblDevStatus, m_lblSerialNum, m_lblProductName}) {
        lbl->setFont(boldFont);
    }

    form->addRow(QStringLiteral("厂商 ID (Vendor ID):"),    m_lblVendorId);
    form->addRow(QStringLiteral("设备类型 (Device Type):"),  m_lblDeviceType);
    form->addRow(QStringLiteral("产品代码 (Product Code):"), m_lblProductCode);
    form->addRow(QStringLiteral("版本 (Revision):"),         m_lblRevision);
    form->addRow(QStringLiteral("状态 (Status):"),           m_lblDevStatus);
    form->addRow(QStringLiteral("序列号 (Serial Number):"),  m_lblSerialNum);
    form->addRow(QStringLiteral("产品名称 (Product Name):"), m_lblProductName);
    form->addRow(QStringLiteral("原始数据 (Hex):"),          m_lblRawData);
    layout->addLayout(form);

    layout->addStretch();
    return w;
}

QWidget* MainWindow::createAssemblyTab()
{
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);

    // Read section
    auto *readGroup = new QGroupBox(QStringLiteral("读取 Assembly"), this);
    auto *readLayout = new QVBoxLayout(readGroup);

    auto *readCtrl = new QHBoxLayout;
    readCtrl->addWidget(new QLabel(QStringLiteral("Instance:"), this));
    m_spinReadInst = new QSpinBox(this);
    m_spinReadInst->setRange(1, 65535);
    m_spinReadInst->setValue(100);
    readCtrl->addWidget(m_spinReadInst);

    auto *btnRead = new QPushButton(QStringLiteral("读取"), this);
    connect(btnRead, &QPushButton::clicked, this, &MainWindow::onBtnReadAssembly);
    readCtrl->addWidget(btnRead);

    // Preset buttons
    auto *btnPreset100 = new QPushButton(QStringLiteral("Input(100)"), this);
    connect(btnPreset100, &QPushButton::clicked, this, [this]{ m_spinReadInst->setValue(100); onBtnReadAssembly(); });
    readCtrl->addWidget(btnPreset100);
    auto *btnPreset150 = new QPushButton(QStringLiteral("Output(150)"), this);
    connect(btnPreset150, &QPushButton::clicked, this, [this]{ m_spinReadInst->setValue(150); onBtnReadAssembly(); });
    readCtrl->addWidget(btnPreset150);

    m_chkPolling = new QCheckBox(QStringLiteral("自动轮询"), this);
    connect(m_chkPolling, &QCheckBox::toggled, this, &MainWindow::onPollingToggled);
    readCtrl->addWidget(m_chkPolling);

    readCtrl->addWidget(new QLabel(QStringLiteral("间隔(ms):"), this));
    m_spinPollMs = new QSpinBox(this);
    m_spinPollMs->setRange(100, 10000);
    m_spinPollMs->setValue(500);
    readCtrl->addWidget(m_spinPollMs);
    readCtrl->addStretch();
    readLayout->addLayout(readCtrl);

    m_txtReadResult = new QTextEdit(this);
    m_txtReadResult->setReadOnly(true);
    m_txtReadResult->setFont(QFont(QStringLiteral("Consolas"), 10));
    m_txtReadResult->setMaximumHeight(120);
    readLayout->addWidget(m_txtReadResult);

    layout->addWidget(readGroup);

    // Write section
    auto *writeGroup = new QGroupBox(QStringLiteral("写入 Assembly"), this);
    auto *writeLayout = new QVBoxLayout(writeGroup);

    auto *writeCtrl = new QHBoxLayout;
    writeCtrl->addWidget(new QLabel(QStringLiteral("Instance:"), this));
    m_spinWriteInst = new QSpinBox(this);
    m_spinWriteInst->setRange(1, 65535);
    m_spinWriteInst->setValue(150);
    writeCtrl->addWidget(m_spinWriteInst);

    auto *btnWrite = new QPushButton(QStringLiteral("写入"), this);
    connect(btnWrite, &QPushButton::clicked, this, &MainWindow::onBtnWriteAssembly);
    writeCtrl->addWidget(btnWrite);
    writeCtrl->addStretch();
    writeLayout->addLayout(writeCtrl);

    writeLayout->addWidget(new QLabel(QStringLiteral("数据 (16进制, 空格分隔, 如: 01 02 03 FF):"), this));
    m_txtWriteData = new QTextEdit(this);
    m_txtWriteData->setFont(QFont(QStringLiteral("Consolas"), 10));
    m_txtWriteData->setMaximumHeight(80);
    m_txtWriteData->setPlainText(QStringLiteral("00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"));
    writeLayout->addWidget(m_txtWriteData);

    layout->addWidget(writeGroup);
    layout->addStretch();

    // Poll timer
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPollTimer);

    return w;
}

QWidget* MainWindow::createIOTab()
{
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);

    // Parameters
    auto *paramGroup = new QGroupBox(QStringLiteral("I/O 连接参数"), this);
    auto *paramLayout = new QHBoxLayout(paramGroup);

    auto *form1 = new QFormLayout;
    m_spinInputAsm = new QSpinBox(this);
    m_spinInputAsm->setRange(1, 65535); m_spinInputAsm->setValue(100);
    form1->addRow(QStringLiteral("Input Assembly:"), m_spinInputAsm);

    m_spinOutputAsm = new QSpinBox(this);
    m_spinOutputAsm->setRange(1, 65535); m_spinOutputAsm->setValue(150);
    form1->addRow(QStringLiteral("Output Assembly:"), m_spinOutputAsm);

    m_spinConfigAsm = new QSpinBox(this);
    m_spinConfigAsm->setRange(1, 65535); m_spinConfigAsm->setValue(151);
    form1->addRow(QStringLiteral("Config Assembly:"), m_spinConfigAsm);
    paramLayout->addLayout(form1);

    auto *form2 = new QFormLayout;
    m_spinInputSize = new QSpinBox(this);
    m_spinInputSize->setRange(0, 1024); m_spinInputSize->setValue(32);
    form2->addRow(QStringLiteral("Input Size (bytes):"), m_spinInputSize);

    m_spinOutputSize = new QSpinBox(this);
    m_spinOutputSize->setRange(0, 1024); m_spinOutputSize->setValue(32);
    form2->addRow(QStringLiteral("Output Size (bytes):"), m_spinOutputSize);

    m_spinRpiMs = new QSpinBox(this);
    m_spinRpiMs->setRange(10, 10000); m_spinRpiMs->setValue(100);
    form2->addRow(QStringLiteral("RPI (ms):"), m_spinRpiMs);
    paramLayout->addLayout(form2);

    layout->addWidget(paramGroup);

    // Control buttons
    auto *ctrlLayout = new QHBoxLayout;
    m_btnForwardOpen = new QPushButton(QStringLiteral("Forward Open"), this);
    connect(m_btnForwardOpen, &QPushButton::clicked, this, &MainWindow::onBtnForwardOpen);
    ctrlLayout->addWidget(m_btnForwardOpen);

    m_btnStartIO = new QPushButton(QStringLiteral("开始 I/O"), this);
    m_btnStartIO->setEnabled(false);
    connect(m_btnStartIO, &QPushButton::clicked, this, &MainWindow::onBtnStartIO);
    ctrlLayout->addWidget(m_btnStartIO);

    m_btnStopIO = new QPushButton(QStringLiteral("停止 I/O"), this);
    m_btnStopIO->setEnabled(false);
    connect(m_btnStopIO, &QPushButton::clicked, this, &MainWindow::onBtnStopIO);
    ctrlLayout->addWidget(m_btnStopIO);

    m_btnForwardClose = new QPushButton(QStringLiteral("Forward Close"), this);
    m_btnForwardClose->setEnabled(false);
    connect(m_btnForwardClose, &QPushButton::clicked, this, &MainWindow::onBtnForwardClose);
    ctrlLayout->addWidget(m_btnForwardClose);
    ctrlLayout->addStretch();
    layout->addLayout(ctrlLayout);

    // I/O data display
    auto *dataSplitter = new QSplitter(Qt::Horizontal, this);

    // Input data (T->O, from device)
    auto *inputGroup = new QGroupBox(QStringLiteral("输入数据 T→O (从站 → 主站)"), this);
    auto *inputLayout = new QVBoxLayout(inputGroup);
    m_txtInputData = new QTextEdit(this);
    m_txtInputData->setReadOnly(true);
    m_txtInputData->setFont(QFont(QStringLiteral("Consolas"), 10));
    inputLayout->addWidget(m_txtInputData);
    dataSplitter->addWidget(inputGroup);

    // Output data (O->T, to device)
    auto *outputGroup = new QGroupBox(QStringLiteral("输出数据 O→T (主站 → 从站)"), this);
    auto *outputLayout = new QVBoxLayout(outputGroup);
    m_txtOutputData = new QTextEdit(this);
    m_txtOutputData->setFont(QFont(QStringLiteral("Consolas"), 10));
    m_txtOutputData->setPlainText(QStringLiteral("00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
                                                  "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"));
    outputLayout->addWidget(m_txtOutputData);

    auto *btnSendOutput = new QPushButton(QStringLiteral("发送输出数据"), this);
    connect(btnSendOutput, &QPushButton::clicked, this, &MainWindow::onBtnSendOutputData);
    outputLayout->addWidget(btnSendOutput, 0, Qt::AlignRight);
    dataSplitter->addWidget(outputGroup);

    layout->addWidget(dataSplitter);

    // I/O refresh timer
    m_ioRefreshTimer = new QTimer(this);
    connect(m_ioRefreshTimer, &QTimer::timeout, this, &MainWindow::onIORefreshTimer);

    return w;
}

/* ============================================================
 * Connection Slots
 * ============================================================ */
void MainWindow::onBtnDiscover()
{
    m_deviceTable->setRowCount(0);
    m_client->discover();
    m_btnDiscover->setEnabled(false);
    appendLog(QStringLiteral("正在扫描 EIP 设备..."));
}

void MainWindow::onBtnConnect()
{
    QString ip = m_editIp->text().trimmed();
    if (ip.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请输入目标 IP 地址"));
        return;
    }
    m_btnConnect->setEnabled(false);
    m_client->connectToDevice(ip);
}

void MainWindow::onBtnDisconnect()
{
    if (m_client->isIOActive()) {
        m_client->stopIO();
        m_client->forwardClose();
    }
    m_client->disconnectFromDevice();
}

void MainWindow::onConnected()
{
    updateConnectionStatus(true);
    appendLog(QStringLiteral("已连接到 %1").arg(m_editIp->text()));

    // 如果已加载 EDS, 自动验证
    if (m_edsParser)
        validateAndApplyEds();
}

void MainWindow::onDisconnected()
{
    updateConnectionStatus(false);
}

void MainWindow::onConnectionError(const QString &err)
{
    updateConnectionStatus(false);
    appendLog(QStringLiteral("[Error] %1").arg(err));
    QMessageBox::warning(this, QStringLiteral("连接错误"), err);
}

void MainWindow::onDeviceDiscovered(const EipDeviceInfo &dev)
{
    int row = m_deviceTable->rowCount();
    m_deviceTable->insertRow(row);
    m_deviceTable->setItem(row, 0, new QTableWidgetItem(dev.ip));
    m_deviceTable->setItem(row, 1, new QTableWidgetItem(dev.productName));
    m_deviceTable->setItem(row, 2, new QTableWidgetItem(QString::number(dev.vendorId)));
    m_deviceTable->setItem(row, 3, new QTableWidgetItem(QString::number(dev.deviceType)));
    m_deviceTable->setItem(row, 4, new QTableWidgetItem(QString::number(dev.productCode)));
    m_deviceTable->setItem(row, 5, new QTableWidgetItem(
        QStringLiteral("%1.%2").arg(dev.revisionMajor).arg(dev.revisionMinor)));
    m_deviceTable->setItem(row, 6, new QTableWidgetItem(
        QStringLiteral("0x%1").arg(dev.serialNumber, 8, 16, QLatin1Char('0'))));
    m_deviceTable->resizeColumnsToContents();
}

void MainWindow::onDiscoverFinished()
{
    m_btnDiscover->setEnabled(true);
    appendLog(QStringLiteral("设备扫描完成，找到 %1 台设备").arg(m_deviceTable->rowCount()));
}

void MainWindow::onDeviceTableDoubleClicked(int row, int /*col*/)
{
    auto *item = m_deviceTable->item(row, 0);
    if (item) {
        m_editIp->setText(item->text());
        appendLog(QStringLiteral("已选择设备: %1").arg(item->text()));
    }
}

/* ============================================================
 * Identity Slots
 * ============================================================ */
void MainWindow::onBtnReadIdentity()
{
    if (!m_client->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请先连接到设备"));
        return;
    }

    EipDeviceInfo info = m_client->readIdentity();
    m_lblVendorId->setText(QStringLiteral("%1 (0x%2)")
                               .arg(info.vendorId)
                               .arg(info.vendorId, 4, 16, QLatin1Char('0')));
    m_lblDeviceType->setText(QString::number(info.deviceType));
    m_lblProductCode->setText(QString::number(info.productCode));
    m_lblRevision->setText(QStringLiteral("%1.%2").arg(info.revisionMajor).arg(info.revisionMinor));
    m_lblDevStatus->setText(QStringLiteral("0x%1").arg(info.status, 4, 16, QLatin1Char('0')));
    m_lblSerialNum->setText(QStringLiteral("0x%1 (%2)")
                                .arg(info.serialNumber, 8, 16, QLatin1Char('0'))
                                .arg(info.serialNumber));
    m_lblProductName->setText(info.productName);

    // Show raw data
    CipResponse resp = m_client->getAttributeAll(EipConst::ClassIdentity, 1);
    if (resp.success) {
        QString hex;
        for (int i = 0; i < resp.data.size(); ++i) {
            hex += QStringLiteral("%1 ").arg(static_cast<quint8>(resp.data[i]), 2, 16, QLatin1Char('0'));
        }
        m_lblRawData->setText(hex.trimmed().toUpper());
    }

    appendLog(QStringLiteral("Identity 读取成功: %1").arg(info.productName));
}

/* ============================================================
 * Assembly Slots
 * ============================================================ */
void MainWindow::onBtnReadAssembly()
{
    if (!m_client->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请先连接到设备"));
        return;
    }

    quint16 inst = static_cast<quint16>(m_spinReadInst->value());
    CipResponse resp = m_client->readAssembly(inst);

    if (resp.success) {
        QString hex;
        for (int i = 0; i < resp.data.size(); ++i) {
            hex += QStringLiteral("%1 ").arg(static_cast<quint8>(resp.data[i]), 2, 16, QLatin1Char('0'));
            if ((i + 1) % 16 == 0) hex += QStringLiteral("\n");
        }
        m_txtReadResult->setPlainText(QStringLiteral("Assembly %1 (%2 bytes):\n%3")
                                          .arg(inst).arg(resp.data.size()).arg(hex.trimmed().toUpper()));
    } else {
        m_txtReadResult->setPlainText(QStringLiteral("读取失败: %1").arg(resp.errorText));
    }
}

void MainWindow::onBtnWriteAssembly()
{
    if (!m_client->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请先连接到设备"));
        return;
    }

    QString hexStr = m_txtWriteData->toPlainText().simplified();
    QByteArray data;
    const QStringList parts = hexStr.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok;
        int val = part.toInt(&ok, 16);
        if (ok && val >= 0 && val <= 0xFF) {
            data.append(static_cast<char>(val));
        }
    }

    if (data.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("无有效的 16 进制数据"));
        return;
    }

    quint16 inst = static_cast<quint16>(m_spinWriteInst->value());
    CipResponse resp = m_client->writeAssembly(inst, data);

    if (resp.success) {
        appendLog(QStringLiteral("Assembly %1 写入成功 (%2 bytes)").arg(inst).arg(data.size()));
    } else {
        appendLog(QStringLiteral("Assembly %1 写入失败: %2").arg(inst).arg(resp.errorText));
        QMessageBox::warning(this, QStringLiteral("写入失败"), resp.errorText);
    }
}

void MainWindow::onPollingToggled(bool checked)
{
    if (checked) {
        m_pollTimer->start(m_spinPollMs->value());
    } else {
        m_pollTimer->stop();
    }
}

void MainWindow::onPollTimer()
{
    if (m_client->isConnected()) {
        onBtnReadAssembly();
    }
}

/* ============================================================
 * I/O Slots
 * ============================================================ */
void MainWindow::onBtnForwardOpen()
{
    if (!m_client->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请先连接到设备"));
        return;
    }

    bool ok = m_client->forwardOpen(
        static_cast<quint16>(m_spinInputAsm->value()),
        static_cast<quint16>(m_spinOutputAsm->value()),
        static_cast<quint16>(m_spinConfigAsm->value()),
        static_cast<quint16>(m_spinInputSize->value()),
        static_cast<quint16>(m_spinOutputSize->value()),
        static_cast<quint32>(m_spinRpiMs->value()) * 1000
    );

    if (ok) {
        m_btnForwardOpen->setEnabled(false);
        m_btnStartIO->setEnabled(true);
        m_btnForwardClose->setEnabled(true);
        appendLog(QStringLiteral("Forward Open 成功"));
    }
}

void MainWindow::onBtnForwardClose()
{
    if (m_client->isIOActive()) {
        m_client->stopIO();
    }
    m_client->forwardClose();

    m_btnForwardOpen->setEnabled(true);
    m_btnStartIO->setEnabled(false);
    m_btnStopIO->setEnabled(false);
    m_btnForwardClose->setEnabled(false);
    m_ioRefreshTimer->stop();
}

void MainWindow::onBtnStartIO()
{
    m_client->startIO();
    m_btnStartIO->setEnabled(false);
    m_btnStopIO->setEnabled(true);
    m_ioRefreshTimer->start(200);  // refresh display every 200ms
}

void MainWindow::onBtnStopIO()
{
    m_client->stopIO();
    m_btnStartIO->setEnabled(true);
    m_btnStopIO->setEnabled(false);
    m_ioRefreshTimer->stop();
}

void MainWindow::onBtnSendOutputData()
{
    QString hexStr = m_txtOutputData->toPlainText().simplified();
    QByteArray data;
    const QStringList parts = hexStr.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok;
        int val = part.toInt(&ok, 16);
        if (ok && val >= 0 && val <= 0xFF) {
            data.append(static_cast<char>(val));
        }
    }
    m_client->setOutputData(data);
    appendLog(QStringLiteral("输出数据已更新 (%1 bytes)").arg(data.size()));
}

void MainWindow::onIODataReceived(const QByteArray &data)
{
    Q_UNUSED(data)
    // Display is refreshed by m_ioRefreshTimer to avoid UI flooding
}

void MainWindow::onIORefreshTimer()
{
    QByteArray input = m_client->inputData();
    QString hex;
    for (int i = 0; i < input.size(); ++i) {
        hex += QStringLiteral("%1 ").arg(static_cast<quint8>(input[i]), 2, 16, QLatin1Char('0'));
        if ((i + 1) % 16 == 0) hex += QStringLiteral("\n");
    }
    m_txtInputData->setPlainText(hex.trimmed().toUpper());
}

/* ============================================================
 * EDS Slots
 * ============================================================ */
void MainWindow::onBtnLoadEds()
{
    QString filepath = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 EDS 文件"), QString(),
        QStringLiteral("EDS 文件 (*.eds);;所有文件 (*.*)"));
    if (filepath.isEmpty())
        return;

    auto *parser = new EdsParser();
    if (!parser->load(filepath)) {
        QMessageBox::critical(this, QStringLiteral("EDS 加载失败"),
                              QStringLiteral("无法解析 EDS 文件"));
        delete parser;
        return;
    }

    delete m_edsParser;
    m_edsParser = parser;

    QString fname = QFileInfo(filepath).fileName();
    m_lblEdsStatus->setText(QStringLiteral("EDS: %1").arg(fname));
    m_lblEdsStatus->setStyleSheet(QStringLiteral("color: blue;"));

    appendLog(QStringLiteral("EDS 已加载: %1").arg(filepath));
    appendLog(m_edsParser->summary());

    if (m_client->isConnected()) {
        validateAndApplyEds();
    } else {
        if (QMessageBox::question(this, QStringLiteral("提示"),
                QStringLiteral("当前未连接设备, 无法验证 EDS 匹配.\n是否仍然应用 EDS 参数配置?"))
            == QMessageBox::Yes)
        {
            applyEdsConfig();
        }
    }
}

void MainWindow::validateAndApplyEds()
{
    if (!m_edsParser || !m_client->isConnected())
        return;

    EipDeviceInfo info = m_client->readIdentity();
    if (info.vendorId == 0 && info.productCode == 0 && info.serialNumber == 0) {
        appendLog(QStringLiteral("EDS 验证失败: 无法读取设备 Identity"));
        QMessageBox::warning(this, QStringLiteral("验证失败"),
                             QStringLiteral("无法读取设备 Identity"));
        return;
    }

    auto result = m_edsParser->matchDevice(info.vendorId, info.deviceType,
                                           info.productCode, info.revisionMajor);

    QString fname = QFileInfo(m_edsParser->filePath()).fileName();

    if (result.matched) {
        m_lblEdsStatus->setText(QStringLiteral("EDS: %1 ✓ 匹配").arg(fname));
        m_lblEdsStatus->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        appendLog(QStringLiteral("EDS 验证通过: 设备匹配!"));
        applyEdsConfig();
        QMessageBox::information(this, QStringLiteral("EDS 匹配"),
                                 QStringLiteral("EDS 文件与设备匹配!\n已自动配置所有参数."));
    } else {
        m_lblEdsStatus->setText(QStringLiteral("EDS: %1 ✗ 不匹配").arg(fname));
        m_lblEdsStatus->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
        QString mismatchText = result.mismatches.join(QLatin1Char('\n'));
        appendLog(QStringLiteral("EDS 验证失败 - 不匹配:\n%1").arg(mismatchText));
        if (QMessageBox::question(this, QStringLiteral("EDS 不匹配"),
                QStringLiteral("EDS 与设备不匹配:\n%1\n\n是否仍然强制应用参数?").arg(mismatchText))
            == QMessageBox::Yes)
        {
            applyEdsConfig();
        }
    }
}

void MainWindow::applyEdsConfig()
{
    if (!m_edsParser)
        return;

    EdsIoConfig cfg = m_edsParser->getIoConfig();
    QStringList applied;

    // I/O connection params
    if (cfg.inputAssembly >= 0) {
        m_spinInputAsm->setValue(cfg.inputAssembly);
        applied << QStringLiteral("Input Assembly = %1").arg(cfg.inputAssembly);
    }
    if (cfg.outputAssembly >= 0) {
        m_spinOutputAsm->setValue(cfg.outputAssembly);
        applied << QStringLiteral("Output Assembly = %1").arg(cfg.outputAssembly);
    }
    if (cfg.configAssembly >= 0) {
        m_spinConfigAsm->setValue(cfg.configAssembly);
        applied << QStringLiteral("Config Assembly = %1").arg(cfg.configAssembly);
    }
    if (cfg.inputAssembly >= 0) {
        m_spinInputSize->setValue(cfg.inputSize);
        applied << QStringLiteral("Input Size = %1").arg(cfg.inputSize);
    }
    if (cfg.outputAssembly >= 0) {
        m_spinOutputSize->setValue(cfg.outputSize);
        applied << QStringLiteral("Output Size = %1").arg(cfg.outputSize);
    }
    if (cfg.rpiUs > 0) {
        int rpiMs = cfg.rpiUs / 1000;
        if (rpiMs < 1) rpiMs = 1;
        m_spinRpiMs->setValue(rpiMs);
        applied << QStringLiteral("RPI = %1 ms").arg(rpiMs);
    }

    // Assembly read/write page
    if (cfg.inputAssembly >= 0)
        m_spinReadInst->setValue(cfg.inputAssembly);
    if (cfg.outputAssembly >= 0) {
        m_spinWriteInst->setValue(cfg.outputAssembly);
        int outSz = cfg.outputSize > 0 ? cfg.outputSize : 32;
        QStringList hex;
        for (int i = 0; i < outSz; ++i) hex << QStringLiteral("00");
        m_txtWriteData->setPlainText(hex.join(QLatin1Char(' ')));
    }

    if (!applied.isEmpty()) {
        appendLog(QStringLiteral("EDS 自动配置已应用:"));
        for (const QString &item : applied)
            appendLog(QStringLiteral("  %1").arg(item));
    } else {
        appendLog(QStringLiteral("EDS 中未找到可配置的参数"));
    }
}

/* ============================================================
 * Helpers
 * ============================================================ */
void MainWindow::updateConnectionStatus(bool connected)
{
    m_btnConnect->setEnabled(!connected);
    m_btnDisconnect->setEnabled(connected);
    if (connected) {
        m_lblStatus->setText(QStringLiteral("● 已连接"));
        m_lblStatus->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
    } else {
        m_lblStatus->setText(QStringLiteral("● 未连接"));
        m_lblStatus->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));

        // Reset I/O controls
        m_btnForwardOpen->setEnabled(true);
        m_btnStartIO->setEnabled(false);
        m_btnStopIO->setEnabled(false);
        m_btnForwardClose->setEnabled(false);
        if (m_ioRefreshTimer) m_ioRefreshTimer->stop();
    }
}

void MainWindow::appendLog(const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_txtLog->append(QStringLiteral("[%1] %2").arg(timestamp, msg));
}
