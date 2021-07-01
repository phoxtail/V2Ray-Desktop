#include "serverconfighelper.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include "constants.h"
#include "utility.h"

QString ServerConfigHelper::getServerNameError(const QJsonObject &serverConfig,
                                               const QString *pServerName)
{
    if (pServerName != nullptr) {
        QString newServerName = serverConfig["serverName"].toString();
        if (newServerName == *pServerName) {
            return Utility::getStringConfigError(serverConfig, "serverName", tr("Server Name"));
        }
    }
    return Utility::getStringConfigError(
            serverConfig, "serverName", tr("Server Name"),
            { std::bind(&Utility::isServerNameNotUsed, std::placeholders::_1) }, false,
            tr("The '%1' has been used by another server."));
}

ServerConfigHelper::Protocol ServerConfigHelper::getProtocol(QString protocol)
{
    protocol = protocol.toLower();

    if (protocol == "vmess" || protocol == "v2ray") {
        return Protocol::VMESS;
    } else if (protocol == "shadowsocks" || protocol == "ss" || protocol == "shadowsocksr"
               || protocol == "ssr") {
        return Protocol::SHADOWSOCKS;
    } else if (protocol == "trojan") {
        return Protocol::TROJAN;
    } else {
        return Protocol::UNKNOWN;
    }
}

QStringList ServerConfigHelper::getServerConfigErrors(Protocol protocol,
                                                      const QJsonObject &serverConfig,
                                                      const QString *pServerName)
{
    if (protocol == Protocol::VMESS) {
        return getV2RayServerConfigErrors(serverConfig, pServerName);
    } else if (protocol == Protocol::SHADOWSOCKS) {
        return getShadowsocksServerConfigErrors(serverConfig, pServerName);
    } else if (protocol == Protocol::TROJAN) {
        return getTrojanServerConfigErrors(serverConfig, pServerName);
    }
    return QStringList { tr("Unknown Server protocol") };
}

QJsonObject ServerConfigHelper::getPrettyServerConfig(Protocol protocol,
                                                      const QJsonObject &serverConfig)
{
    if (protocol == Protocol::VMESS) {
        return getPrettyV2RayConfig(serverConfig);
    } else if (protocol == Protocol::SHADOWSOCKS) {
        return getPrettyShadowsocksConfig(serverConfig);
    } else if (protocol == Protocol::TROJAN) {
        return getPrettyTrojanConfig(serverConfig);
    }
    return QJsonObject {};
}

QJsonObject ServerConfigHelper::getServerConfigFromUrl(Protocol protocol, const QString &serverUrl,
                                                       const QString &subscriptionUrl)
{
    QString _serverUrl = serverUrl.trimmed();
    if (protocol == Protocol::VMESS) {
        return getV2RayServerConfigFromUrl(_serverUrl, subscriptionUrl);
    } else if (protocol == Protocol::SHADOWSOCKS) {
        if (serverUrl.startsWith("ssr://")) {
            return getShadowsocksRServerConfigFromUrl(_serverUrl, subscriptionUrl);
        } else {
            return getShadowsocksServerConfigFromUrl(_serverUrl, subscriptionUrl);
        }
    } else if (protocol == Protocol::TROJAN) {
        return getTrojanServerConfigFromUrl(_serverUrl, subscriptionUrl);
    }
    return QJsonObject {};
}

