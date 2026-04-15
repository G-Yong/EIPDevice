#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QTimer>
#include <QGroupBox>

class EipClient;
class EdsParser;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // Connection
    void onBtnDiscover();
    void onBtnConnect();
    void onBtnDisconnect();
    void onConnected();
    void onDisconnected();
    void onConnectionError(const QString &err);
    void onDeviceDiscovered(const struct EipDeviceInfo &dev);
    void onDiscoverFinished();
    void onDeviceTableDoubleClicked(int row, int col);

    // EDS
    void onBtnLoadEds();

    // Identity
    void onBtnReadIdentity();

    // Assembly
    void onBtnReadAssembly();
    void onBtnWriteAssembly();
    void onPollingToggled(bool checked);
    void onPollTimer();

    // I/O
    void onBtnForwardOpen();
    void onBtnForwardClose();
    void onBtnStartIO();
    void onBtnStopIO();
    void onBtnSendOutputData();
    void onIODataReceived(const QByteArray &data);
    void onIORefreshTimer();

    // Log
    void appendLog(const QString &msg);

private:
    void createUI();
    QWidget* createConnectionBar();
    QWidget* createDiscoverTab();
    QWidget* createIdentityTab();
    QWidget* createAssemblyTab();
    QWidget* createIOTab();
    void updateConnectionStatus(bool connected);
    void applyEdsConfig();
    void validateAndApplyEds();

    EipClient *m_client = nullptr;

    // Connection bar
    QLineEdit   *m_editIp       = nullptr;
    QPushButton *m_btnDiscover   = nullptr;
    QPushButton *m_btnConnect    = nullptr;
    QPushButton *m_btnDisconnect = nullptr;
    QLabel      *m_lblStatus     = nullptr;
    QPushButton *m_btnLoadEds    = nullptr;
    QLabel      *m_lblEdsStatus  = nullptr;

    // Discover tab
    QTableWidget *m_deviceTable = nullptr;

    // Identity tab
    QLabel *m_lblVendorId    = nullptr;
    QLabel *m_lblDeviceType  = nullptr;
    QLabel *m_lblProductCode = nullptr;
    QLabel *m_lblRevision    = nullptr;
    QLabel *m_lblSerialNum   = nullptr;
    QLabel *m_lblProductName = nullptr;
    QLabel *m_lblDevStatus   = nullptr;
    QLabel *m_lblRawData     = nullptr;

    // Assembly tab
    QSpinBox   *m_spinReadInst   = nullptr;
    QTextEdit  *m_txtReadResult  = nullptr;
    QSpinBox   *m_spinWriteInst  = nullptr;
    QTextEdit  *m_txtWriteData   = nullptr;
    QCheckBox  *m_chkPolling     = nullptr;
    QSpinBox   *m_spinPollMs     = nullptr;
    QTimer     *m_pollTimer      = nullptr;

    // I/O tab
    QSpinBox   *m_spinInputAsm   = nullptr;
    QSpinBox   *m_spinOutputAsm  = nullptr;
    QSpinBox   *m_spinConfigAsm  = nullptr;
    QSpinBox   *m_spinInputSize  = nullptr;
    QSpinBox   *m_spinOutputSize = nullptr;
    QSpinBox   *m_spinRpiMs      = nullptr;
    QPushButton*m_btnForwardOpen = nullptr;
    QPushButton*m_btnForwardClose= nullptr;
    QPushButton*m_btnStartIO     = nullptr;
    QPushButton*m_btnStopIO      = nullptr;
    QTextEdit  *m_txtInputData   = nullptr;
    QTextEdit  *m_txtOutputData  = nullptr;
    QTimer     *m_ioRefreshTimer = nullptr;

    // Log
    QTextEdit  *m_txtLog = nullptr;

    // Tab widget
    QTabWidget *m_tabWidget = nullptr;

    // EDS
    EdsParser  *m_edsParser = nullptr;
};

#endif // MAINWINDOW_H
