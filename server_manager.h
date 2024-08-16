#pragma once

#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTime>

#include "database.h"

class server_manager : public QObject
{
    Q_OBJECT

public:
    server_manager(const QString &URI_string, QObject *parent = nullptr);
    server_manager(QWebSocket *client, QObject *parent = nullptr);
    ~server_manager();

    void sign_up(const int &phone_number, const QString &first_name, const QString &last_name, const QString &password, const QString &secret_question, const QString &secret_answer);
    void login_request(const int &phone_number, const QString &password, const QString &time_zone);
    void audio_received(const QString &audio_name, const QString &audio_data);
    void lookup_friend(const int &phone_number);
    void profile_image(const QString &file_name, const QString &data);
    void group_profile_image(const int &group_ID, const QString &file_name, const QString &data);
    void profile_image_deleted();
    void text_received(const int &receiver, const QString &message, const QString &time, const int &chat_ID);
    void new_group(const QString &group_name, QJsonArray group_members);
    void group_text_received(const int &groupID, QString sender_name, const QString &message, const QString &time);
    void file_received(const int &chatID, const int &receiver, const QString &file_name, const QString &file_data, const QString &time);
    void group_file_received(const int &groupID, const QString &sender_name, const QString &file_name, const QString &file_data, const QString &time);
    void is_typing_received(const int &receiver);
    void group_is_typing_received(const int &groupID);

private slots:
    void on_new_connection();
    void on_client_disconnected();
    void on_text_message_received(const QString &message);

private:
    QWebSocketServer *_server{nullptr};
    QWebSocket *_socket{nullptr};

    static mongocxx::database _chatAppDB;
    static QHash<int, QWebSocket *> _clients;
    static QHash<int, QString> _time_zone;

    static Aws::SDKOptions _options;
    static std::shared_ptr<Aws::S3::S3Client> _s3_client;

    QHostAddress _ip{QHostAddress::Any};
    int _port{12345};

    void map_initialization();

    enum MessageType
    {
        SignUp = Qt::UserRole + 1,
        IsTyping,
        SetName,
        ProfileImage,
        GroupProfileImage,
        ProfileImageDeleted,
        File,
        Text,
        GroupFile,
        GroupText,
        LoginRequest,
        ClientNewName,
        ClientDisconnected,
        ClientConnected,
        LookupFriend,
        NewGroup,
        AddedToGroup,
        AudioMessage,
        NewPasswordRequest,
        UpdatePassword,
        DeleteMessage,
        DeleteGroupMessage,
        GroupIsTyping,
        GroupAudio,
        NewGroupMember,
        RemoveGroupMember,
        DeleteAccount,
        LastMessageRead,
        GroupLastMessageRead,
        InvalidType
    };
    static QHash<QString, MessageType> _map;
};