QStringList ServerConfigHelper::getV2RayServerConfigErrors(const QJsonObject &serverConfig,
                                                           const QString *serverName)
{
    QStringList errors;
    errors.append(getServerNameError(serverConfig, serverName));
    errors.append(Utility::getStringConfigError(
            serverConfig, "serverAddr", tr("Server Address"),
            {
                    std::bind(&Utility::isIpAddrValid, std::placeholders::_1),
                    std::bind(&Utility::isDomainNameValid, std::placeholders::_1),
            }));
    errors.append(Utility::getNumericConfigError(serverConfig, "serverPort", tr("Server Port"), 0,
                                                 65535));
    errors.append(Utility::getStringConfigError(serverConfig, "id", tr("ID")));
    errors.append(
            Utility::getNumericConfigError(serverConfig, "alterId", tr("Alter ID"), 0, 65535));
    errors.append(Utility::getStringConfigError(serverConfig, "security", tr("Security")));
    errors.append(Utility::getStringConfigError(serverConfig, "network", tr("Network")));
    errors.append(
            Utility::getStringConfigError(serverConfig, "networkSecurity", tr("Network Security")));
    errors.append(Utility::getStringConfigError(serverConfig, "tcpHeaderType", tr("TCP Header")));
    errors.append(getV2RayStreamSettingsErrors(serverConfig, serverConfig["network"].toString()));

    // Remove empty error messages generated by Utility::getNumericConfigError()
    // and Utility::getStringConfigError()
    errors.removeAll("");
    return errors;
}

QStringList ServerConfigHelper::getV2RayStreamSettingsErrors(const QJsonObject &serverConfig,
                                                             const QString &network)
{
    QStringList errors;
    if (network != "tcp" && network != "ws") {
        // Clash only supports tcp and ws :(
        errors.append(QString(tr("Unspoorted 'Network': %1.")).arg(network));
    }
    if (network == "ws") {
        errors.append(Utility::getStringConfigError(
                serverConfig, "networkHost", tr("Host"),
                { std::bind(&Utility::isDomainNameValid, std::placeholders::_1) }, true));
        errors.append(
                Utility::getStringConfigError(serverConfig, "networkPath", tr("Path"), {}, true));
    }
    return errors;
}

QJsonObject ServerConfigHelper::getPrettyV2RayConfig(const QJsonObject &serverConfig)
{
    QJsonObject v2RayConfig {
        { "autoConnect", serverConfig["autoConnect"].toBool() },
        { "subscription",
          serverConfig.contains("subscription") ? serverConfig["subscription"].toString() : "" },
        { "name", serverConfig["serverName"].toString() },
        { "type", "vmess" },
        { "udp", serverConfig["udp"].toBool() },
        { "server", serverConfig["serverAddr"].toString() },
        { "port", serverConfig["serverPort"].toVariant().toInt() },
        { "uuid", serverConfig["id"].toString() },
        { "alterId", serverConfig["alterId"].toVariant().toInt() },
        { "cipher", serverConfig["security"].toString().toLower() },
        { "tls", serverConfig["networkSecurity"].toString().toLower() == "tls" },
        { "skip-cert-verify", serverConfig["allowInsecure"].toBool() }
    };

    QString network = serverConfig["network"].toString();
    QString tcpHeader = serverConfig["tcpHeaderType"].toString();
    if (network == "ws") {
        v2RayConfig["network"] = "ws";
        v2RayConfig["ws-path"] = serverConfig["networkPath"].toString();
        v2RayConfig["ws-headers"] =
                QJsonObject { { "Host", serverConfig["networkHost"].toString() } };
    } else if (network == "tcp" && tcpHeader == "none") {
        v2RayConfig["network"] = "tcp";
    } else if (network == "tcp" && tcpHeader == "http") {
        v2RayConfig["network"] = "http";
        v2RayConfig["http-opts"] = QJsonObject {
            { "method", "GET" },
            { "headers",
              QJsonObject {
                      { "host",
                        QJsonArray { "www.baidu.com", "www.bing.com", "www.163.com",
                                     "www.netease.com", "www.qq.com", "www.tencent.com",
                                     "www.taobao.com", "www.tmall.com", "www.alibaba-inc.com",
                                     "www.aliyun.com", "www.sensetime.com", "www.megvii.com" } },
                      { "User-Agent", getRandomUserAgents(24) },
                      { "Accept-Encoding", QJsonArray { "gzip, deflate" } },
                      { "Connection", QJsonArray { "keep-alive" } } } },
        };
    }
    return v2RayConfig;
}

