#pragma once

#include <QCheckBox>
#include <QHash>
#include <QHostAddress>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QTextEdit>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QUuid>

struct PeerInfo
{
    QString id;
    QString name;
    QHostAddress address;
    quint16 port = 0;
    qint64 lastSeenMs = 0;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onBroadcastTimeout();
    void onDiscoveryReadyRead();
    void onPruneTimeout();
    void onUserSelectionChanged();
    void onSendClicked();
    void onNewTcpConnection();
    void onTcpSocketReadyRead();
    void onTcpSocketDisconnected();
    void onTcpSocketError(QAbstractSocket::SocketError socketError);

private:
    void setupUi();
    void setupNetworking();
    void updateUsersList();
    void appendChatLine(const QString &line);
    void saveChatLine(const QString &line);
    void loadChatHistory();
    bool isSelfPresence(const QString &peerId, const QHostAddress &address) const;
    QString formatMessageLine(const QString &sender, const QString &text) const;

    QString m_instanceId;
    quint16 m_udpPort = 45454;
    quint16 m_tcpPort = 45455;
    qint64 m_presenceTtlMs = 7000;

    QWidget *m_centralWidget = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QCheckBox *m_visibilityCheck = nullptr;
    QListWidget *m_usersList = nullptr;
    QTextEdit *m_chatView = nullptr;
    QLineEdit *m_messageEdit = nullptr;
    QPushButton *m_sendButton = nullptr;

    QUdpSocket *m_udpSocket = nullptr;
    QTimer *m_broadcastTimer = nullptr;
    QTimer *m_pruneTimer = nullptr;
    QTcpServer *m_tcpServer = nullptr;

    QHash<QString, PeerInfo> m_peersById;
    QHash<QTcpSocket *, QByteArray> m_tcpBuffers;

    QString m_historyPath;
};
