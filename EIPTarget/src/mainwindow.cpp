/*******************************************************************************
 * mainwindow.cpp - EIP Target main window implementation
 ******************************************************************************/

#include "mainwindow.h"
#include "eiptargetservice.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QScrollArea>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_service = new EipTargetService(this);

    connect(m_service, &EipTargetService::started,
            this, &MainWindow::onStarted);
    connect(m_service, &EipTargetService::stopped,
            this, &MainWindow::onStopped);
    connect(m_service, &EipTargetService::errorOccurred,
            this, &MainWindow::onError);
    connect(m_service, &EipTargetService::logMessage,
            this, &MainWindow::onLog);
    connect(m_service, &EipTargetService::ioConnectionEvent,
            this, &MainWindow::onIoEvent);
    connect(m_service, &EipTargetService::outputDataReceived,
            this, &MainWindow::onOutputDataReceived);

    setupUi();
    refreshInterfaces();
}

MainWindow::~MainWindow()
{
    m_service->stop();
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("EIP Target (Adapter/Slave)"));
    resize(720, 560);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    /* ---- Interface group ---- */
    QGroupBox *ifaceGroup = new QGroupBox(QStringLiteral("Network Interface"), central);
    QHBoxLayout *ifaceLayout = new QHBoxLayout(ifaceGroup);

    m_ifaceCombo = new QComboBox(ifaceGroup);
    m_ifaceCombo->setMinimumWidth(260);
    m_refreshBtn = new QPushButton(QStringLiteral("Refresh"), ifaceGroup);
    m_startBtn   = new QPushButton(QStringLiteral("Start"), ifaceGroup);
    m_stopBtn    = new QPushButton(QStringLiteral("Stop"), ifaceGroup);
    m_stopBtn->setEnabled(false);
    m_statusLabel = new QLabel(QStringLiteral("Stopped"), ifaceGroup);

    ifaceLayout->addWidget(m_ifaceCombo, 1);
    ifaceLayout->addWidget(m_refreshBtn);
    ifaceLayout->addWidget(m_startBtn);
    ifaceLayout->addWidget(m_stopBtn);
    ifaceLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(ifaceGroup);

    /* ---- I/O Config group ---- */
    QGroupBox *cfgGroup = new QGroupBox(QStringLiteral("I/O Configuration (set before Start)"), central);
    QHBoxLayout *cfgLayout = new QHBoxLayout(cfgGroup);

    cfgLayout->addWidget(new QLabel(QStringLiteral("Input(T\u2192O) Size:"), cfgGroup));
    m_inputSizeSpin = new QSpinBox(cfgGroup);
    m_inputSizeSpin->setRange(1, 512);
    m_inputSizeSpin->setValue(32);
    m_inputSizeSpin->setSuffix(QStringLiteral(" bytes"));
    cfgLayout->addWidget(m_inputSizeSpin);

    cfgLayout->addWidget(new QLabel(QStringLiteral("Output(O\u2192T) Size:"), cfgGroup));
    m_outputSizeSpin = new QSpinBox(cfgGroup);
    m_outputSizeSpin->setRange(1, 512);
    m_outputSizeSpin->setValue(32);
    m_outputSizeSpin->setSuffix(QStringLiteral(" bytes"));
    cfgLayout->addWidget(m_outputSizeSpin);

    cfgLayout->addStretch();
    m_exportEdsBtn = new QPushButton(QStringLiteral("Export EDS"), cfgGroup);
    cfgLayout->addWidget(m_exportEdsBtn);

    mainLayout->addWidget(cfgGroup);

    /* ---- I/O Data group ---- */
    QGroupBox *ioGroup = new QGroupBox(QStringLiteral("I/O Data"), central);
    QVBoxLayout *ioLayout = new QVBoxLayout(ioGroup);

    QFormLayout *inputForm = new QFormLayout;
    m_inputDataEdit = new QLineEdit(ioGroup);
    m_inputDataEdit->setPlaceholderText(
        QStringLiteral("Hex bytes, e.g. 01 02 03 04 ... (max 32 bytes)"));
    m_sendInputBtn = new QPushButton(QStringLiteral("Set Input Data"), ioGroup);
    QHBoxLayout *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_inputDataEdit, 1);
    inputRow->addWidget(m_sendInputBtn);
    inputForm->addRow(QStringLiteral("Input (T->O):"), inputRow);
    ioLayout->addLayout(inputForm);

    m_outputDataView = new QTextEdit(ioGroup);
    m_outputDataView->setReadOnly(true);
    m_outputDataView->setMaximumHeight(100);
    QFormLayout *outputForm = new QFormLayout;
    outputForm->addRow(QStringLiteral("Output (O->T):"), m_outputDataView);
    ioLayout->addLayout(outputForm);

    mainLayout->addWidget(ioGroup);

    /* ---- Log ---- */
    QGroupBox *logGroup = new QGroupBox(QStringLiteral("Log"), central);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    m_logView = new QTextEdit(logGroup);
    m_logView->setReadOnly(true);
    logLayout->addWidget(m_logView);
    mainLayout->addWidget(logGroup, 1);

    /* ---- Connections ---- */
    connect(m_refreshBtn, &QPushButton::clicked,
            this, &MainWindow::onRefreshInterfaces);
    connect(m_startBtn, &QPushButton::clicked,
            this, &MainWindow::onStart);
    connect(m_stopBtn, &QPushButton::clicked,
            this, &MainWindow::onStop);
    connect(m_sendInputBtn, &QPushButton::clicked,
            this, &MainWindow::onSendInputData);
    connect(m_exportEdsBtn, &QPushButton::clicked,
            this, &MainWindow::onExportEds);
}