QJsonArray ServerConfigHelper::getRandomUserAgents(int n)
{
    QStringList OPERATING_SYSTEMS { "Macintosh; Intel Mac OS X 10_15", "X11; Linux x86_64",
                                    "Windows NT 10.0; Win64; x64" };
    QJsonArray userAgents;
    for (int i = 0; i < n; ++i) {
        int osIndex = std::rand() % 3;
        int chromeMajorVersion = std::rand() % 30 + 50;
        int chromeBuildVersion = std::rand() % 4000 + 1000;
        int chromePatchVersion = std::rand() % 100;
        userAgents.append(QString("Mozilla/5.0 (%1) AppleWebKit/537.36 (KHTML, "
                                  "like Gecko) Chrome/%2.0.%3.%4 Safari/537.36")
                                  .arg(OPERATING_SYSTEMS[osIndex],
                                       QString::number(chromeMajorVersion),
                                       QString::number(chromeBuildVersion),
                                       QString::number(chromePatchVersion)));
    }
    return userAgents;
}

QJsonObject ServerConfigHelper::getV2RayServerConfigFromUrl(const QString &server,
                                                            const QString &subscriptionUrl)
{
    // Ref:
    // https://github.com/2dust/v2rayN/wiki/%E5%88%86%E4%BA%AB%E9%93%BE%E6%8E%A5%E6%A0%BC%E5%BC%8F%E8%AF%B4%E6%98%8E(ver-2)
    const QMap<QString, QString> NETWORK_MAPPER = {
        { "tcp", "tcp" }, { "kcp", "kcp" }, { "ws", "ws" }, { "h2", "http" }, { "quic", "quic" },
    };
    QJsonObject rawServerConfig =
            QJsonDocument::fromJson(QByteArray::fromBase64(server.mid(8).toUtf8(),
                                                           QByteArray::AbortOnBase64DecodingErrors))
                    .object();
    QString network = rawServerConfig.contains("net") ? rawServerConfig["net"].toString() : "tcp";
    QString serverAddr = rawServerConfig.contains("add") ? rawServerConfig["add"].toString() : "";
    QString serverPort = rawServerConfig["port"].isString()
            ? rawServerConfig["port"].toString()
            : QString::number(rawServerConfig["port"].toInt());
    int alterId = rawServerConfig.contains("aid")
            ? (rawServerConfig["aid"].isString() ? rawServerConfig["aid"].toString().toInt()
                                                 : rawServerConfig["aid"].toInt())
            : 0;

    QJsonObject serverConfig {
        { "autoConnect", false },
        { "serverName",
          rawServerConfig.contains("ps") ? rawServerConfig["ps"].toString().trimmed()
                                         : serverAddr },
        { "serverAddr", serverAddr },
        { "serverPort", rawServerConfig.contains("port") ? serverPort : "" },
        { "subscription", subscriptionUrl },
        { "id", rawServerConfig.contains("id") ? rawServerConfig["id"].toString() : "" },
        { "alterId", alterId },
        { "udp", false },
        { "security", "auto" },
        { "network", NETWORK_MAPPER.contains(network) ? NETWORK_MAPPER[network] : "tcp" },
        { "networkHost",
          rawServerConfig.contains("host") ? rawServerConfig["host"].toString() : "" },
        { "networkPath",
          rawServerConfig.contains("path") ? rawServerConfig["path"].toString() : "" },
        { "tcpHeaderType",
          rawServerConfig.contains("type") ? rawServerConfig["type"].toString() : "" },
        { "networkSecurity",
          rawServerConfig.contains("tls") && !rawServerConfig["tls"].toString().isEmpty()
                  ? "tls"
                  : "none" }
    };

    return serverConfig;
}

bool ServerConfigHelper::isShadowsocksR(const QJsonObject &serverConfig)
{
    if (!serverConfig.contains("plugins")) {
        return false;
    }
    QJsonObject plugins = serverConfig["plugins"].toObject();
    return plugins.contains("protocol");
}

