/*******************************************************************************
 * eipclient.cpp - EtherNet/IP Scanner (Client) Protocol Implementation
 ******************************************************************************/

#include "eipclient.h"

#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QEventLoop>
#include <QRandomGenerator>
#include <QtEndian>
#include <QThread>

/* ============================================================
 * Little-endian helpers (Qt uses host byte order by default)
 * ============================================================ */
static inline void writeLE16(QByteArray &buf, quint16 v) {
    v = qToLittleEndian(v);
    buf.append(reinterpret_cast<const char*>(&v), 2);
}
static inline void writeLE32(QByteArray &buf, quint32 v) {
    v = qToLittleEndian(v);
    buf.append(reinterpret_cast<const char*>(&v), 4);
}
static inline quint16 readLE16(const char *p) {
    quint16 v;
    memcpy(&v, p, 2);
    return qFromLittleEndian(v);
}
static inline quint32 readLE32(const char *p) {
    quint32 v;
    memcpy(&v, p, 4);
    return qFromLittleEndian(v);
}
static inline quint16 readBE16(const char *p) {
    quint16 v;
    memcpy(&v, p, 2);
    return qFromBigEndian(v);
}

/* ============================================================
 * Constructor / Destructor
 * ============================================================ */
EipClient::EipClient(QObject *parent)
    : QObject(parent)
{
    m_tcpSocket = new QTcpSocket(this);
    m_tcpSocket->setProxy(QNetworkProxy::NoProxy);
    connect(m_tcpSocket, &QTcpSocket::connected, this, &EipClient::onTcpConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &EipClient::onTcpDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &EipClient::onTcpReadyRead);
    connect(m_tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &EipClient::onTcpError);

    m_discoverTimer = new QTimer(this);
    m_discoverTimer->setSingleShot(true);
    connect(m_discoverTimer, &QTimer::timeout, this, &EipClient::onDiscoverTimeout);
}

EipClient::~EipClient()
{
    stopIO();
    disconnectFromDevice();
}

/* ============================================================
 * Device Discovery (ListIdentity via UDP broadcast)
 * ============================================================ */
void EipClient::discover(const QString &broadcastAddr, int timeoutMs)
{
    m_discoveredIps.clear();
    if (m_discoverSocket) {
        m_discoverSocket->close();
        m_discoverSocket->deleteLater();
    }
    m_discoverSocket = new QUdpSocket(this);
    m_discoverSocket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress);
    connect(m_discoverSocket, &QUdpSocket::readyRead, this, &EipClient::onDiscoverReadyRead);

    QByteArray packet = buildEncapHeader(EipConst::CmdListIdentity, 0, QByteArray());
    m_discoverSocket->writeDatagram(packet, QHostAddress(broadcastAddr), EipConst::UdpPort);

    m_discoverTimer->start(timeoutMs);
    emit logMessage(QStringLiteral("发送 ListIdentity 广播到 %1").arg(broadcastAddr));
}

