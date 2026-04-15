/*******************************************************************************
 * edsparser.cpp - EDS (Electronic Data Sheet) File Parser
 ******************************************************************************/

#include "edsparser.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

EdsParser::EdsParser() {}

bool EdsParser::load(const QString &filepath)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    m_filePath = filepath;
    m_sections.clear();
    m_device = EdsDeviceInfo();
    m_assemblies.clear();
    m_connections.clear();
    m_params.clear();

    QTextStream in(&file);
    in.setCodec("UTF-8");
    QString content = in.readAll();
    file.close();

    parseSections(content);
    parseDevice();
    parseParams();
    parseAssemblies();
    parseConnections();
    return true;
}

/* ============================================================
 * Section splitting
 * ============================================================ */
void EdsParser::parseSections(const QString &content)
{
    static const QRegularExpression reSectionHead(QStringLiteral("^\\[(.+?)\\]\\s*$"));

    QString currentSection;
    QStringList lines;

    const QStringList allLines = content.split(QLatin1Char('\n'));
    for (const QString &rawLine : allLines) {
        // Strip $ comments
        QString line = rawLine.section(QLatin1Char('$'), 0, 0).trimmed();
        QRegularExpressionMatch m = reSectionHead.match(line);
        if (m.hasMatch()) {
            if (!currentSection.isEmpty())
                m_sections[currentSection] = lines;
            currentSection = m.captured(1);
            lines.clear();
        } else {
            if (!currentSection.isEmpty() && !line.isEmpty())
                lines.append(line);
        }
    }
    if (!currentSection.isEmpty())
        m_sections[currentSection] = lines;
}

/* ============================================================
 * [Device] section
 * ============================================================ */
void EdsParser::parseDevice()
{
    QStringList lines = m_sections.value(QStringLiteral("Device"));
    QString text = lines.join(QLatin1Char(' '));

    static const QRegularExpression reInt(QStringLiteral("(\\w+)\\s*=\\s*(\\d+)"));
    static const QRegularExpression reStr(QStringLiteral("(\\w+)\\s*=\\s*\"([^\"]*)\""));

    auto matchInt = [&](const QString &key) -> int {
        QRegularExpressionMatchIterator it = reInt.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            if (m.captured(1) == key)
                return m.captured(2).toInt();
        }
        return -1;
    };
    auto matchStr = [&](const QString &key) -> QString {
        QRegularExpressionMatchIterator it = reStr.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            if (m.captured(1) == key)
                return m.captured(2);
        }
        return {};
    };

    m_device.vendCode = matchInt(QStringLiteral("VendCode"));
    m_device.vendName = matchStr(QStringLiteral("VendName"));
    m_device.prodType = matchInt(QStringLiteral("ProdType"));
    m_device.prodCode = matchInt(QStringLiteral("ProdCode"));
    m_device.majRev   = matchInt(QStringLiteral("MajRev"));
    m_device.minRev   = matchInt(QStringLiteral("MinRev"));
    m_device.prodName = matchStr(QStringLiteral("ProdName"));
}

/* ============================================================
 * [Params] section
 * ============================================================ */
void EdsParser::parseParams()
{
    QStringList lines = m_sections.value(QStringLiteral("Params"));
    QString text = lines.join(QLatin1Char('\n'));

    static const QRegularExpression reParam(QStringLiteral("(Param\\d+)\\s*=\\s*(.*?);"),
                                            QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator it = reParam.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QString paramName = m.captured(1);
        QStringList fields = m.captured(2).split(QLatin1Char(','));
        for (QString &f : fields) f = f.trimmed();

        EdsParam param;
        param.name = paramName;
        if (fields.size() > 3)  param.dataType    = safeInt(fields[3]);
        if (fields.size() > 4)  param.dataSize    = safeInt(fields[4]);
        if (fields.size() > 5)  param.displayName = stripQuotes(fields[5]);
        if (fields.size() > 8)  param.minVal      = safeInt(fields[8]);
        if (fields.size() > 9)  param.maxVal      = safeInt(fields[9]);
        if (fields.size() > 10) param.defaultVal   = safeInt(fields[10]);
        m_params[paramName] = param;
    }
}

/* ============================================================
 * [Assembly] section
 * ============================================================ */
