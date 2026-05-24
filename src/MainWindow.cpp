#include "MainWindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
constexpr const char *kMessageTypePresence = "presence";
constexpr const char *kMessageTypeChat = "chat";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_historyPath = QCoreApplication::applicationDirPath() + "/chat_history.txt";

    setupUi();
    setupNetworking();
    loadChatHistory();
}

void MainWindow::setupUi()
{
    setWindowTitle("LAN Chat");
    resize(800, 520);

    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    auto *mainLayout = new QHBoxLayout(m_centralWidget);

    auto *leftLayout = new QVBoxLayout();
    auto *rightLayout = new QVBoxLayout();

    auto *nameLabel = new QLabel("Your name:");
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("Enter name");

    m_visibilityCheck = new QCheckBox("Visible");
    m_visibilityCheck->setChecked(true);

    auto *usersLabel = new QLabel("Nearby users:");
    m_usersList = new QListWidget();

    leftLayout->addWidget(nameLabel);
    leftLayout->addWidget(m_nameEdit);
    leftLayout->addWidget(m_visibilityCheck);
    leftLayout->addSpacing(8);
    leftLayout->addWidget(usersLabel);
    leftLayout->addWidget(m_usersList, 1);

    auto *chatLabel = new QLabel("Chat:");
    m_chatView = new QTextEdit();
    m_chatView->setReadOnly(true);

    auto *messageRow = new QHBoxLayout();
    m_messageEdit = new QLineEdit();
    m_messageEdit->setPlaceholderText("Type a message...");
    m_sendButton = new QPushButton("Send");

    messageRow->addWidget(m_messageEdit, 1);
    messageRow->addWidget(m_sendButton);

    rightLayout->addWidget(chatLabel);
    rightLayout->addWidget(m_chatView, 1);
    rightLayout->addLayout(messageRow);

    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addLayout(rightLayout, 2);

    statusBar()->showMessage("Ready");

    connect(m_usersList, &QListWidget::itemSelectionChanged, this, &MainWindow::onUserSelectionChanged);
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_messageEdit, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
}

void MainWindow::setupNetworking()
{
    m_udpSocket = new QUdpSocket(this);

    if (!m_udpSocket->bind(QHostAddress::AnyIPv4, m_udpPort,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
        QMessageBox::warning(this, "UDP Bind Failed", m_udpSocket->errorString());
    }

    connect(m_udpSocket, &QUdpSocket::readyRead, this, &MainWindow::onDiscoveryReadyRead);

    m_broadcastTimer = new QTimer(this);
    m_broadcastTimer->setInterval(2000);
    connect(m_broadcastTimer, &QTimer::timeout, this, &MainWindow::onBroadcastTimeout);
    m_broadcastTimer->start();

    m_pruneTimer = new QTimer(this);
    m_pruneTimer->setInterval(3000);
    connect(m_pruneTimer, &QTimer::timeout, this, &MainWindow::onPruneTimeout);
    m_pruneTimer->start();

    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(QHostAddress::AnyIPv4, m_tcpPort))
    {
        QMessageBox::warning(this, "TCP Listen Failed", m_tcpServer->errorString());
    }

    connect(m_tcpServer, &QTcpServer::newConnection, this, &MainWindow::onNewTcpConnection);
}

void MainWindow::onBroadcastTimeout()
{
    if (!m_visibilityCheck->isChecked())
    {
        return;
    }

    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
    {
        name = "Anonymous";
    }

    QJsonObject payload;
    payload["type"] = kMessageTypePresence;
    payload["id"] = m_instanceId;
    payload["name"] = name;
    payload["port"] = static_cast<int>(m_tcpPort);

    QJsonDocument doc(payload);
    QByteArray datagram = doc.toJson(QJsonDocument::Compact);

    m_udpSocket->writeDatagram(datagram, QHostAddress::Broadcast, m_udpPort);
}

void MainWindow::onDiscoveryReadyRead()
{
    while (m_udpSocket->hasPendingDatagrams())
    {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_udpSocket->pendingDatagramSize()));
        QHostAddress senderAddress;
        quint16 senderPort = 0;

        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &senderAddress, &senderPort);

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(datagram, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
        {
            continue;
        }

        QJsonObject obj = doc.object();
        if (obj.value("type").toString() != kMessageTypePresence)
        {
            continue;
        }

        QString peerId = obj.value("id").toString();
        if (isSelfPresence(peerId, senderAddress))
        {
            continue;
        }

        PeerInfo info;
        info.id = peerId;
        info.name = obj.value("name").toString("Unknown");
        info.address = senderAddress;
        info.port = static_cast<quint16>(obj.value("port").toInt(m_tcpPort));
        info.lastSeenMs = QDateTime::currentMSecsSinceEpoch();

        m_peersById.insert(peerId, info);
    }

    updateUsersList();
}

void MainWindow::onPruneTimeout()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool removed = false;

    auto it = m_peersById.begin();
    while (it != m_peersById.end())
    {
        if (now - it->lastSeenMs > m_presenceTtlMs)
        {
            it = m_peersById.erase(it);
            removed = true;
        }
        else
        {
            ++it;
        }
    }

    if (removed)
    {
        updateUsersList();
    }
}

void MainWindow::onUserSelectionChanged()
{
    m_sendButton->setEnabled(m_usersList->currentItem() != nullptr);
}