void MainWindow::refreshInterfaces()
{
    m_ifaceCombo->clear();
    const auto ifaces = EipTargetService::availableInterfaces();
    for (const EipNetworkInterface &ni : ifaces) {
        m_ifaceCombo->addItem(ni.displayLabel, QString::number(ni.index));
    }
}

void MainWindow::onRefreshInterfaces()
{
    refreshInterfaces();
}

void MainWindow::onStart()
{
    if (m_ifaceCombo->currentIndex() < 0) {
        onLog(QStringLiteral("No network interface selected"));
        return;
    }
    int inSz = m_inputSizeSpin->value();
    int outSz = m_outputSizeSpin->value();
    m_service->config().inputSize  = inSz;
    m_service->config().outputSize = outSz;
    m_inputDataEdit->setPlaceholderText(
        QStringLiteral("Hex bytes, e.g. 01 02 03 04 ... (max %1 bytes)").arg(inSz));

    QString ifaceName = m_ifaceCombo->currentData().toString();
    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_inputSizeSpin->setEnabled(false);
    m_outputSizeSpin->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Starting..."));
    onLog(QStringLiteral("Starting EIP Target on interface: %1 (Input=%2B, Output=%3B)")
              .arg(ifaceName).arg(inSz).arg(outSz));
    m_service->start(ifaceName);
}

void MainWindow::onStop()
{
    m_stopBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Stopping..."));
    m_service->stop();
}

void MainWindow::onStarted()
{
    m_statusLabel->setText(QStringLiteral("Running"));
    m_statusLabel->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
    onLog(QStringLiteral("EIP Target is running (TCP:44818, UDP:44818, UDP:2222)"));
}

void MainWindow::onStopped()
{
    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    m_inputSizeSpin->setEnabled(true);
    m_outputSizeSpin->setEnabled(true);
    m_statusLabel->setText(QStringLiteral("Stopped"));
    m_statusLabel->setStyleSheet(QString());
    onLog(QStringLiteral("EIP Target stopped"));
}

void MainWindow::onError(const QString &err)
{
    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Error"));
    m_statusLabel->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
    onLog(QStringLiteral("ERROR: %1").arg(err));
}

void MainWindow::onLog(const QString &msg)
{
    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    m_logView->append(QStringLiteral("[%1] %2").arg(ts, msg));
}

void MainWindow::onIoEvent(unsigned int outAsm, unsigned int inAsm, int event)
{
    QString eventStr;
    switch (event) {
    case 0: eventStr = QStringLiteral("Opened"); break;
    case 1: eventStr = QStringLiteral("Timed out"); break;
    case 2: eventStr = QStringLiteral("Closed"); break;
    default: eventStr = QStringLiteral("Unknown(%1)").arg(event); break;
    }
    onLog(QStringLiteral("I/O Connection %1: Output=%2, Input=%3")
              .arg(eventStr)
              .arg(outAsm)
              .arg(inAsm));
}

void MainWindow::onOutputDataReceived(const QByteArray &data)
{
    QString hex;
    for (int i = 0; i < data.size(); i++) {
        if (i > 0) hex += QLatin1Char(' ');
        hex += QStringLiteral("%1").arg(static_cast<quint8>(data[i]), 2, 16,
                                        QLatin1Char('0'));
    }
    m_outputDataView->setText(hex.toUpper());
    onLog(QStringLiteral("Output data received: %1").arg(hex.toUpper()));
}

void MainWindow::onSendInputData()
{
    QString text = m_inputDataEdit->text().trimmed();
    if (text.isEmpty()) return;

    QStringList parts = text.split(QRegExp(QStringLiteral("[\\s,]+")),
                                   QString::SkipEmptyParts);
    QByteArray data;
    for (const QString &p : parts) {
        bool ok = false;
        int val = p.toInt(&ok, 16);
        if (!ok || val < 0 || val > 255) {
            onLog(QStringLiteral("Invalid hex byte: %1").arg(p));
            return;
        }
        data.append(static_cast<char>(val));
    }
    int maxSz = m_service->config().inputSize;
    if (data.size() > maxSz) {
        onLog(QStringLiteral("Input data truncated to %1 bytes").arg(maxSz));
        data = data.left(maxSz);
    }

    m_service->setInputData(data);
    onLog(QStringLiteral("Input data set (%1 bytes)").arg(data.size()));
}

void MainWindow::onExportEds()
{
    int inSz = m_inputSizeSpin->value();
    int outSz = m_outputSizeSpin->value();

    QString fileName = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export EDS File"),
        QStringLiteral("QtEipTarget.eds"),
        QStringLiteral("EDS Files (*.eds);;All Files (*)"));
    if (fileName.isEmpty()) return;

    QString eds = EipTargetService::generateEds(inSz, outSz);

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        onLog(QStringLiteral("Failed to write EDS file: %1").arg(file.errorString()));
        return;
    }
    file.write(eds.toUtf8());
    file.close();

    onLog(QStringLiteral("EDS file exported: %1 (Input=%2B, Output=%3B)")
              .arg(fileName).arg(inSz).arg(outSz));
}
