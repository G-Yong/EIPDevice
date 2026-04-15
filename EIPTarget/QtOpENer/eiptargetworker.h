/*******************************************************************************
 * eiptargetworker.h - Qt wrapper running OpENer stack in a worker thread
 ******************************************************************************/
#ifndef EIPTARGETWORKER_H
#define EIPTARGETWORKER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QByteArray>
#include <atomic>

class EipTargetWorker : public QObject
{
    Q_OBJECT
public:
    explicit EipTargetWorker(QObject *parent = nullptr);
    ~EipTargetWorker() override;

    bool isRunning() const { return m_running.load(); }

    // Read I/O data (thread-safe)
    QByteArray inputData() const;
    QByteArray outputData() const;

    // Write input data to be sent to scanner (thread-safe)
    void setInputData(const QByteArray &data);

    // Set I/O data sizes (must be called before start)
    void setIoSizes(int inputSize, int outputSize);
    int inputSize() const { return m_inputSize; }
    int outputSize() const { return m_outputSize; }

signals:
    void started();
    void stopped();
    void errorOccurred(const QString &error);
    void logMessage(const QString &msg);
    void ioConnectionEvent(unsigned int outputAsm, unsigned int inputAsm, int event);
    void outputDataReceived(const QByteArray &data);
    void ipConfigured(const QString &ip);

public slots:
    void start(const QString &ifaceIndex);
    void stop();

private:
    void run(const QString &ifaceIndex);

    QThread     m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<int>  m_stopFlag{0};
    int m_inputSize = 32;
    int m_outputSize = 32;

public:
    mutable QMutex m_dataMutex;
    QByteArray m_inputData;
    QByteArray m_outputData;
};

#endif // EIPTARGETWORKER_H