void MainWindow::onSendClicked()
{
    QListWidgetItem *item = m_usersList->currentItem();
    if (!item)
    {
        statusBar()->showMessage("Select a user first", 2000);
        return;
    }

    const QString messageText = m_messageEdit->text().trimmed();
    if (messageText.isEmpty())
    {
        return;
    }

    const QString peerId = item->data(Qt::UserRole).toString();
    if (!m_peersById.contains(peerId))
    {
        statusBar()->showMessage("User is no longer available", 2000);
        updateUsersList();
        return;
    }

    const PeerInfo info = m_peersById.value(peerId);

    QTcpSocket *socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::readyRead, this, &MainWindow::onTcpSocketReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &MainWindow::onTcpSocketDisconnected);
    connect(socket, &QTcpSocket::errorOccurred, this, &MainWindow::onTcpSocketError);

    socket->connectToHost(info.address, info.port);
    if (!socket->waitForConnected(1500))
    {
        statusBar()->showMessage("Failed to connect", 2000);
        socket->deleteLater();
        return;
    }

    QString senderName = m_nameEdit->text().trimmed();
    if (senderName.isEmpty())
    {
        senderName = "Anonymous";
    }

    QJsonObject payload;
    payload["type"] = kMessageTypeChat;
    payload["from"] = senderName;
    payload["text"] = messageText;
    payload["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonDocument doc(payload);
    QByteArray line = doc.toJson(QJsonDocument::Compact) + "\n";
    socket->write(line);
    socket->flush();
    socket->disconnectFromHost();

    const QString lineText = formatMessageLine(senderName, messageText);
    appendChatLine(lineText);
    saveChatLine(lineText);

    m_messageEdit->clear();
}

void MainWindow::onNewTcpConnection()
{
    while (m_tcpServer->hasPendingConnections())
    {
        QTcpSocket *socket = m_tcpServer->nextPendingConnection();
        m_tcpBuffers.insert(socket, QByteArray());

        connect(socket, &QTcpSocket::readyRead, this, &MainWindow::onTcpSocketReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &MainWindow::onTcpSocketDisconnected);
        connect(socket, &QTcpSocket::errorOccurred, this, &MainWindow::onTcpSocketError);
    }
}

void MainWindow::onTcpSocketReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
    {
        return;
    }

    QByteArray &buffer = m_tcpBuffers[socket];
    buffer.append(socket->readAll());

    int newlineIndex = -1;
    while ((newlineIndex = buffer.indexOf('\n')) != -1)
    {
        QByteArray line = buffer.left(newlineIndex);
        buffer.remove(0, newlineIndex + 1);

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
        {
            continue;
        }

        QJsonObject obj = doc.object();
        if (obj.value("type").toString() != kMessageTypeChat)
        {
            continue;
        }

        QString senderName = obj.value("from").toString("Unknown");
        QString text = obj.value("text").toString();

        const QString lineText = formatMessageLine(senderName, text);
        appendChatLine(lineText);
        saveChatLine(lineText);
    }
}

void MainWindow::onTcpSocketDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
    {
        return;
    }

    m_tcpBuffers.remove(socket);
    socket->deleteLater();
}

void MainWindow::onTcpSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)

    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
    {
        return;
    }

    statusBar()->showMessage(socket->errorString(), 2000);
}

void MainWindow::updateUsersList()
{
    const QString selectedId = m_usersList->currentItem()
                                 ? m_usersList->currentItem()->data(Qt::UserRole).toString()
                                 : QString();

    QSignalBlocker blocker(m_usersList);
    m_usersList->clear();

    QList<PeerInfo> peers = m_peersById.values();
    std::sort(peers.begin(), peers.end(), [](const PeerInfo &a, const PeerInfo &b) {
        const QString nameA = a.name.toLower();
        const QString nameB = b.name.toLower();
        if (nameA == nameB)
        {
            return a.id < b.id;
        }
        return nameA < nameB;
    });

    QListWidgetItem *selectedItem = nullptr;
    for (const PeerInfo &peer : peers)
    {
        const QString label = QString("%1 (%2)").arg(peer.name, peer.address.toString());
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, peer.id);
        m_usersList->addItem(item);

        if (!selectedId.isEmpty() && peer.id == selectedId)
        {
            selectedItem = item;
        }
    }

    if (selectedItem)
    {
        m_usersList->setCurrentItem(selectedItem);
    }

    onUserSelectionChanged();
}

void MainWindow::appendChatLine(const QString &line)
{
    m_chatView->append(line);
}

void MainWindow::saveChatLine(const QString &line)
{
    QFile file(m_historyPath);
    if (!file.open(QIODevice::Append | QIODevice::Text))
    {
        statusBar()->showMessage("Unable to save chat history", 2000);
        return;
    }

    file.write(line.toUtf8());
    file.write("\n");
}

void MainWindow::loadChatHistory()
{
    QFile file(m_historyPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return;
    }

    QByteArray data = file.readAll();
    if (!data.isEmpty())
    {
        m_chatView->setPlainText(QString::fromUtf8(data));
    }
}

bool MainWindow::isSelfPresence(const QString &peerId, const QHostAddress &address) const
{
    if (peerId == m_instanceId)
    {
        return true;
    }

    const QList<QHostAddress> localAddresses = QNetworkInterface::allAddresses();
    return localAddresses.contains(address);
}

QString MainWindow::formatMessageLine(const QString &sender, const QString &text) const
{
    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    return QString("[%1] %2: %3").arg(timestamp, sender, text);
}
