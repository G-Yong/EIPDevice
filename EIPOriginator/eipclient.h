/*******************************************************************************
 * eipclient.h - EtherNet/IP Scanner (Client) Protocol Implementation
 *
 * 实现 EtherNet/IP 主站协议，包括:
 *   - 设备发现 (ListIdentity)
 *   - 会话管理 (RegisterSession / UnregisterSession)
 *   - 显式消息 (SendRRData: Get/Set Attribute)
 *   - Assembly 读写
 *   - I/O 隐式消息 (Forward Open / Close)
 *
 * 跨平台: Windows / Linux
 ******************************************************************************/
#ifndef EIPCLIENT_H
#define EIPCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QByteArray>
#include <QVector>
#include <QMutex>

/* ============================================================
 * EtherNet/IP 协议常量
 * ============================================================ */
namespace EipConst {
    constexpr quint16 TcpPort      = 44818;
    constexpr quint16 UdpPort      = 44818;
    constexpr quint16 IoUdpPort    = 2222;
    constexpr int     EncapHeaderLen = 24;

    // Encapsulation Commands
    constexpr quint16 CmdNop              = 0x0000;
    constexpr quint16 CmdListServices     = 0x0004;
    constexpr quint16 CmdListIdentity     = 0x0063;
    constexpr quint16 CmdListInterfaces   = 0x0064;
    constexpr quint16 CmdRegisterSession  = 0x0065;
    constexpr quint16 CmdUnregisterSession= 0x0066;
    constexpr quint16 CmdSendRRData       = 0x006F;
    constexpr quint16 CmdSendUnitData     = 0x0070;

    // CIP Services
    constexpr quint8 SvcGetAll            = 0x01;
    constexpr quint8 SvcGetAttrSingle     = 0x0E;
    constexpr quint8 SvcSetAttrSingle     = 0x10;
    constexpr quint8 SvcForwardOpen       = 0x54;
    constexpr quint8 SvcForwardClose      = 0x4E;

    // CIP Class Codes
    constexpr quint8 ClassIdentity        = 0x01;
    constexpr quint8 ClassMessageRouter   = 0x02;
    constexpr quint8 ClassAssembly        = 0x04;
    constexpr quint8 ClassConnectionMgr   = 0x06;
    constexpr quint8 ClassTcpIp           = 0xF5;
    constexpr quint8 ClassEthernetLink    = 0xF6;

    // CPF Item Types
    constexpr quint16 CpfNull             = 0x0000;
    constexpr quint16 CpfListIdentity     = 0x000C;
    constexpr quint16 CpfConnAddress      = 0x00A1;
    constexpr quint16 CpfConnData         = 0x00B1;
    constexpr quint16 CpfUnconnData       = 0x00B2;
    constexpr quint16 CpfSockAddrOT       = 0x8000;
    constexpr quint16 CpfSockAddrTO       = 0x8001;
    constexpr quint16 CpfSeqAddress       = 0x8002;
}

/* ============================================================
 * 数据结构
 * ============================================================ */

struct EipDeviceInfo {
    QString  ip;
    QString  productName;
    quint16  vendorId      = 0;
    quint16  deviceType    = 0;
    quint16  productCode   = 0;
    quint8   revisionMajor = 0;
    quint8   revisionMinor = 0;
    quint16  status        = 0;
    quint32  serialNumber  = 0;
};

struct CipResponse {
    quint8     service    = 0;
    quint8     status     = 0;
    QByteArray addlStatus;
    QByteArray data;
    bool       success    = false;
    QString    errorText;
};

/* ============================================================
 * EipClient - 主类
 * ============================================================ */

class EipClient : public QObject
{
    Q_OBJECT
public:
    explicit EipClient(QObject *parent = nullptr);
    ~EipClient() override;

    // --- 设备发现 ---
    void discover(const QString &broadcastAddr = QStringLiteral("255.255.255.255"),
                  int timeoutMs = 3000);

