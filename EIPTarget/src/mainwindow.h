/*******************************************************************************
 * mainwindow.h - EIP Target (Adapter/Slave) main window
 ******************************************************************************/
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTableWidget>
#include <QLabel>
#include <QGroupBox>
#include <QSpinBox>

class EipTargetService;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStart();
    void onStop();
    void onStarted();
    void onStopped();
    void onError(const QString &err);
    void onLog(const QString &msg);
    void onIoEvent(unsigned int outAsm, unsigned int inAsm, int event);
    void onOutputDataReceived(const QByteArray &data);
    void onSendInputData();
    void onRefreshInterfaces();
    void onExportEds();

private:
    void setupUi();
    void refreshInterfaces();

    EipTargetService *m_service = nullptr;

    // Controls
    QComboBox   *m_ifaceCombo    = nullptr;
    QPushButton *m_refreshBtn    = nullptr;
    QPushButton *m_startBtn      = nullptr;
    QPushButton *m_stopBtn       = nullptr;
    QLabel      *m_statusLabel   = nullptr;

    // I/O data display
    QLineEdit   *m_inputDataEdit = nullptr;   // hex string, user editable
    QPushButton *m_sendInputBtn  = nullptr;
    QTextEdit   *m_outputDataView= nullptr;   // received output data

    // I/O size config
    QSpinBox    *m_inputSizeSpin = nullptr;
    QSpinBox    *m_outputSizeSpin = nullptr;
    QPushButton *m_exportEdsBtn  = nullptr;

    // Log
    QTextEdit   *m_logView       = nullptr;
};

#endif // MAINWINDOW_H
