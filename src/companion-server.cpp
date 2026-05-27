#include "companion-server.hpp"
#include "dsk-manager.hpp"

#include <QTcpSocket>
#include <QUrl>

CompanionServer *g_companionServer = nullptr;

CompanionServer::CompanionServer(QObject *parent) : QObject(parent)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &CompanionServer::onNewConnection);
}

CompanionServer::~CompanionServer() { stop(); }

bool CompanionServer::start(quint16 port)
{
    stop();
    m_port = port;
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        blog(LOG_WARNING, "[dsk] HTTP server failed to bind port %d: %s",
             port, m_server->errorString().toUtf8().constData());
        return false;
    }
    blog(LOG_INFO, "[dsk] HTTP server listening on 127.0.0.1:%d", port);
    return true;
}

void CompanionServer::stop()
{
    if (m_server->isListening()) {
        m_server->close();
        blog(LOG_INFO, "[dsk] HTTP server stopped");
    }
}

void CompanionServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        auto *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead,    this, &CompanionServer::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &CompanionServer::onClientDisconnected);
    }
}

void CompanionServer::onReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    QByteArray data = sock->readAll();
    if (data.isEmpty()) return;
    auto [method, path] = parseRequestLine(data);
    handleRequest(sock, method, path);
}

void CompanionServer::onClientDisconnected()
{
    if (auto *sock = qobject_cast<QTcpSocket *>(sender()))
        sock->deleteLater();
}

CompanionServer::RequestLine CompanionServer::parseRequestLine(const QByteArray &data)
{
    int eol = data.indexOf('\r');
    if (eol < 0) eol = data.indexOf('\n');
    if (eol < 0) return {};
    QList<QByteArray> parts = data.left(eol).split(' ');
    if (parts.size() < 2) return {};
    return { QString::fromUtf8(parts[0]).toUpper(), QString::fromUtf8(parts[1]) };
}

/*
 *  GET  /api/status                   -> all items + active state
 *  GET  /api/item/:name               -> single item (name is URL-encoded)
 *  POST /api/item/:name/activate
 *  POST /api/item/:name/deactivate
 *  POST /api/item/:name/toggle
 */
void CompanionServer::handleRequest(QTcpSocket *sock,
                                    const QString &method,
                                    const QString &path)
{
    auto &mgr = DskManager::instance();

    if (method == "GET" && path == "/api/status") {
        sendJson(sock, 200, buildStatusJson());
        return;
    }

    if (path.startsWith("/api/item/")) {
        QString rest   = path.mid(10);
        int slash      = rest.indexOf('/');
        QString enc    = (slash >= 0) ? rest.left(slash) : rest;
        QString action = (slash >= 0) ? rest.mid(slash + 1) : "";
        std::string name = QUrl::fromPercentEncoding(enc.toUtf8()).toStdString();

        if (name.empty()) { sendError(sock, 400, "empty name"); return; }

        if (method == "GET" && action.isEmpty()) {
            sendJson(sock, 200, buildItemJson(name, mgr.isActive(name),
                                             mgr.timeRemaining(name)));
            return;
        }
        if (method == "POST") {
            if (action == "activate")        mgr.activate(name);
            else if (action == "deactivate") mgr.deactivate(name);
            else if (action == "toggle")     mgr.toggle(name);
            else { sendError(sock, 404, "unknown action"); return; }
            sendJson(sock, 200, buildItemJson(name, mgr.isActive(name),
                                             mgr.timeRemaining(name)));
            return;
        }
    }

    sendError(sock, 404, "not found");
}

void CompanionServer::sendJson(QTcpSocket *sock, int status, const QByteArray &body)
{
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n";
    resp += "Content-Type: application/json\r\n";
    resp += "Access-Control-Allow-Origin: *\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Connection: close\r\n\r\n";
    resp += body;
    sock->write(resp);
    sock->flush();
    sock->disconnectFromHost();
}

void CompanionServer::sendError(QTcpSocket *sock, int status, const QString &msg)
{
    sendJson(sock, status, "{\"error\":\"" + msg.toUtf8() + "\"}");
}

QByteArray CompanionServer::escapeJson(const std::string &s)
{
    return QByteArray::fromStdString(s).replace('\\', "\\\\").replace('"', "\\\"");
}

QByteArray CompanionServer::buildItemJson(const std::string &name, bool active,
                                          double timeRemaining)
{
    QByteArray json = "{\"name\":\"" + escapeJson(name) + "\","
                      "\"active\":" + (active ? "true" : "false");
    if (timeRemaining >= 0.0)
        json += ",\"timeRemaining\":" + QByteArray::number(timeRemaining, 'f', 1);
    json += "}";
    return json;
}

QByteArray CompanionServer::buildStatusJson() const
{
    auto &mgr = DskManager::instance();
    QByteArray out = "{\"scene\":\"" + escapeJson(mgr.sceneName()) + "\",\"items\":[";
    bool first = true;
    for (const auto &item : mgr.currentItems()) {
        if (!first) out += ",";
        first = false;
        out += buildItemJson(item.sourceName, item.visible,
                             mgr.timeRemaining(item.sourceName));
    }
    out += "]}";
    return out;
}