void EdsParser::parseAssemblies()
{
    QStringList lines = m_sections.value(QStringLiteral("Assembly"));
    QString text = lines.join(QLatin1Char('\n'));

    static const QRegularExpression reAssem(QStringLiteral("Assem(\\d+)\\s*=\\s*(.*?);"),
                                            QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator it = reAssem.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        int instId = m.captured(1).toInt();
        QStringList fields = m.captured(2).split(QLatin1Char(','));
        for (QString &f : fields) f = f.trimmed();

        EdsAssembly asm_;
        asm_.instance = instId;
        if (fields.size() > 0) asm_.name = stripQuotes(fields[0]);
        if (fields.size() > 1) asm_.path = stripQuotes(fields[1]);
        if (fields.size() > 2) asm_.size = safeInt(fields[2]);
        if (fields.size() > 3) {
            int dirVal = safeInt(fields[3]);
            if (dirVal == 0)      asm_.direction = QStringLiteral("input");
            else if (dirVal == 1) asm_.direction = QStringLiteral("output");
            else                  asm_.direction = QStringLiteral("unknown");
        }
        m_assemblies[instId] = asm_;
    }
}

/* ============================================================
 * [Connection Manager] section
 * ============================================================ */
void EdsParser::parseConnections()
{
    QStringList lines = m_sections.value(QStringLiteral("Connection Manager"));
    QString text = lines.join(QLatin1Char('\n'));

    static const QRegularExpression reConn(QStringLiteral("(Connection\\d+)\\s*=\\s*(.*?);"),
                                           QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator it = reConn.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QString connName = m.captured(1);
        QStringList fields = m.captured(2).split(QLatin1Char(','));
        for (QString &f : fields) f = f.trimmed();

        EdsConnection conn;
        conn.name = connName;
        if (fields.size() > 0) conn.transportMask = safeInt(fields[0]);
        if (fields.size() > 1) conn.connParams    = safeInt(fields[1]);

        // Connection type from transport_mask
        QStringList types;
        if (conn.transportMask & (1LL << 26)) types << QStringLiteral("Exclusive Owner");
        if (conn.transportMask & (1LL << 25)) types << QStringLiteral("Input Only");
        if (conn.transportMask & (1LL << 24)) types << QStringLiteral("Listen Only");
        conn.type = types.isEmpty() ? QStringLiteral("Unknown") : types.join(QStringLiteral(", "));

        // O→T assembly (field 4)
        if (fields.size() > 4) conn.otAssembly  = extractAssemRef(fields[4]);
        // T→O assembly (field 7)
        if (fields.size() > 7) conn.toAssembly  = extractAssemRef(fields[7]);
        // Config assembly (field 11, fallback field 9)
        if (fields.size() > 11) {
            conn.cfgAssembly = extractAssemRef(fields[11]);
            if (conn.cfgAssembly < 0 && fields.size() > 9)
                conn.cfgAssembly = extractAssemRef(fields[9]);
        }
        // Display name (field 12)
        if (fields.size() > 12) conn.displayName = stripQuotes(fields[12]);

        m_connections[connName] = conn;
    }
}

/* ============================================================
 * I/O config extraction
 * ============================================================ */
EdsIoConfig EdsParser::getIoConfig() const
{
    EdsIoConfig cfg;

    // Find Exclusive Owner connection first
    const EdsConnection *exConn = nullptr;
    for (auto it = m_connections.constBegin(); it != m_connections.constEnd(); ++it) {
        if (it->type.contains(QStringLiteral("Exclusive Owner"))) {
            exConn = &it.value();
            break;
        }
    }
    if (!exConn && !m_connections.isEmpty())
        exConn = &m_connections.constBegin().value();

    // Collect from Assembly section
    const EdsAssembly *inputAsm  = nullptr;
    const EdsAssembly *outputAsm = nullptr;
    const EdsAssembly *configAsm = nullptr;

    for (auto it = m_assemblies.constBegin(); it != m_assemblies.constEnd(); ++it) {
        if (it->direction == QStringLiteral("input") && !inputAsm)
            inputAsm = &it.value();
        else if (it->direction == QStringLiteral("output") && !outputAsm)
            outputAsm = &it.value();
    }

    // Override with Connection Manager info (more precise)
    if (exConn) {
        if (exConn->toAssembly >= 0 && m_assemblies.contains(exConn->toAssembly))
            inputAsm = &m_assemblies[exConn->toAssembly];
        if (exConn->otAssembly >= 0 && m_assemblies.contains(exConn->otAssembly))
            outputAsm = &m_assemblies[exConn->otAssembly];
        if (exConn->cfgAssembly >= 0 && m_assemblies.contains(exConn->cfgAssembly))
            configAsm = &m_assemblies[exConn->cfgAssembly];
    }

    // Fallback: find config assembly by name
    if (!configAsm) {
        for (auto it = m_assemblies.constBegin(); it != m_assemblies.constEnd(); ++it) {
            if (it->direction != QStringLiteral("input") &&
                it->direction != QStringLiteral("output") &&
                it->name.toLower().contains(QStringLiteral("config"))) {
                configAsm = &it.value();
                break;
            }
        }
    }

    if (inputAsm) {
        cfg.inputAssembly = inputAsm->instance;
        cfg.inputSize     = inputAsm->size > 0 ? inputAsm->size : 32;
    }
    if (outputAsm) {
        cfg.outputAssembly = outputAsm->instance;
        cfg.outputSize     = outputAsm->size > 0 ? outputAsm->size : 32;
    }
    if (configAsm) {
        cfg.configAssembly = configAsm->instance;
        cfg.configSize     = configAsm->size;
    }

    // RPI from Params
    for (auto it = m_params.constBegin(); it != m_params.constEnd(); ++it) {
        if (it->displayName.toUpper() == QStringLiteral("RPI")) {
            if (it->defaultVal > 0)
                cfg.rpiUs = it->defaultVal;
            break;
        }
    }

    return cfg;
}