void EipClient::onDiscoverReadyRead()
{
    while (m_discoverSocket && m_discoverSocket->hasPendingDatagrams()) {
        QNetworkDatagram dg = m_discoverSocket->receiveDatagram();
        QByteArray raw = dg.data();
        QString fromIp = dg.senderAddress().toString();

        quint16 cmd, len;
        quint32 session, status;
        QByteArray payload;
        if (!parseEncapHeader(raw, cmd, len, session, status, payload))
            continue;
        if (cmd != EipConst::CmdListIdentity)
            continue;
        if (payload.size() < 2)
            continue;

        quint16 itemCount = readLE16(payload.constData());
        if (itemCount < 1) continue;

        int offset = 2;
        if (payload.size() < offset + 4) continue;
        quint16 itemType = readLE16(payload.constData() + offset);
        quint16 itemLen  = readLE16(payload.constData() + offset + 2);
        offset += 4;

        if (itemType != EipConst::CpfListIdentity) continue;
        if (payload.size() < offset + itemLen) continue;

        const char *item = payload.constData() + offset;
        if (itemLen < 33) continue;

        EipDeviceInfo dev;
        dev.ip = fromIp;
        // Skip protocol version (2) + socket address info (16)
        int idOff = 18;
        dev.vendorId      = readLE16(item + idOff);
        dev.deviceType    = readLE16(item + idOff + 2);
        dev.productCode   = readLE16(item + idOff + 4);
        dev.revisionMajor = static_cast<quint8>(item[idOff + 6]);
        dev.revisionMinor = static_cast<quint8>(item[idOff + 7]);
        dev.status        = readLE16(item + idOff + 8);
        dev.serialNumber  = readLE32(item + idOff + 10);
        quint8 nameLen    = static_cast<quint8>(item[idOff + 14]);
        if (idOff + 15 + nameLen <= itemLen) {
            dev.productName = QString::fromUtf8(item + idOff + 15, nameLen);
        }

        if (m_discoveredIps.contains(dev.ip))
            continue;
        m_discoveredIps.insert(dev.ip);
        emit deviceDiscovered(dev);
    }
}

void EipClient::onDiscoverTimeout()
{
    if (m_discoverSocket) {
        m_discoverSocket->close();
        m_discoverSocket->deleteLater();
        m_discoverSocket = nullptr;
    }
    emit discoverFinished();
    emit logMessage(QStringLiteral("设备发现完成"));
}

/* ============================================================
 * TCP Connection & Session Management
 * ============================================================ */
void EipClient::connectToDevice(const QString &ip, quint16 port)
{
    m_targetIp = ip;
    m_tcpBuffer.clear();
    m_tcpSocket->connectToHost(ip, port);
}

void EipClient::disconnectFromDevice()
{
    if (m_connected) {
        // Unregister Session
        QByteArray pkt = buildEncapHeader(EipConst::CmdUnregisterSession, m_sessionHandle, QByteArray());
        m_tcpSocket->write(pkt);
        m_tcpSocket->flush();
    }
    m_tcpSocket->disconnectFromHost();
    m_connected = false;
    m_sessionHandle = 0;
}

void EipClient::onTcpConnected()
{
    m_localIp = m_tcpSocket->localAddress().toString();
    emit logMessage(QStringLiteral("TCP 连接已建立，注册会话..."));

    // Register Session
    QByteArray regData;
    writeLE16(regData, 1); // Protocol version
    writeLE16(regData, 0); // Options
    QByteArray pkt = buildEncapHeader(EipConst::CmdRegisterSession, 0, regData);
    m_tcpSocket->write(pkt);
}

void EipClient::onTcpDisconnected()
{
    m_connected = false;
    m_sessionHandle = 0;
    emit disconnected();
    emit logMessage(QStringLiteral("TCP 连接已断开"));
}

void EipClient::onTcpError(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err)
    emit connectionError(m_tcpSocket->errorString());
    emit logMessage(QStringLiteral("TCP 错误: %1").arg(m_tcpSocket->errorString()));
}

void EipClient::onTcpReadyRead()
{
    if (m_syncMode) return;
    m_tcpBuffer.append(m_tcpSocket->readAll());

    // Process complete encap packets
    while (m_tcpBuffer.size() >= EipConst::EncapHeaderLen) {
        quint16 dataLen = readLE16(m_tcpBuffer.constData() + 2);
        int totalLen = EipConst::EncapHeaderLen + dataLen;
        if (m_tcpBuffer.size() < totalLen)
            break;

        QByteArray packet = m_tcpBuffer.left(totalLen);
        m_tcpBuffer.remove(0, totalLen);

        quint16 cmd, len;
        quint32 session, status;
        QByteArray payload;
        if (!parseEncapHeader(packet, cmd, len, session, status, payload))
            continue;

        if (cmd == EipConst::CmdRegisterSession) {
            if (status == 0) {
                m_sessionHandle = session;
                m_connected = true;
                emit connected();
                emit logMessage(QStringLiteral("会话注册成功, handle=0x%1")
                                    .arg(session, 8, 16, QLatin1Char('0')));
            } else {
                emit connectionError(QStringLiteral("RegisterSession 失败, status=0x%1")
                                         .arg(status, 4, 16, QLatin1Char('0')));
            }
        }
        // Other responses are handled synchronously via sendRRDataSync
    }
}

