/*******************************************************************************
 * eiptargetworker.cpp - Qt wrapper running OpENer stack in a worker thread
 ******************************************************************************/

#include "eiptargetworker.h"

#include <QMetaObject>

/* ============================================================
 * OpENer C headers (wrapped in extern "C")
 * ============================================================ */
extern "C" {
#include "generic_networkhandler.h"
#include "opener_api.h"
#include "cipethernetlink.h"
#include "ciptcpipinterface.h"
#include "trace.h"
#include "networkconfig.h"
#include "doublylinkedlist.h"
#include "cipconnectionobject.h"
#include "nvdata.h"
#include "my_application.h"

/* Defined in my_application.c */
extern void MyApp_SetIoEventCallback(void (*)(unsigned int, unsigned int, int));
extern void MyApp_SetOutputDataCallback(void (*)(const EipUint8*, int));
extern void MyApp_SetIoSizes(int input_size, int output_size);

/* The global end-stack flag used by OpENer's network handler.
 * Defined in my_application.c since we replace OpENer's platform main.c. */
extern volatile int g_end_stack;
}

/* ============================================================
 * Static callback trampolines (C callbacks -> Qt signals)
 * ============================================================ */
static EipTargetWorker *s_instance = nullptr;

static void sIoEventCb(unsigned int outAsm, unsigned int inAsm, int event)
{
    if (s_instance) {
        QByteArray empty;
        QMetaObject::invokeMethod(s_instance, [=](){
            emit s_instance->ioConnectionEvent(outAsm, inAsm, event);
        }, Qt::QueuedConnection);
    }
}

static void sOutputDataCb(const EipUint8 *data, int size)
{
    if (s_instance) {
        QByteArray ba(reinterpret_cast<const char*>(data), size);
        QMetaObject::invokeMethod(s_instance, [=](){
            {
                QMutexLocker lock(&s_instance->m_dataMutex);
                s_instance->m_outputData = ba;
            }
            emit s_instance->outputDataReceived(ba);
        }, Qt::QueuedConnection);
    }
}

/* ============================================================
 * BringupNetwork / ShutdownNetwork stubs
 * (Windows: network is already configured by OS)
 * (Linux: we don't reconfigure interfaces either)
 * ============================================================ */
#ifdef _WIN32
#define BringupNetwork(if_name, method, if_cfg, hostname) (0)
#define ShutdownNetwork(if_name) (0)
#else
/* On Linux, OpENer POSIX port has BringupNetwork/ShutdownNetwork
   but we skip them for simplicity — user manages their own IP. */
#define BringupNetwork(if_name, method, if_cfg, hostname) (0)
#define ShutdownNetwork(if_name) (0)
#endif

/* ============================================================
 * Constructor / Destructor
 * ============================================================ */
EipTargetWorker::EipTargetWorker(QObject *parent)
    : QObject(parent)
{
    m_inputData.fill(0, m_inputSize);
    m_outputData.fill(0, m_outputSize);
}

EipTargetWorker::~EipTargetWorker()
{
    stop();
    if (m_thread.isRunning()) {
        m_thread.quit();
        m_thread.wait(5000);
    }
}

QByteArray EipTargetWorker::inputData() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_inputData;
}

QByteArray EipTargetWorker::outputData() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_outputData;
}

void EipTargetWorker::setInputData(const QByteArray &data)
{
    QMutexLocker lock(&m_dataMutex);
    int sz = qMin(data.size(), m_inputSize);
    memcpy(g_input_data, data.constData(), sz);
    if (sz < m_inputSize)
        memset(g_input_data + sz, 0, m_inputSize - sz);
    m_inputData = data.left(m_inputSize);
}

void EipTargetWorker::setIoSizes(int inputSize, int outputSize)
{
    m_inputSize = qBound(1, inputSize, 512);
    m_outputSize = qBound(1, outputSize, 512);
    m_inputData.fill(0, m_inputSize);
    m_outputData.fill(0, m_outputSize);
}

/* ============================================================
 * Start / Stop
 * ============================================================ */