QStringList ServerConfigHelper::getShadowsocksServerConfigErrors(const QJsonObject &serverConfig,
                                                                 const QString *pServerName)
{
    QStringList errors;
    errors.append(getServerNameError(serverConfig, pServerName));
    errors.append(Utility::getStringConfigError(
            serverConfig, "serverAddr", tr("Server Address"),
            {
                    std::bind(&Utility::isIpAddrValid, std::placeholders::_1),
                    std::bind(&Utility::isDomainNameValid, std::placeholders::_1),
            }));
    errors.append(Utility::getNumericConfigError(serverConfig, "serverPort", tr("Server Port"), 0,
                                                 65535));
    errors.append(Utility::getStringConfigError(serverConfig, "encryption", tr("Security")));
    errors.append(Utility::getStringConfigError(serverConfig, "password", tr("Password")));

    // Remove empty error messages generated by getNumericConfigError() and
    // getStringConfigError()
    errors.removeAll("");
    return errors;
}

QJsonObject ServerConfigHelper::getPrettyShadowsocksConfig(const QJsonObject &serverConfig)
{
    bool isSSR = isShadowsocksR(serverConfig);
    QJsonObject prettyServerCfg = {
        { "autoConnect", serverConfig["autoConnect"].toBool() },
        { "subscription",
          serverConfig.contains("subscription") ? serverConfig["subscription"].toString() : "" },
        { "name", serverConfig["serverName"].toString() },
        { "type", isSSR ? "ssr" : "ss" },
        { "server", serverConfig["serverAddr"].toString() },
        { "port", serverConfig["serverPort"].toVariant().toInt() },
        { "cipher", serverConfig["encryption"].toString().toLower() },
        { "password", serverConfig["password"].toString() }
    };

    // Parse Shadowsocks Plugins
    if (!isSSR && serverConfig.contains("plugins")) {
        QJsonObject plugins = serverConfig["plugins"].toObject();

        if (plugins.contains("obfs") && !plugins["obfs"].toString().isEmpty()) {
            prettyServerCfg["plugin"] = "obfs";
            QJsonObject pluginOpts;
            pluginOpts["mode"] = plugins["obfs"].toString();
            if (plugins.contains("obfs-host") && plugins["obfs-host"].toString().size()) {
                pluginOpts["host"] = plugins["obfs-host"].toString();
            }
            prettyServerCfg["plugin-opts"] = pluginOpts;
        }
    }
    // Parse ShadowsocksR (SSR)
    if (isSSR) {
        QJsonObject plugins = serverConfig["plugins"].toObject();

        prettyServerCfg["obfs"] = plugins["obfs"].toString().toLower();
        prettyServerCfg["protocol"] = plugins["protocol"].toString().toLower();
        if (plugins.contains("obfsparam") && plugins["obfsparam"].toString().size()) {
            prettyServerCfg["obfs-param"] = plugins["obfsparam"].toString();
        }
        if (plugins.contains("protoparam") && plugins["protoparam"].toString().size()) {
            prettyServerCfg["protocol-param"] = plugins["protoparam"].toString().toLower();
        }
        if (plugins.contains("udp")) {
            prettyServerCfg["udp"] = plugins["udp"].toBool();
        }
    }
    return prettyServerCfg;
}