/* ============================================================
 * CIP Explicit Messaging
 * ============================================================ */
CipResponse EipClient::getAttributeAll(quint16 classId, quint16 instanceId)
{
    QByteArray path = buildCipPath(classId, instanceId);
    QByteArray cipReq;
    cipReq.append(static_cast<char>(EipConst::SvcGetAll));
    cipReq.append(static_cast<char>(path.size() / 2));
    cipReq.append(path);
    return sendRRDataSync(cipReq);
}

CipResponse EipClient::getAttributeSingle(quint16 classId, quint16 instanceId, quint16 attributeId)
{
    QByteArray path = buildCipPath(classId, instanceId, attributeId);
    QByteArray cipReq;
    cipReq.append(static_cast<char>(EipConst::SvcGetAttrSingle));
    cipReq.append(static_cast<char>(path.size() / 2));
    cipReq.append(path);
    return sendRRDataSync(cipReq);
}

CipResponse EipClient::setAttributeSingle(quint16 classId, quint16 instanceId, quint16 attributeId,
                                           const QByteArray &value)
{
    QByteArray path = buildCipPath(classId, instanceId, attributeId);
    QByteArray cipReq;
    cipReq.append(static_cast<char>(EipConst::SvcSetAttrSingle));
    cipReq.append(static_cast<char>(path.size() / 2));
    cipReq.append(path);
    cipReq.append(value);
    return sendRRDataSync(cipReq);
}

CipResponse EipClient::readAssembly(quint16 instanceId)
{
    return getAttributeSingle(EipConst::ClassAssembly, instanceId, 3);
}

CipResponse EipClient::writeAssembly(quint16 instanceId, const QByteArray &data)
{
    return setAttributeSingle(EipConst::ClassAssembly, instanceId, 3, data);
}

EipDeviceInfo EipClient::readIdentity()
{
    EipDeviceInfo info;
    CipResponse resp = getAttributeAll(EipConst::ClassIdentity, 1);
    if (!resp.success) {
        emit logMessage(QStringLiteral("读取 Identity 失败: %1").arg(resp.errorText));
        return info;
    }

    const QByteArray &d = resp.data;
    if (d.size() >= 14) {
        info.vendorId      = readLE16(d.constData());
        info.deviceType    = readLE16(d.constData() + 2);
        info.productCode   = readLE16(d.constData() + 4);
        info.revisionMajor = static_cast<quint8>(d[6]);
        info.revisionMinor = static_cast<quint8>(d[7]);
        info.status        = readLE16(d.constData() + 8);
        info.serialNumber  = readLE32(d.constData() + 10);
        if (d.size() > 14) {
            quint8 nameLen = static_cast<quint8>(d[14]);
            if (d.size() >= 15 + nameLen)
                info.productName = QString::fromUtf8(d.constData() + 15, nameLen);
        }
    }
    info.ip = m_targetIp;
    return info;
}

/* ============================================================
 * Forward Open / Close  (I/O Implicit Messaging)
 * ============================================================ */