void EipTargetWorker::start(const QString &ifaceIndex)
{
    if (m_running.load()) return;

    m_stopFlag.store(0);
    QString iface = ifaceIndex;

    // Run the blocking OpENer loop in a std::thread-like manner via QThread
    QThread *t = QThread::create([this, iface]() {
        run(iface);
    });
    t->setObjectName(QStringLiteral("OpENerThread"));
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void EipTargetWorker::stop()
{
    m_stopFlag.store(1);
    /* OpENer checks g_end_stack to exit its loop */
    g_end_stack = 1;
}

/* ============================================================
 * OpENer main loop (runs in worker thread)
 * ============================================================ */
void EipTargetWorker::run(const QString &ifaceIndex)
{
    s_instance = this;
    m_running.store(true);

    QByteArray ifaceBytes = ifaceIndex.toLocal8Bit();
    const char *ifaceName = ifaceBytes.constData();

    emit logMessage(QStringLiteral("OpENer: Initializing..."));

    /* 1. Init connection list */
    DoublyLinkedListInitialize(&connection_list,
                               CipConnectionObjectListArrayAllocator,
                               CipConnectionObjectListArrayFree);

    /* 2. Get MAC address */
    uint8_t mac_address[6] = {0};
    if (kEipStatusError == IfaceGetMacAddress(ifaceName, mac_address)) {
        emit errorOccurred(QStringLiteral("Network interface '%1' not found")
                               .arg(ifaceIndex));
        m_running.store(false);
        return;
    }

    emit logMessage(QStringLiteral("MAC: %1-%2-%3-%4-%5-%6")
                        .arg(mac_address[0], 2, 16, QLatin1Char('0'))
                        .arg(mac_address[1], 2, 16, QLatin1Char('0'))
                        .arg(mac_address[2], 2, 16, QLatin1Char('0'))
                        .arg(mac_address[3], 2, 16, QLatin1Char('0'))
                        .arg(mac_address[4], 2, 16, QLatin1Char('0'))
                        .arg(mac_address[5], 2, 16, QLatin1Char('0')));

    /* 3. Device serial number */
    SetDeviceSerialNumber(123456789);

    /* 4. Set I/O sizes and init CIP stack */
    MyApp_SetIoSizes(m_inputSize, m_outputSize);
    srand(static_cast<unsigned int>(time(nullptr)));
    EipUint16 unique_connection_id = static_cast<EipUint16>(rand());
    CipStackInit(unique_connection_id);

    /* 5. Set MAC */
    CipEthernetLinkSetMac(mac_address);

    /* 6. Hostname */
    GetHostName(&g_tcpip.hostname);

    /* 7. NV data */
    NvdataLoad();

    /* 8. Static IP mode */
    g_tcpip.config_control = 0x00;

    /* 9. BringupNetwork (no-op) */
    BringupNetwork(ifaceName, g_tcpip.config_control,
                   &g_tcpip.interface_configuration, &g_tcpip.hostname);

    /* 10. Read IP config */
    if (kEipStatusOk == IfaceGetConfiguration(ifaceName,
                                              &g_tcpip.interface_configuration)) {
        CipTcpIpInterfaceConfiguration *cfg = &g_tcpip.interface_configuration;
        QString ipStr = QStringLiteral("%1.%2.%3.%4")
                            .arg(cfg->ip_address & 0xFF)
                            .arg((cfg->ip_address >> 8) & 0xFF)
                            .arg((cfg->ip_address >> 16) & 0xFF)
                            .arg((cfg->ip_address >> 24) & 0xFF);
        emit logMessage(QStringLiteral("IP Address: %1").arg(ipStr));
        emit ipConfigured(ipStr);
    }

    /* Register C callbacks */
    MyApp_SetIoEventCallback(sIoEventCb);
    MyApp_SetOutputDataCallback(sOutputDataCb);

    g_end_stack = 0;

    emit logMessage(QStringLiteral("OpENer: Listening on TCP:44818, UDP:44818, UDP:2222"));

    /* 11. Init network handler */
    if (kEipStatusOk != NetworkHandlerInitialize()) {
        emit errorOccurred(QStringLiteral("NetworkHandlerInitialize() failed"));
        ShutdownCipStack();
        m_running.store(false);
        return;
    }

    emit started();

    /* 12. Event loop */
    while (!m_stopFlag.load() && g_end_stack == 0) {
        if (kEipStatusOk != NetworkHandlerProcessCyclic()) {
            emit logMessage(QStringLiteral("OpENer: NetworkHandlerProcessCyclic() error"));
            break;
        }

        /* Update input data from Qt side -> OpENer buffer */
        {
            QMutexLocker lock(&m_dataMutex);
            if (!m_inputData.isEmpty()) {
                int sz = qMin(m_inputData.size(), m_inputSize);
                memcpy(g_input_data, m_inputData.constData(), sz);
            }
        }
    }

    /* 13. Cleanup */
    MyApp_SetIoEventCallback(nullptr);
    MyApp_SetOutputDataCallback(nullptr);

    NetworkHandlerFinish();
    ShutdownCipStack();
    ShutdownNetwork(ifaceName);

    m_running.store(false);
    s_instance = nullptr;

    emit logMessage(QStringLiteral("OpENer: Stopped"));
    emit stopped();
}
