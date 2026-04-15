/*******************************************************************************
 * edsparser.h - EDS (Electronic Data Sheet) File Parser
 *
 * 解析 EDS 文件，提取设备信息、Assembly 定义、连接参数等，
 * 用于自动配置 I/O 连接和 Assembly 读写参数。
 ******************************************************************************/
#ifndef EDSPARSER_H
#define EDSPARSER_H

#include <QString>
#include <QMap>
#include <QStringList>
#include <QVariant>

struct EdsAssembly {
    int      instance  = 0;
    QString  name;
    QString  path;
    int      size      = 0;
    QString  direction;   // "input", "output", "unknown"
    int      memberCount = 0;
};

struct EdsConnection {
    QString  name;
    qint64   transportMask = 0;
    qint64   connParams    = 0;
    QString  type;         // "Exclusive Owner", "Input Only", etc.
    int      otAssembly    = -1;
    int      toAssembly    = -1;
    int      cfgAssembly   = -1;
    QString  displayName;
};

struct EdsParam {
    QString  name;
    int      dataType = 0;
    int      dataSize = 0;
    QString  displayName;
    int      minVal   = 0;
    int      maxVal   = 0;
    int      defaultVal = 0;
};

struct EdsDeviceInfo {
    int      vendCode  = -1;
    QString  vendName;
    int      prodType  = -1;
    int      prodCode  = -1;
    int      majRev    = -1;
    int      minRev    = -1;
    QString  prodName;
};

struct EdsIoConfig {
    int  inputAssembly   = -1;
    int  inputSize       = 32;
    int  outputAssembly  = -1;
    int  outputSize      = 32;
    int  configAssembly  = -1;
    int  configSize      = 0;
    int  rpiUs           = -1;
};

class EdsParser
{
public:
    EdsParser();

    bool load(const QString &filepath);
    QString filePath() const { return m_filePath; }

    const EdsDeviceInfo& device() const { return m_device; }
    const QMap<int, EdsAssembly>& assemblies() const { return m_assemblies; }
    const QMap<QString, EdsConnection>& connections() const { return m_connections; }

    EdsIoConfig getIoConfig() const;

    struct MatchResult {
        bool matched = true;
        QStringList mismatches;
    };
    MatchResult matchDevice(quint16 vendorId, quint16 deviceType,
                            quint16 productCode, quint8 revMajor) const;

    QString summary() const;

private:
    void parseSections(const QString &content);
    void parseDevice();
    void parseParams();
    void parseAssemblies();
    void parseConnections();

    static int safeInt(const QString &s);
    static QString stripQuotes(const QString &s);
    static int extractAssemRef(const QString &s);

    QString  m_filePath;
    QMap<QString, QStringList> m_sections;
    EdsDeviceInfo m_device;
    QMap<int, EdsAssembly> m_assemblies;
    QMap<QString, EdsConnection> m_connections;
    QMap<QString, EdsParam> m_params;
};

#endif // EDSPARSER_H