bool EipClient::forwardOpen(quint16 inputAssembly, quint16 outputAssembly,
                             quint16 configAssembly,
                             quint16 inputSize, quint16 outputSize,
                             quint32 rpiUs)
{
    if (!m_connected) {
        emit ioError(QStringLiteral("未连接到设备"));
        return false;
    }

    m_rpiUs = rpiUs;
    m_connectionSerial = static_cast<quint16>(QRandomGenerator::global()->bounded(1, 0xFFFF));

    {
        QMutexLocker lock(&m_ioMutex);
        m_inputData.fill(0, inputSize);
        m_outputData.fill(0, outputSize);
    }

    // Class 1 transport: +2 bytes sequence count
    // O->T: +4 bytes Run/Idle header
    quint16 otConnSize = outputSize + 2 + (m_runIdleHeader ? 4 : 0);
    quint16 toConnSize = inputSize + 2;

    // Build Forward Open CIP request
    QByteArray path = buildCipPath(EipConst::ClassConnectionMgr, 1);
    QByteArray cipReq;
    cipReq.append(static_cast<char>(EipConst::SvcForwardOpen));
    cipReq.append(static_cast<char>(path.size() / 2));
    cipReq.append(path);

    // Priority/Tick + Timeout ticks
    cipReq.append(static_cast<char>(0x0A));  // priority_tick
    cipReq.append(static_cast<char>(250));   // timeout_ticks

    quint32 proposedOtId = QRandomGenerator::global()->bounded(0x10000u, 0xFFFFFFu);
    quint32 proposedToId = QRandomGenerator::global()->bounded(0x10000u, 0xFFFFFFu);
    writeLE32(cipReq, proposedOtId);  // O->T Connection ID
    writeLE32(cipReq, proposedToId);  // T->O Connection ID

    writeLE16(cipReq, m_connectionSerial);
    writeLE16(cipReq, m_originatorVendor);
    writeLE32(cipReq, m_originatorSerial);

    // Connection Timeout Multiplier + 3 reserved bytes
    cipReq.append(static_cast<char>(4));
    cipReq.append(3, '\0');

    // O->T RPI
    writeLE32(cipReq, rpiUs);
    // O->T Network Connection Parameters
    quint16 otParams = (otConnSize & 0x01FF) | (0 << 9) | (2 << 13);
    writeLE16(cipReq, otParams);

    // T->O RPI
    writeLE32(cipReq, rpiUs);
    // T->O Network Connection Parameters
    quint16 toParams = (toConnSize & 0x01FF) | (0 << 9) | (2 << 13);
    writeLE16(cipReq, toParams);

    // Transport Type/Trigger: Class 1, cyclic, server
    cipReq.append(static_cast<char>(0x01));

    // Connection Path
    QByteArray connPath;
    // Config: Class Assembly (0x04) + Instance
    connPath.append(static_cast<char>(0x20));
    connPath.append(static_cast<char>(0x04));
    if (configAssembly <= 0xFF) {
        connPath.append(static_cast<char>(0x24));
        connPath.append(static_cast<char>(configAssembly));
    } else {
        connPath.append(static_cast<char>(0x25));
        connPath.append(static_cast<char>(0x00));
        writeLE16(connPath, configAssembly);
    }
    // O->T Connection Point (output assembly)
    if (outputAssembly <= 0xFF) {
        connPath.append(static_cast<char>(0x2C));
        connPath.append(static_cast<char>(outputAssembly));
    } else {
        connPath.append(static_cast<char>(0x2D));
        connPath.append(static_cast<char>(0x00));
        writeLE16(connPath, outputAssembly);
    }
    // T->O Connection Point (input assembly)
    if (inputAssembly <= 0xFF) {
        connPath.append(static_cast<char>(0x2C));
        connPath.append(static_cast<char>(inputAssembly));
    } else {
        connPath.append(static_cast<char>(0x2D));
        connPath.append(static_cast<char>(0x00));
        writeLE16(connPath, inputAssembly);
    }
    cipReq.append(static_cast<char>(connPath.size() / 2));  // path size in words
    cipReq.append(connPath);

    // Create UDP socket for I/O
    if (m_ioSocket) {
        m_ioSocket->close();
        m_ioSocket->deleteLater();
    }
    m_ioSocket = new QUdpSocket(this);
    m_ioSocket->bind(QHostAddress(m_localIp), 0);
    connect(m_ioSocket, &QUdpSocket::readyRead, this, &EipClient::onIoUdpReadyRead);
    quint16 localUdpPort = m_ioSocket->localPort();

    // Build SocketAddressInfo CPF items
    QHostAddress localAddr(m_localIp);
    quint32 localIpBE = qToBigEndian(localAddr.toIPv4Address());

    QByteArray sockAddrOT;
    { // O->T
        quint16 sinFamily = qToBigEndian(quint16(2));
        quint16 sinPort   = qToBigEndian(quint16(EipConst::IoUdpPort));
        sockAddrOT.append(reinterpret_cast<const char*>(&sinFamily), 2);
        sockAddrOT.append(reinterpret_cast<const char*>(&sinPort), 2);
        sockAddrOT.append(reinterpret_cast<const char*>(&localIpBE), 4);
        sockAddrOT.append(8, '\0');
    }
    QByteArray sockAddrTO;
    { // T->O
        quint16 sinFamily = qToBigEndian(quint16(2));
        quint16 sinPort   = qToBigEndian(localUdpPort);
        sockAddrTO.append(reinterpret_cast<const char*>(&sinFamily), 2);
        sockAddrTO.append(reinterpret_cast<const char*>(&sinPort), 2);
        sockAddrTO.append(reinterpret_cast<const char*>(&localIpBE), 4);
        sockAddrTO.append(8, '\0');
    }

    QVector<QPair<quint16,QByteArray>> extraCpf;
    extraCpf.append({EipConst::CpfSockAddrOT, sockAddrOT});
    extraCpf.append({EipConst::CpfSockAddrTO, sockAddrTO});

    CipResponse resp = sendRRDataSync(cipReq, extraCpf);

    if (!resp.success) {
        if (m_ioSocket) {
            m_ioSocket->close();
            m_ioSocket->deleteLater();
            m_ioSocket = nullptr;
        }
        QString errMsg = resp.errorText;
        if (resp.addlStatus.size() >= 2) {
            quint16 extStatus = readLE16(resp.addlStatus.constData());
            errMsg += QStringLiteral(" (ext=0x%1)").arg(extStatus, 4, 16, QLatin1Char('0'));
        }
        emit ioError(QStringLiteral("Forward Open 失败: %1").arg(errMsg));
        return false;
    }

    if (resp.data.size() >= 8) {
        m_otConnectionId = readLE32(resp.data.constData());
        m_toConnectionId = readLE32(resp.data.constData() + 4);
    }

    emit logMessage(QStringLiteral("Forward Open 成功: O->T ID=0x%1, T->O ID=0x%2")
                        .arg(m_otConnectionId, 8, 16, QLatin1Char('0'))
                        .arg(m_toConnectionId, 8, 16, QLatin1Char('0')));
    return true;
}

