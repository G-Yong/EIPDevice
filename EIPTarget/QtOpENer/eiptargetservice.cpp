/*******************************************************************************
 * eiptargetservice.cpp - Reusable EtherNet/IP Target (Adapter) service
 ******************************************************************************/

#include "eiptargetservice.h"
#include "eiptargetworker.h"

#include <QNetworkInterface>
#include <QDate>
#include <QTime>
#include <QTextStream>
#include <QMap>
#include <QSet>

extern "C" {
#include "devicedata.h"
}

EipTargetService::EipTargetService(QObject *parent)
    : QObject(parent)
{
    m_worker = new EipTargetWorker(this);

    // Forward worker signals
    connect(m_worker, &EipTargetWorker::started,
            this, &EipTargetService::started);
    connect(m_worker, &EipTargetWorker::stopped,
            this, &EipTargetService::stopped);
    connect(m_worker, &EipTargetWorker::errorOccurred,
            this, &EipTargetService::errorOccurred);
    connect(m_worker, &EipTargetWorker::logMessage,
            this, &EipTargetService::logMessage);
    connect(m_worker, &EipTargetWorker::ioConnectionEvent,
            this, &EipTargetService::ioConnectionEvent);
    connect(m_worker, &EipTargetWorker::outputDataReceived,
            this, &EipTargetService::outputDataReceived);
    connect(m_worker, &EipTargetWorker::ipConfigured,
            this, &EipTargetService::ipConfigured);
}

EipTargetService::~EipTargetService()
{
    stop();
}

bool EipTargetService::isRunning() const
{
    return m_worker->isRunning();
}

void EipTargetService::start(const QString &ifaceIndex)
{
    m_worker->setIoSizes(m_config.inputSize, m_config.outputSize);
    m_worker->start(ifaceIndex);
}

void EipTargetService::stop()
{
    m_worker->stop();
}

void EipTargetService::setDeviceIdentity(const QString &vendorName, quint16 vendorID,
                                         const QString &productName, quint16 productCode,
                                         quint8 majorRev, quint8 minorRev)
{
    m_worker->setDeviceIdentity(vendorName, vendorID, productName, productCode,
                                majorRev, minorRev);
}

QByteArray EipTargetService::inputData() const
{
    return m_worker->inputData();
}

QByteArray EipTargetService::outputData() const
{
    return m_worker->outputData();
}

void EipTargetService::setInputData(const QByteArray &data)
{
    qDebug() << "set input data" << data;
    m_worker->setInputData(data);
}

/* static */
QList<EipNetworkInterface> EipTargetService::availableInterfaces()
{
    QList<EipNetworkInterface> result;
    const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;

        QString ipStr;
        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ipStr = e.ip().toString();
                break;
            }
        }
        if (ipStr.isEmpty()) continue;

        EipNetworkInterface ni;
        ni.index = iface.index();
        ni.name  = iface.humanReadableName();
        ni.ipAddress = ipStr;
        ni.displayLabel = QStringLiteral("%1 (%2) [idx:%3]")
                              .arg(ni.name).arg(ni.ipAddress).arg(ni.index);
#ifdef Q_OS_WIN
        ni.openerIdent = QString::number(ni.index);
#else
        ni.openerIdent = iface.name();  // Linux: system name like "eth0"
#endif
        result.append(ni);
    }
    return result;
}

// ---- CIP Data Type helpers ----

static int cipTypeByteSize(quint8 cipType)
{
    switch (cipType) {
    case 0xC1: return 1;  // BOOL (stored as 1 byte in Param)
    case 0xC2: return 1;  // SINT
    case 0xC3: return 2;  // INT
    case 0xC4: return 4;  // DINT
    case 0xC5: return 8;  // LINT
    case 0xC6: return 1;  // USINT
    case 0xC7: return 2;  // UINT
    case 0xC8: return 4;  // UDINT
    case 0xC9: return 8;  // ULINT
    case 0xCA: return 4;  // REAL
    case 0xCB: return 8;  // LREAL
    case 0xD1: return 1;  // BYTE
    case 0xD2: return 2;  // WORD
    case 0xD3: return 4;  // DWORD
    case 0xD4: return 8;  // LWORD
    default:   return 1;
    }
}