/* ============================================================
 * Device matching
 * ============================================================ */
EdsParser::MatchResult EdsParser::matchDevice(quint16 vendorId, quint16 deviceType,
                                               quint16 productCode, quint8 revMajor) const
{
    MatchResult result;
    struct Check { int edsVal; int devVal; const char *desc; };
    Check checks[] = {
        { m_device.vendCode, vendorId,    "厂商 ID" },
        { m_device.prodType, deviceType,  "设备类型" },
        { m_device.prodCode, productCode, "产品代码" },
        { m_device.majRev,   revMajor,    "主版本号" },
    };
    for (auto &c : checks) {
        if (c.edsVal >= 0 && c.devVal >= 0 && c.edsVal != c.devVal) {
            result.matched = false;
            result.mismatches << QStringLiteral("%1: EDS=%2, 设备=%3")
                                     .arg(QString::fromUtf8(c.desc))
                                     .arg(c.edsVal)
                                     .arg(c.devVal);
        }
    }
    return result;
}

/* ============================================================
 * Summary
 * ============================================================ */
QString EdsParser::summary() const
{
    QStringList lines;
    lines << QStringLiteral("产品: %1 (Vendor=%2, Type=%3, Code=%4, Rev=%5.%6)")
                 .arg(m_device.prodName)
                 .arg(m_device.vendCode)
                 .arg(m_device.prodType)
                 .arg(m_device.prodCode)
                 .arg(m_device.majRev)
                 .arg(m_device.minRev);
    lines << QStringLiteral("Assembly 实例: %1 个").arg(m_assemblies.size());
    for (auto it = m_assemblies.constBegin(); it != m_assemblies.constEnd(); ++it) {
        lines << QStringLiteral("  Assem%1: %2 (%3 bytes, %4)")
                     .arg(it->instance)
                     .arg(it->name)
                     .arg(it->size)
                     .arg(it->direction);
    }
    lines << QStringLiteral("连接定义: %1 个").arg(m_connections.size());
    for (auto it = m_connections.constBegin(); it != m_connections.constEnd(); ++it) {
        lines << QStringLiteral("  %1: %2")
                     .arg(it->name,
                          it->displayName.isEmpty() ? it->type : it->displayName);
    }
    return lines.join(QLatin1Char('\n'));
}

/* ============================================================
 * Helpers
 * ============================================================ */
int EdsParser::safeInt(const QString &s)
{
    QString t = stripQuotes(s.trimmed());
    if (t.isEmpty()) return 0;
    bool ok;
    if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        int v = t.mid(2).toInt(&ok, 16);
        return ok ? v : 0;
    }
    int v = t.toInt(&ok);
    return ok ? v : 0;
}

QString EdsParser::stripQuotes(const QString &s)
{
    QString t = s.trimmed();
    if (t.size() >= 2) {
        if ((t.startsWith(QLatin1Char('"'))  && t.endsWith(QLatin1Char('"'))) ||
            (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\'')))) {
            return t.mid(1, t.size() - 2);
        }
    }
    return t;
}

int EdsParser::extractAssemRef(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("Assem(\\d+)"));
    QRegularExpressionMatch m = re.match(s);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}