void EipClient::forwardClose()
{
    if (!m_connected) return;

    QByteArray path = buildCipPath(EipConst::ClassConnectionMgr, 1);
    QByteArray cipReq;
    cipReq.append(static_cast<char>(EipConst::SvcForwardClose));
    cipReq.append(static_cast<char>(path.size() / 2));
    cipReq.append(path);

    cipReq.append(static_cast<char>(0x0A));  // priority_tick
    cipReq.append(static_cast<char>(250));   // timeout_ticks

    writeLE16(cipReq, m_connectionSerial);
    writeLE16(cipReq, m_originatorVendor);
    writeLE32(cipReq, m_originatorSerial);

    // Connection Path (minimal)
    QByteArray connPath;
    connPath.append(static_cast<char>(0x20));
    connPath.append(static_cast<char>(0x04));
    connPath.append(static_cast<char>(0x24));
    connPath.append(static_cast<char>(0x01));
    cipReq.append(static_cast<char>(connPath.size() / 2));
    cipReq.append(static_cast<char>(0x00));  // reserved
    cipReq.append(connPath);

    sendRRDataSync(cipReq);
    emit logMessage(QStringLiteral("Forward Close 已发送"));
}

void EipClient::startIO()
{
    if (m_ioActive) return;
    m_ioActive = true;
    m_ioSeqCount = 0;

    if (!m_ioTimer) {
        m_ioTimer = new QTimer(this);
        connect(m_ioTimer, &QTimer::timeout, this, &EipClient::onIoTimerTimeout);
    }
    m_ioTimer->start(static_cast<int>(m_rpiUs / 1000));
    emit logMessage(QStringLiteral("I/O 数据交换已启动 (RPI=%1ms)").arg(m_rpiUs / 1000));
}

