/*******************************************************************************
 * eiptargetservice.h - Reusable EtherNet/IP Target (Adapter) service
 *
 * High-level API wrapping OpENer stack. Can be used by both GUI and CLI apps.
 * Thread-safe: the OpENer event loop runs in a background thread while I/O
 * data is exchanged through mutex-protected accessors.
 *
 * Usage:
 *   EipTargetService svc;
 *   svc.config().inputSize  = 32;
 *   svc.config().outputSize = 32;
 *   svc.start("3");                 // network interface index
 *   svc.setInputData(QByteArray(32, '\0'));
 *   ...
 *   svc.stop();
 ******************************************************************************/
#ifndef EIPTARGETSERVICE_H
#define EIPTARGETSERVICE_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>

class EipTargetWorker;

/* ---- Configuration ---- */
struct EipTargetConfig
{
    int inputSize  = 32;   // T->O assembly data size (bytes, 1-512)
    int outputSize = 32;   // O->T assembly data size (bytes, 1-512)
};

/* ---- Network interface info ---- */
struct EipNetworkInterface
{
    int     index;         // OS interface index
    QString name;          // Human-readable name
    QString ipAddress;     // IPv4 address
    QString displayLabel;  // "name (ip) [idx:N]"
};

/* ---- EIP Target Service ---- */
class EipTargetService : public QObject
{
    Q_OBJECT
public:
    explicit EipTargetService(QObject *parent = nullptr);
    ~EipTargetService() override;

    /* -- Configuration (set before start) -- */
    EipTargetConfig &config() { return m_config; }
    const EipTargetConfig &config() const { return m_config; }

    /* -- Lifecycle -- */
    bool isRunning() const;
    void start(const QString &ifaceIndex);
    void stop();

    /* -- I/O Data (thread-safe) -- */
    QByteArray inputData() const;
    QByteArray outputData() const;
    void setInputData(const QByteArray &data);

    /* -- Utilities -- */
    static QList<EipNetworkInterface> availableInterfaces();
    static QString generateEds(int inputSize, int outputSize);

signals:
    void started();
    void stopped();
    void errorOccurred(const QString &error);
    void logMessage(const QString &msg);
    void ioConnectionEvent(unsigned int outputAsm, unsigned int inputAsm, int event);
    void outputDataReceived(const QByteArray &data);
    void ipConfigured(const QString &ip);

private:
    EipTargetConfig  m_config;
    EipTargetWorker *m_worker = nullptr;
};

#endif // EIPTARGETSERVICE_H