QJsonObject ServerConfigHelper::getShadowsocksServerConfigFromUrl(QString serverUrl,
                                                                  const QString &subscriptionUrl)
{
    serverUrl = serverUrl.mid(5);
    int atIndex = serverUrl.indexOf('@');
    int colonIndex = serverUrl.indexOf(':');
    int splashIndex = serverUrl.indexOf('/');
    int sharpIndex = serverUrl.indexOf('#');
    int questionMarkIndex = serverUrl.indexOf('?');

    QString confidential = QByteArray::fromBase64(serverUrl.left(atIndex).toUtf8(),
                                                  QByteArray::AbortOnBase64DecodingErrors);
    QString serverAddr = serverUrl.mid(atIndex + 1, colonIndex - atIndex - 1);
    QString serverPort = serverUrl.mid(colonIndex + 1,
                                       splashIndex != -1 ? (splashIndex - colonIndex - 1)
                                                         : (sharpIndex - colonIndex - 1));
    QString plugins = serverUrl.mid(questionMarkIndex + 1, sharpIndex - questionMarkIndex - 1);
    QString serverName =
            QUrl::fromPercentEncoding(serverUrl.mid(sharpIndex + 1).toUtf8()).trimmed();

    colonIndex = confidential.indexOf(':');
    QString encryption = confidential.left(colonIndex);
    QString password = confidential.mid(colonIndex + 1);

    QJsonObject serverConfig { { "serverName", serverName },
                               { "autoConnect", false },
                               { "subscription", subscriptionUrl },
                               { "serverAddr", serverAddr },
                               { "serverPort", serverPort },
                               { "encryption", encryption },
                               { "password", password } };

    QJsonObject pluginOptions = getShadowsocksPlugins(plugins);
    if (!pluginOptions.empty()) {
        serverConfig["plugins"] = pluginOptions;
    }
    return serverConfig;
}

QJsonObject ServerConfigHelper::getShadowsocksPlugins(const QString &pluginString)
{
    QJsonObject plugins;
    for (QPair<QString, QString> p : QUrlQuery(pluginString).queryItems()) {
        if (p.first == "plugin") {
            QStringList options = QUrl::fromPercentEncoding(p.second.toUtf8()).split(';');
            for (QString o : options) {
                QStringList t = o.split('=');
                if (t.size() == 2) {
                    plugins[t[0]] = t[1];
                }
            }
        }
    }
    return plugins;
}

QJsonObject ServerConfigHelper::getShadowsocksRServerConfigFromUrl(QString server,
                                                                   const QString &subscriptionUrl)
{
    server = server.mid(6);
    QString serverUrl =
            QByteArray::fromBase64(server.toUtf8(), QByteArray::AbortOnBase64DecodingErrors);
    // Failed to parse the SSR URL
    if (!serverUrl.size()) {
        serverUrl = QByteArray::fromBase64(server.replace('_', '/').toUtf8(),
                                           QByteArray::AbortOnBase64DecodingErrors);
    }
    int sepIndex = serverUrl.indexOf("/?");
    QStringList essentialServerCfg = serverUrl.left(sepIndex).split(':');
    if (essentialServerCfg.size() != 6) {
        return QJsonObject {};
    }

    QString serverAddr = essentialServerCfg.at(0);
    int serverPort = essentialServerCfg.at(1).toInt();
    QString serverName = QString("%1:%2").arg(serverAddr, QString::number(serverPort));
    QJsonObject serverConfig { { "serverName", serverName },
                               { "autoConnect", false },
                               { "subscription", subscriptionUrl },
                               { "serverAddr", serverAddr },
                               { "serverPort", serverPort },
                               { "encryption", essentialServerCfg.at(3) },
                               { "password", essentialServerCfg.at(5) },
                               { "plugins",
                                 QJsonObject { { "obfs", essentialServerCfg.at(4) },
                                               { "protocol", essentialServerCfg.at(2) } } } };

    QString optionalServerCfg = serverUrl.mid(sepIndex + 2);
    for (QPair<QString, QString> p : QUrlQuery(optionalServerCfg).queryItems()) {
        serverConfig[p.first] = p.second;
    }
    return serverConfig;
}