void EipClient::stopIO()
{
    m_ioActive = false;
    if (m_ioTimer) m_ioTimer->stop();
    if (m_ioSocket) {
        m_ioSocket->close();
        m_ioSocket->deleteLater();
        m_ioSocket = nullptr;
    }
    emit logMessage(QStringLiteral("I/O 数据交换已停止"));
}

QByteArray EipClient::inputData() const
{
    QMutexLocker lock(&m_ioMutex);
    return m_inputData;
}

QByteArray EipClient::outputData() const
{
    QMutexLocker lock(&m_ioMutex);
    return m_outputData;
}

void EipClient::setOutputData(const QByteArray &data)
{
    QMutexLocker lock(&m_ioMutex);
    m_outputData = data;
}

void EipClient::onIoTimerTimeout()
{
    if (m_ioActive) sendIOData();
}

void EipClient::onIoUdpReadyRead()
{
    while (m_ioSocket && m_ioSocket->hasPendingDatagrams()) {
        QNetworkDatagram dg = m_ioSocket->receiveDatagram();
        processIOPacket(dg.data());
    }
}

void EipClient::sendIOData()
{
    if (!m_ioSocket || !m_ioActive) return;

    m_ioSeqCount++;
    QByteArray packet;

    // CPF: 2 items
    writeLE16(packet, 2);  // item count

    // Item 1: Sequenced Address (0x8002)
    writeLE16(packet, EipConst::CpfSeqAddress);
    writeLE16(packet, 8);  // length
    writeLE32(packet, m_otConnectionId);
    writeLE32(packet, m_ioSeqCount);

    // Item 2: Connected Data (0x00B1)
    QByteArray ioData;
    writeLE16(ioData, static_cast<quint16>(m_ioSeqCount & 0xFFFF));  // sequence count
    if (m_runIdleHeader) {
        writeLE32(ioData, 0x00000001);  // Run/Idle: Run
    }
    {
        QMutexLocker lock(&m_ioMutex);
        ioData.append(m_outputData);
    }
    writeLE16(packet, EipConst::CpfConnData);
    writeLE16(packet, static_cast<quint16>(ioData.size()));
    packet.append(ioData);

    m_ioSocket->writeDatagram(packet, QHostAddress(m_targetIp), EipConst::IoUdpPort);
}

void EipClient::processIOPacket(const QByteArray &data)
{
    if (data.size() < 6) return;

    quint16 itemCount = readLE16(data.constData());
    int offset = 2;

    for (int i = 0; i < itemCount && offset + 4 <= data.size(); ++i) {
        quint16 itemType = readLE16(data.constData() + offset);
        quint16 itemLen  = readLE16(data.constData() + offset + 2);
        offset += 4;

        if (itemType == EipConst::CpfConnData && itemLen >= 2) {
            // Skip 2-byte sequence count
            QByteArray ioPayload = data.mid(offset + 2, itemLen - 2);
            {
                QMutexLocker lock(&m_ioMutex);
                m_inputData = ioPayload;
            }
            emit ioDataReceived(ioPayload);
        }
        offset += itemLen;
    }
}

/* ============================================================
 * Protocol building helpers
 * ============================================================ */