static const char *cipTypeName(quint8 cipType)
{
    switch (cipType) {
    case 0xC1: return "BOOL";
    case 0xC2: return "SINT";
    case 0xC3: return "INT";
    case 0xC4: return "DINT";
    case 0xC5: return "LINT";
    case 0xC6: return "USINT";
    case 0xC7: return "UINT";
    case 0xC8: return "UDINT";
    case 0xC9: return "ULINT";
    case 0xCA: return "REAL";
    case 0xCB: return "LREAL";
    case 0xD1: return "BYTE";
    case 0xD2: return "WORD";
    case 0xD3: return "DWORD";
    case 0xD4: return "LWORD";
    default:   return "BYTE";
    }
}

/* static */
QString EipTargetService::generateEds(const QList<EdsAssemblyMember> &inputMembers,
                                      const QList<EdsAssemblyMember> &outputMembers,
                                      const QString &vendorName, quint16 vendorID,
                                      const QString &productName, quint16 productCode,
                                      quint8 majorRev, quint8 minorRev)
{
    // Calculate assembly sizes from member bit lengths
    int inBits = 0, outBits = 0;
    for (const auto &m : inputMembers)  inBits  += m.bitLen;
    for (const auto &m : outputMembers) outBits += m.bitLen;
    int inSz  = qMax(1, (inBits  + 7) / 8);
    int outSz = qMax(1, (outBits + 7) / 8);

    // Assign a unique Param number for each variable (one Param per variable)
    // Layout: input members first, then output members, then config/RPI/PIT
    struct ParamEntry {
        int paramNum;
        quint8 cipType;
        QString name;
    };
    QList<ParamEntry> inputParams;
    QList<ParamEntry> outputParams;
    int paramNum = 1;

    // Config param comes first (Param1), matching official convention
    int configParamNum = paramNum++;

    for (const auto &m : inputMembers) {
        inputParams.append({paramNum++, m.cipType, m.name});
    }
    for (const auto &m : outputMembers) {
        outputParams.append({paramNum++, m.cipType, m.name});
    }

    QString eds;
    QTextStream ts(&eds);

    ts << "$ EDS file generated by QtEipTarget\n";
    ts << "$ Input(T->O) Assembly 100: " << inSz << " bytes\n";
    ts << "$ Output(O->T) Assembly 150: " << outSz << " bytes\n";
    ts << "$ Config Assembly 151: 10 bytes\n\n";

    // [File]
    ts << "[File]\n";
    ts << "        DescText = \"" << productName << " EtherNet/IP Adapter\";\n";
    ts << "        CreateDate = " << QDate::currentDate().toString("MM-dd-yyyy") << ";\n";
    ts << "        CreateTime = " << QTime::currentTime().toString("HH:mm:ss") << ";\n";
    ts << "        ModDate = " << QDate::currentDate().toString("MM-dd-yyyy") << ";\n";
    ts << "        ModTime = " << QTime::currentTime().toString("HH:mm:ss") << ";\n";
    ts << "        Revision = 2.3;\n";
    ts << "        HomeURL = \"\";\n\n";

    // [Device]
    ts << "[Device]\n";
    ts << "        VendCode = " << vendorID << ";\n";
    ts << "        VendName = \"" << vendorName << "\";\n";
    ts << "        ProdType = " << OPENER_DEVICE_TYPE << ";\n";
    ts << "        ProdTypeStr = \"Communications Adapter\";\n";
    ts << "        ProdCode = " << productCode << ";\n";
    ts << "        MajRev = " << majorRev << ";\n";
    ts << "        MinRev = " << minorRev << ";\n";
    ts << "        ProdName = \"" << productName << "\";\n";
    ts << "        Catalog = \"" << productName << "\";\n\n";

    ts << "[Device Classification]\n";
    ts << "        Class1 = EtherNetIP;\n\n";

    // [Params] — one Param per variable, matching official EDS convention
    ts << "[Params]\n";

    // Config Param (Param1)
    ts << "        Param" << configParamNum << " =\n";
    ts << "                0,                      $ reserved, shall equal 0\n";
    ts << "                ,,                      $ Link Path Size, Link Path\n";
    ts << "                0x0000,                 $ Descriptor\n";
    ts << "                0xD2,                   $ Data Type\n";
    ts << "                2,                      $ Data Size in bytes\n";
    ts << "                \"Config Parameter\",     $ name\n";
    ts << "                \"\",                     $ units\n";
    ts << "                \"This is a dummy parameter to satisfy rockwell tools...\",$ help string\n";
    ts << "                0,65535,0,              $ min, max, default data values\n";
    ts << "                ,,,,                    $ mult, div, base, offset scaling\n";
    ts << "                ,,,,                    $ mult, div, base, offset links\n";
    ts << "                ;                       $ decimal places\n";

    // Helper: write one Param definition per variable
    auto writeParam = [&](const ParamEntry &pe) {
        int bs = cipTypeByteSize(pe.cipType);
        ts << "        Param" << pe.paramNum << " =\n";
        ts << "                0,                      $ reserved, shall equal 0\n";
        ts << "                ,,                      $ Link Path Size, Link Path\n";
        ts << "                0x0000,                 $ Descriptor\n";
        ts << "                0x" << QString::number(pe.cipType, 16).toUpper()
           << ",                   $ Data Type\n";
        ts << "                " << bs
           << ",                      $ Data Size in bytes\n";
        ts << "                \"" << pe.name << "\",";
        // Pad to align the comment
        int pad = qMax(1, 24 - pe.name.length() - 2);
        for (int p = 0; p < pad; ++p) ts << ' ';
        ts << "$ name\n";
        ts << "                \"\",                     $ units\n";
        ts << "                \"\",                     $ help string\n";
        ts << "                ,,0,                    $ min, max, default data values\n";
        ts << "                ,,,,                    $ mult, div, base, offset scaling\n";
        ts << "                ,,,,                    $ mult, div, base, offset links\n";
        ts << "                ;                       $ decimal places\n";
        // BOOL variables get an Enum definition
        if (pe.cipType == 0xC1) {
            ts << "        Enum" << pe.paramNum << " =\n";
            ts << "                0, \"FALSE\",\n";
            ts << "                1, \"TRUE\";\n";
        }
    };

    for (const auto &pe : inputParams)  writeParam(pe);
    for (const auto &pe : outputParams) writeParam(pe);
    ts << "\n";

    // [Assembly]
    ts << "[Assembly]\n";
    ts << "        Object_Name = \"Assembly Object\";\n";
    ts << "        Object_Class_Code = 0x04;\n";

    // Helper: write assembly members
    // For BOOL: explicit "1,ParamN"; for others: ",ParamN" (omit size, use Param default)
    auto writeAssemblyMembers = [&](const QList<EdsAssemblyMember> &members,
                                    const QList<ParamEntry> &params,
                                    int fallbackParamNum) {
        if (members.isEmpty()) {
            ts << "                ,Param" << fallbackParamNum << ";\n";
        } else {
            for (int i = 0; i < members.size(); ++i) {
                const auto &m = members[i];
                int pn = params[i].paramNum;
                bool isLast = (i == members.size() - 1);
                QString sep = isLast ? ";" : ",";
                if (m.cipType == 0xC1) {
                    // BOOL: explicit 1 bit
                    ts << "                1,Param" << pn << sep << "\n";
                } else {
                    // Non-BOOL: omit size, use Param's Data Size
                    ts << "                ,Param" << pn << sep << "\n";
                }
            }
        }
    };

    // Assem100 = Input (T->O)
    ts << "        Assem100 =\n";
    ts << "                \"Input Assembly\",\n";
    ts << "                ,\n";
    ts << "                " << inSz << ",\n";
    ts << "                0x0000,\n";
    ts << "                ,,\n";
    writeAssemblyMembers(inputMembers, inputParams, configParamNum);

    // Assem150 = Output (O->T)
    ts << "        Assem150 =\n";
    ts << "                \"Output Assembly\",\n";
    ts << "                ,\n";
    ts << "                " << outSz << ",\n";
    ts << "                0x0000,\n";
    ts << "                ,,\n";
    writeAssemblyMembers(outputMembers, outputParams, configParamNum);

    // Assem151 = Config (fixed 10 bytes)
    ts << "        Assem151 =\n";
    ts << "                \"Config Assembly\",\n";
    ts << "                ,\n";
    ts << "                10,\n";
    ts << "                0x0000,\n";
    ts << "                ,,\n";
    ts << "                ,Param" << configParamNum << ";\n";
    ts << "\n";

    // [Connection Manager]
    ts << "[Connection Manager]\n";
    ts << "        Object_Name = \"Connection Manager Object\";\n";
    ts << "        Object_Class_Code = 0x06;\n";

    ts << "        Connection1 =\n";
    ts << "                0x04030002,             $ trigger/transport: exclusive-owner, cyclic+COS, class 1\n";
    ts << "                0xFF640405,             $ O->T: fixed+variable, P2P; T->O: fixed+variable, multicast+P2P\n";
    ts << "                ,,Assem150,             $ O->T RPI, size, format\n";
    ts << "                ,,Assem100,             $ T->O RPI, size, format\n";
    ts << "                ,,                      $ proxy config size, format\n";
    ts << "                10,Assem151,            $ target config size, format\n";
    ts << "                \"Exclusive Owner\",      $ Connection Name\n";
    ts << "                \"\",                     $ help string\n";
    ts << "                \"20 04 24 97 2C 96 2C 64\";    $ Path\n";

    ts << "        Connection2 =\n";
    ts << "                0x02030002,             $ trigger/transport: input-only, cyclic+COS, class 1\n";
    ts << "                0xFF640305,             $ O->T: fixed+variable; T->O: fixed+variable, multicast+P2P\n";
    ts << "                ,0,,                    $ O->T RPI, size=0 (heartbeat), format\n";
    ts << "                ,,Assem100,             $ T->O RPI, size, format\n";
    ts << "                ,,                      $ proxy config size, format\n";
    ts << "                ,,                      $ target config size, format\n";
    ts << "                \"Input Only\",           $ Connection Name\n";
    ts << "                \"\",                     $ help string\n";
    ts << "                \"20 04 24 97 2C 98 2C 64\";    $ Path\n";

    ts << "        Connection3 =\n";
    ts << "                0x01030002,             $ trigger/transport: listen-only, cyclic+COS, class 1\n";
    ts << "                0xFF640305,             $ O->T: fixed+variable; T->O: fixed+variable, multicast+P2P\n";
    ts << "                ,0,,                    $ O->T RPI, size=0 (heartbeat), format\n";
    ts << "                ,,Assem100,             $ T->O RPI, size, format\n";
    ts << "                ,,                      $ proxy config size, format\n";
    ts << "                ,,                      $ target config size, format\n";
    ts << "                \"Listen Only\",          $ Connection Name\n";
    ts << "                \"\",                     $ help string\n";
    ts << "                \"20 04 24 97 2C 99 2C 64\";    $ Path\n";
    ts << "\n";

    // [Capacity]
    ts << "[Capacity]\n";
    ts << "        TSpec1 = TxRx, 1, 1000;\n";
    ts << "        TSpec2 = TxRx, " << qMax(inSz, outSz) << ", 1000;\n\n";

    ts << "[TCP/IP Interface Class]\n";
    ts << "        Revision = 4;\n";
    ts << "        Object_Name = \"TCP/IP Interface Object\";\n";
    ts << "        Object_Class_Code = 0xF5;\n";
    ts << "        MaxInst = 1;\n";
    ts << "        Number_Of_Static_Instances = 1;\n";
    ts << "        Max_Number_Of_Dynamic_Instances = 0;\n\n";

    ts << "[Ethernet Link Class]\n";
    ts << "        Revision = 4;\n";
    ts << "        Object_Name = \"Ethernet Link Object\";\n";
    ts << "        Object_Class_Code = 0xF6;\n";
    ts << "        MaxInst = 1;\n";
    ts << "        Number_Of_Static_Instances = 1;\n";
    ts << "        Max_Number_Of_Dynamic_Instances = 0;\n\n";

    ts << "[Identity Class]\n";
    ts << "        Revision = 1;\n";
    ts << "        Object_Name = \"Identity Object\";\n";
    ts << "        Object_Class_Code = 0x01;\n";
    ts << "        MaxInst = 1;\n";
    ts << "        Number_Of_Static_Instances = 1;\n";
    ts << "        Max_Number_Of_Dynamic_Instances = 0;\n\n";

    ts << "[QoS Class]\n";
    ts << "        Revision = 1;\n";
    ts << "        Object_Name = \"QoS Object\";\n";
    ts << "        Object_Class_Code = 0x48;\n";
    ts << "        MaxInst = 1;\n";
    ts << "        Number_Of_Static_Instances = 1;\n";
    ts << "        Max_Number_Of_Dynamic_Instances = 0;\n";

    return eds;
}