QStringList ServerConfigHelper::getTrojanServerConfigErrors(const QJsonObject &serverConfig,
                                                            const QString *pServerName)
{
    QStringList errors;
    errors.append(getServerNameError(serverConfig, pServerName));
    errors.append(Utility::getStringConfigError(
            serverConfig, "serverAddr", tr("Server Address"),
            {
                    std::bind(&Utility::isIpAddrValid, std::placeholders::_1),
                    std::bind(&Utility::isDomainNameValid, std::placeholders::_1),
            }));
    errors.append(Utility::getNumericConfigError(serverConfig, "serverPort", tr("Server Port"), 0,
                                                 65535));
    errors.append(Utility::getStringConfigError(serverConfig, "password", tr("Password")));
    errors.append(Utility::getStringConfigError(
            serverConfig, "sni", tr("SNI"),
            {
                    std::bind(&Utility::isIpAddrValid, std::placeholders::_1),
                    std::bind(&Utility::isDomainNameValid, std::placeholders::_1),
            },
            true));
    errors.append(Utility::getStringConfigError(
            serverConfig, "alpn", tr("ALPN"),
            {
                    std::bind(&Utility::isAlpnValid, std::placeholders::_1),
            }));

    // Remove empty error messages generated by getNumericConfigError() and
    // getStringConfigError()
    errors.removeAll("");
    return errors;
}

QJsonObject ServerConfigHelper::getPrettyTrojanConfig(const QJsonObject &serverConfig)
{
    QJsonArray alpn;
    for (QString a : Utility::getAlpn(serverConfig["alpn"].toString())) {
        alpn.append(a);
    }

    QJsonObject prettyServerCfg = {
        { "autoConnect", serverConfig["autoConnect"].toBool() },
        { "subscription",
          serverConfig.contains("subscription") ? serverConfig["subscription"].toString() : "" },
        { "name", serverConfig["serverName"].toString() },
        { "type", "trojan" },
        { "server", serverConfig["serverAddr"].toString() },
        { "port", serverConfig["serverPort"].toVariant().toInt() },
        { "password", serverConfig["password"].toString() },
        { "sni", serverConfig["sni"].toString() },
        { "udp", serverConfig["udp"].toBool() },
        { "alpn", alpn },
        { "skip-cert-verify", serverConfig["allowInsecure"].toBool() }
    };

    return prettyServerCfg;
}

QJsonObject ServerConfigHelper::getTrojanServerConfigFromUrl(QString serverUrl,
                                                             const QString &subscriptionUrl)
{
    serverUrl = serverUrl.mid(9);
    int atIndex = serverUrl.indexOf('@');
    int colonIndex = serverUrl.indexOf(':');
    int sharpIndex = serverUrl.indexOf('#');
    int questionMarkIndex = serverUrl.indexOf('?') == -1 ? sharpIndex : serverUrl.indexOf('?');

    QString password = serverUrl.left(atIndex);
    QString serverAddr = serverUrl.mid(atIndex + 1, colonIndex - atIndex - 1);
    QString serverPort = serverUrl.mid(colonIndex + 1, questionMarkIndex - colonIndex - 1);
    QString options = serverUrl.mid(questionMarkIndex + 1, sharpIndex - questionMarkIndex - 1);
    QString serverName =
            QUrl::fromPercentEncoding(serverUrl.mid(sharpIndex + 1).toUtf8()).trimmed();

    QJsonObject serverConfig { { "serverName", serverName },        { "autoConnect", false },
                               { "subscription", subscriptionUrl }, { "serverAddr", serverAddr },
                               { "serverPort", serverPort },        { "password", password } };

    QJsonObject serverOptions = getTrojanOptions(options);
    for (auto itr = serverOptions.begin(); itr != serverOptions.end(); ++itr) {
        serverConfig[itr.key()] = itr.value();
    }
    return serverConfig;
}

QJsonObject ServerConfigHelper::getTrojanOptions(const QString &optionString)
{
    QJsonObject options { { "sni", DEFAULT_TROJRAN_SNI },
                          { "udp", DEFAULT_TROJRAN_ENABLE_UDP },
                          { "alpn", DEFAULT_TROJRAN_ALPN },
                          { "allowInsecure", DEFAULT_TROJRAN_ALLOW_INSECURE } };

    for (QPair<QString, QString> p : QUrlQuery(optionString).queryItems()) {
        if (options.contains(p.first)) {
            options[p.first] = p.second;
        }
    }
    return options;
}