QByteArray EipClient::buildEncapHeader(quint16 command, quint32 session, const QByteArray &data)
{
    QByteArray buf;
    buf.reserve(EipConst::EncapHeaderLen + data.size());
    writeLE16(buf, command);
    writeLE16(buf, static_cast<quint16>(data.size()));
    writeLE32(buf, session);
    writeLE32(buf, 0);       // status
    buf.append(8, '\0');     // sender context (8 bytes)
    writeLE32(buf, 0);       // options
    buf.append(data);
    return buf;
}

bool EipClient::parseEncapHeader(const QByteArray &raw, quint16 &command, quint16 &length,
                                  quint32 &session, quint32 &status, QByteArray &payload)
{
    if (raw.size() < EipConst::EncapHeaderLen) return false;
    const char *p = raw.constData();
    command = readLE16(p);
    length  = readLE16(p + 2);
    session = readLE32(p + 4);
    status  = readLE32(p + 8);
    // sender context at 12..19
    // options at 20..23
    payload = raw.mid(EipConst::EncapHeaderLen, length);
    return true;
}

QByteArray EipClient::buildCipPath(quint16 classId, quint16 instanceId, int attributeId)
{
    QByteArray path;
    if (classId <= 0xFF) {
        path.append(static_cast<char>(0x20));
        path.append(static_cast<char>(classId));
    } else {
        path.append(static_cast<char>(0x21));
        path.append(static_cast<char>(0x00));
        writeLE16(path, classId);
    }
    if (instanceId <= 0xFF) {
        path.append(static_cast<char>(0x24));
        path.append(static_cast<char>(instanceId));
    } else {
        path.append(static_cast<char>(0x25));
        path.append(static_cast<char>(0x00));
        writeLE16(path, instanceId);
    }
    if (attributeId >= 0) {
        if (attributeId <= 0xFF) {
            path.append(static_cast<char>(0x30));
            path.append(static_cast<char>(attributeId));
        } else {
            path.append(static_cast<char>(0x31));
            path.append(static_cast<char>(0x00));
            writeLE16(path, static_cast<quint16>(attributeId));
        }
    }
    return path;
}

QByteArray EipClient::buildSendRRData(const QByteArray &cipData,
                                       const QVector<QPair<quint16,QByteArray>> &extraCpf)
{
    QByteArray cpf;
    writeLE32(cpf, 0);    // Interface Handle
    writeLE16(cpf, 10);   // Timeout

    quint16 itemCount = 2 + static_cast<quint16>(extraCpf.size());
    writeLE16(cpf, itemCount);

    // Item 1: Null Address
    writeLE16(cpf, EipConst::CpfNull);
    writeLE16(cpf, 0);

    // Item 2: Unconnected Data
    writeLE16(cpf, EipConst::CpfUnconnData);
    writeLE16(cpf, static_cast<quint16>(cipData.size()));
    cpf.append(cipData);

    // Extra CPF items
    for (auto &item : extraCpf) {
        writeLE16(cpf, item.first);
        writeLE16(cpf, static_cast<quint16>(item.second.size()));
        cpf.append(item.second);
    }

    return buildEncapHeader(EipConst::CmdSendRRData, m_sessionHandle, cpf);
}

CipResponse EipClient::parseCipResponse(const QByteArray &data)
{
    CipResponse resp;
    if (data.size() < 4) {
        resp.errorText = QStringLiteral("CIP 响应太短");
        return resp;
    }

    resp.service = static_cast<quint8>(data[0]);
    // data[1] is reserved
    resp.status  = static_cast<quint8>(data[2]);
    quint8 addlSize = static_cast<quint8>(data[3]);

    int addlEnd = 4 + addlSize * 2;
    if (data.size() >= addlEnd) {
        resp.addlStatus = data.mid(4, addlSize * 2);
        resp.data = data.mid(addlEnd);
    }

    resp.success = (resp.status == 0);
    if (!resp.success) {
        resp.errorText = cipErrorString(resp.status);
    }
    return resp;
}