    // --- 连接管理 ---
    void connectToDevice(const QString &ip, quint16 port = EipConst::TcpPort);
    void disconnectFromDevice();
    bool isConnected() const { return m_connected; }
    quint32 sessionHandle() const { return m_sessionHandle; }

    // --- CIP 显式消息 ---
    CipResponse getAttributeAll(quint16 classId, quint16 instanceId);
    CipResponse getAttributeSingle(quint16 classId, quint16 instanceId, quint16 attributeId);
    CipResponse setAttributeSingle(quint16 classId, quint16 instanceId, quint16 attributeId, const QByteArray &value);

    // --- Assembly ---
    CipResponse readAssembly(quint16 instanceId);
    CipResponse writeAssembly(quint16 instanceId, const QByteArray &data);

    // --- Identity ---
    EipDeviceInfo readIdentity();

    // --- I/O 连接 (Forward Open / Close) ---
    bool forwardOpen(quint16 inputAssembly = 100, quint16 outputAssembly = 150,
                     quint16 configAssembly = 151,
                     quint16 inputSize = 32, quint16 outputSize = 32,
                     quint32 rpiUs = 100000);
    void forwardClose();
    void startIO();
    void stopIO();
    bool isIOActive() const { return m_ioActive; }

    // I/O data access
    QByteArray inputData() const;   // T->O: data from device
    QByteArray outputData() const;  // O->T: data to device
    void setOutputData(const QByteArray &data);

signals:
    void deviceDiscovered(const EipDeviceInfo &device);
    void discoverFinished();
    void connected();
    void disconnected();
    void connectionError(const QString &error);
    void ioDataReceived(const QByteArray &data);
    void ioError(const QString &error);
    void logMessage(const QString &msg);

private slots:
    void onTcpConnected();
    void onTcpDisconnected();
    void onTcpError(QAbstractSocket::SocketError err);
    void onTcpReadyRead();
    void onDiscoverTimeout();
    void onDiscoverReadyRead();
    void onIoTimerTimeout();
    void onIoUdpReadyRead();

private:
    // Protocol helpers
    QByteArray buildEncapHeader(quint16 command, quint32 session, const QByteArray &data);
    bool parseEncapHeader(const QByteArray &raw, quint16 &command, quint16 &length,
                          quint32 &session, quint32 &status, QByteArray &payload);
    QByteArray buildCipPath(quint16 classId, quint16 instanceId, int attributeId = -1);
    QByteArray buildSendRRData(const QByteArray &cipData,
                               const QVector<QPair<quint16,QByteArray>> &extraCpf = {});
    CipResponse parseCipResponse(const QByteArray &data);
    CipResponse sendRRDataSync(const QByteArray &cipData,
                               const QVector<QPair<quint16,QByteArray>> &extraCpf = {});

    // I/O helpers
    void sendIOData();
    void processIOPacket(const QByteArray &data);

    static QString cipErrorString(quint8 status);

    // TCP
    QTcpSocket  *m_tcpSocket = nullptr;
    bool         m_connected = false;
    quint32      m_sessionHandle = 0;
    QString      m_targetIp;
    QString      m_localIp;
    QByteArray   m_tcpBuffer;

    // Discovery
    QUdpSocket  *m_discoverSocket = nullptr;
    QTimer      *m_discoverTimer  = nullptr;

    // I/O
    QUdpSocket  *m_ioSocket = nullptr;
    QTimer      *m_ioTimer  = nullptr;
    bool         m_ioActive = false;
    quint32      m_otConnectionId = 0;  // O->T ID assigned by target
    quint32      m_toConnectionId = 0;  // T->O ID assigned by target
    quint16      m_connectionSerial = 0;
    quint32      m_originatorSerial = 0x12345678;
    quint16      m_originatorVendor = 0x0001;
    quint32      m_ioSeqCount = 0;
    quint32      m_rpiUs = 100000;
    bool         m_runIdleHeader = true;

    mutable QMutex m_ioMutex;
    QByteArray   m_inputData;   // T->O
    QByteArray   m_outputData;  // O->T
};

#endif // EIPCLIENT_H