QList<QJsonObject> ServerConfigHelper::getServerConfigFromV2RayConfig(const QJsonObject &config)
{
    QList<QJsonObject> servers;
    QJsonArray serversConfig = config["outbounds"].toArray();
    for (auto itr = serversConfig.begin(); itr != serversConfig.end(); ++itr) {
        QJsonObject server = (*itr).toObject();
        QString protocol = server["protocol"].toString();
        if (protocol != "vmess") {
            qWarning() << QString("Ignore the server protocol: %1").arg(protocol);
            continue;
        }
        QJsonObject serverSettings =
                getV2RayServerSettingsFromConfig(server["settings"].toObject());
        QJsonObject streamSettings = getV2RayStreamSettingsFromConfig(
                server["streamSettings"].toObject(), config["transport"].toObject());
        if (!serverSettings.empty()) {
            QJsonObject serverConfig = serverSettings;
            // Stream Settings
            for (auto itr = streamSettings.begin(); itr != streamSettings.end(); ++itr) {
                serverConfig.insert(itr.key(), itr.value());
            }
            // MUX Settings
            if (server.contains("mux")) {
                QJsonObject muxObject = server["mux"].toObject();
                int mux = muxObject["concurrency"].toVariant().toInt();
                if (mux > 0) {
                    serverConfig.insert("mux", QString::number(mux));
                }
            }
            if (!serverConfig.contains("mux")) {
                // Default value for MUX
                serverConfig["mux"] = -1;
            }
            servers.append(serverConfig);
        }
    }
    return servers;
}

QJsonObject ServerConfigHelper::getV2RayServerSettingsFromConfig(const QJsonObject &settings)
{
    QJsonObject server;
    QJsonArray vnext = settings["vnext"].toArray();
    if (vnext.size()) {
        QJsonObject _server = vnext.at(0).toObject();
        server["serverAddr"] = _server["address"].toString();
        server["serverPort"] = _server["port"].toVariant().toInt();
        server["serverName"] = QString("%1:%2").arg(server["serverAddr"].toString(),
                                                    QString::number(server["serverPort"].toInt()));
        QJsonArray users = _server["users"].toArray();
        if (users.size()) {
            QJsonObject user = users.at(0).toObject();
            server["id"] = user["id"].toString();
            server["alterId"] = user["alterId"].toVariant().toInt();
            server["security"] = user.contains("security") ? user["security"].toString() : "auto";
        }
    }
    return server;
}