CipResponse EipClient::sendRRDataSync(const QByteArray &cipData,
                                       const QVector<QPair<quint16,QByteArray>> &extraCpf)
{
    CipResponse errResp;
    if (!m_connected) {
        errResp.errorText = QStringLiteral("未连接到设备");
        return errResp;
    }

    // Prevent onTcpReadyRead from consuming our response
    m_syncMode = true;

    QByteArray packet = buildSendRRData(cipData, extraCpf);
    m_tcpSocket->write(packet);
    m_tcpSocket->flush();

    // Start with any leftover data in m_tcpBuffer
    QByteArray raw = m_tcpBuffer;
    m_tcpBuffer.clear();

    // Wait until we have a complete encap packet
    while (true) {
        if (raw.size() >= EipConst::EncapHeaderLen) {
            quint16 dataLen = readLE16(raw.constData() + 2);
            int totalLen = EipConst::EncapHeaderLen + dataLen;
            if (raw.size() >= totalLen) {
                // Save any excess data back to m_tcpBuffer
                if (raw.size() > totalLen)
                    m_tcpBuffer = raw.mid(totalLen);
                raw = raw.left(totalLen);
                break;
            }
        }
        if (!m_tcpSocket->waitForReadyRead(5000)) {
            m_tcpBuffer = raw;
            m_syncMode = false;
            errResp.errorText = QStringLiteral("响应超时");
            return errResp;
        }
        raw.append(m_tcpSocket->readAll());
    }

    m_syncMode = false;

    quint16 cmd, len;
    quint32 session, status;
    QByteArray payload;
    if (!parseEncapHeader(raw, cmd, len, session, status, payload)) {
        errResp.errorText = QStringLiteral("解析 Encap 响应失败");
        return errResp;
    }
    if (status != 0) {
        errResp.errorText = QStringLiteral("Encap 错误 status=0x%1").arg(status, 4, 16, QLatin1Char('0'));
        return errResp;
    }

    // Parse CPF
    if (payload.size() < 8) {
        errResp.errorText = QStringLiteral("CPF 数据太短");
        return errResp;
    }
    // Skip Interface Handle(4) + Timeout(2)
    quint16 itemCount = readLE16(payload.constData() + 6);
    int offset = 8;
    QByteArray cipResponse;

    for (int i = 0; i < itemCount && offset + 4 <= payload.size(); ++i) {
        quint16 itemType = readLE16(payload.constData() + offset);
        quint16 itemLen  = readLE16(payload.constData() + offset + 2);
        offset += 4;
        if (itemType == EipConst::CpfUnconnData) {
            cipResponse = payload.mid(offset, itemLen);
        }
        offset += itemLen;
    }

    if (cipResponse.isEmpty()) {
        errResp.errorText = QStringLiteral("未找到 CIP 响应数据");
        return errResp;
    }

    return parseCipResponse(cipResponse);
}

QString EipClient::cipErrorString(quint8 status)
{
    static const struct { quint8 code; const char *text; } errors[] = {
        {0x01, "Connection failure"},
        {0x02, "Resource unavailable"},
        {0x03, "Invalid parameter value"},
        {0x04, "Path segment error"},
        {0x05, "Path destination unknown"},
        {0x06, "Partial transfer"},
        {0x08, "Service not supported"},
        {0x09, "Invalid attribute value"},
        {0x0A, "Attribute list error"},
        {0x0C, "Object state conflict"},
        {0x0E, "Attribute not settable"},
        {0x10, "Device state conflict"},
        {0x13, "Not enough data"},
        {0x14, "Attribute not supported"},
        {0x15, "Too much data"},
        {0x16, "Object does not exist"},
        {0x20, "Invalid parameter"},
        {0x26, "Path size invalid"},
    };
    for (auto &e : errors) {
        if (e.code == status) return QString::fromLatin1(e.text);
    }
    return QStringLiteral("Unknown error (0x%1)").arg(status, 2, 16, QLatin1Char('0'));
}
