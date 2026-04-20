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
    QString openerIdent;   // OpENer identifier: interface name on Linux, index string on Windows
};

/* ---- EDS Assembly Member descriptor ---- */
struct EdsAssemblyMember
{
    quint8  cipType;     // CIP Data Type code (e.g., 0xC1=BOOL, 0xCA=REAL, 0xD1=BYTE)
    int     bitLen;      // Size in bits within the assembly
    QString name;        // Variable name (used in EDS comments)
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

    // IP 协议标准偏向规定用 Scanner 视角来命名
    // 因此 inputData-->TOData, outputData-->OTData
    /* -- I/O Data (thread-safe) -- */
    QByteArray inputData() const;
    QByteArray outputData() const;
    void setInputData(const QByteArray &data);

    /* -- Device Identity (set before start, applied during init) -- */
    void setDeviceIdentity(const QString &vendorName, quint16 vendorID,
                           const QString &productName, quint16 productCode,
                           quint8 majorRev, quint8 minorRev);

    /* -- Utilities -- */
    static QList<EipNetworkInterface> availableInterfaces();
    static QString generateEds(const QList<EdsAssemblyMember> &inputMembers,
                               const QList<EdsAssemblyMember> &outputMembers,
                               const QString &vendorName, quint16 vendorID,
                               const QString &productName, quint16 productCode,
                               quint8 majorRev, quint8 minorRev);

signals:
    void started();
    void stopped();
    void errorOccurred(const QString &error);
    void logMessage(const QString &msg);
    void ioConnectionEvent(unsigned int outputAsm, unsigned int inputAsm, int event);
    // IP 协议标准偏向规定用 Scanner 视角来命名
    // 因此这个是属于OT数据，主站写入了新的O->T数据的事件
    void outputDataReceived(const QByteArray &data);
    void ipConfigured(const QString &ip);

private:
    EipTargetConfig  m_config;
    EipTargetWorker *m_worker = nullptr;
};

#endif // EIPTARGETSERVICE_H