QJsonObject ServerConfigHelper::getV2RayStreamSettingsFromConfig(const QJsonObject &transport,
                                                                 const QJsonObject &streamSettings)
{
    QJsonObject _streamSettings = streamSettings.empty() ? transport : streamSettings;
    QJsonObject serverStreamSettings;
    QString network =
            _streamSettings.contains("network") ? _streamSettings["network"].toString() : "tcp";
    serverStreamSettings["network"] = network;
    serverStreamSettings["networkSecurity"] =
            _streamSettings.contains("security") ? _streamSettings["security"].toString() : "none";
    serverStreamSettings["allowInsecure"] = true;
    if (_streamSettings.contains("tlsSettings")) {
        QJsonObject tlsSettings = _streamSettings["tlsSettings"].toObject();
        serverStreamSettings["allowInsecure"] = tlsSettings["allowInsecure"];
    }
    if (network == "tcp") {
        QJsonObject tcpSettings = _streamSettings["tcpSettings"].toObject();
        QJsonObject header = tcpSettings["header"].toObject();
        serverStreamSettings["tcpHeaderType"] =
                header.contains("type") ? header["type"].toString() : "none";
    } else if (network == "kcp") {
        QJsonObject kcpSettings = _streamSettings["kcpSettings"].toObject();
        QJsonObject header = kcpSettings["header"].toObject();
        serverStreamSettings["kcpMtu"] = kcpSettings.contains("mtu")
                ? kcpSettings["mtu"].toVariant().toInt()
                : DEFAULT_V2RAY_KCP_MTU;
        serverStreamSettings["kcpTti"] = kcpSettings.contains("tti")
                ? kcpSettings["tti"].toVariant().toInt()
                : DEFAULT_V2RAY_KCP_TTI;
        serverStreamSettings["kcpUpLink"] = kcpSettings.contains("uplinkCapacity")
                ? kcpSettings["uplinkCapacity"].toVariant().toInt()
                : DEFAULT_V2RAY_KCP_UP_CAPACITY;
        serverStreamSettings["kcpDownLink"] = kcpSettings.contains("downlinkCapacity")
                ? kcpSettings["downlinkCapacity"].toVariant().toInt()
                : DEFAULT_V2RAY_KCP_DOWN_CAPACITY;
        serverStreamSettings["kcpReadBuffer"] = kcpSettings.contains("readBufferSize")
                ? kcpSettings["readBufferSize"].toVariant().toInt()
                : DEFAULT_V2RAY_KCP_READ_BUF_SIZE;
        serverStreamSettings["kcpWriteBuffer"] = kcpSettings.contains("writeBufferSize")
                ? kcpSettings["writeBufferSize"].toVariant().toInt()
                : DEFAULT_V2RAY_KCP_READ_BUF_SIZE;
        serverStreamSettings["kcpCongestion"] = kcpSettings["congestion"].toBool();
        serverStreamSettings["packetHeader"] =
                header.contains("type") ? header["type"].toString() : "none";
    } else if (network == "ws") {
        QJsonObject wsSettings = _streamSettings["wsSettings"].toObject();
        QJsonObject headers = wsSettings["headers"].toObject();
        serverStreamSettings["networkHost"] =
                headers.contains("host") ? headers["host"].toString() : headers["Host"].toString();
        serverStreamSettings["networkPath"] = wsSettings["path"];
    } else if (network == "http") {
        QJsonObject httpSettings = _streamSettings["httpSettings"].toObject();
        serverStreamSettings["networkHost"] = httpSettings["host"].toArray().at(0);
        serverStreamSettings["networkPath"] = httpSettings["path"].toString();
    } else if (network == "domainsocket") {
        QJsonObject dsSettings = _streamSettings["dsSettings"].toObject();
        serverStreamSettings["domainSocketFilePath"] = dsSettings["path"].toString();
    } else if (network == "quic") {
        QJsonObject quicSettings = _streamSettings["quicSettings"].toObject();
        QJsonObject header = quicSettings["header"].toObject();
        serverStreamSettings["quicSecurity"] =
                quicSettings.contains("security") ? quicSettings["security"].toString() : "none";
        serverStreamSettings["packetHeader"] =
                header.contains("type") ? header["type"].toString() : "none";
        serverStreamSettings["quicKey"] = quicSettings["key"].toString();
    }
    return serverStreamSettings;
}

QList<QJsonObject>
ServerConfigHelper::getServerConfigFromShadowsocksQt5Config(const QJsonObject &config)
{
    QList<QJsonObject> servers;
    QJsonArray serversConfig = config["configs"].toArray();

    for (auto itr = serversConfig.begin(); itr != serversConfig.end(); ++itr) {
        QJsonObject server = (*itr).toObject();
        QJsonObject serverConfig = {
            { "serverName", server["remarks"].toString().trimmed() },
            { "serverAddr", server["server"].toString() },
            { "serverPort", QString::number(server["server_port"].toInt()) },
            { "encryption", server["method"].toString() },
            { "password", server["password"].toString() },
        };
        if (server.contains("plugin_opts") && !server["plugin_opts"].toString().isEmpty()) {
            QString plugins = QString("plugin=%1%3B%2")
                                      .arg(server["plugin"].toString(),
                                           QString(QUrl::toPercentEncoding(
                                                   server["plugin_opts"].toString())));
            serverConfig["plugins"] = getShadowsocksPlugins(plugins);
        }
        servers.append(serverConfig);
    }
    return servers;
}
