#pragma once

#include "database.hpp"

class server_manager : public QObject
{
    Q_OBJECT

public:
    server_manager(QObject *parent = nullptr);
    server_manager(std::shared_ptr<QWebSocket> client, QObject *parent = nullptr);
    ~server_manager();

    void sign_up(const int &phone_number, const QString &first_name, const QString &last_name, const QString &password, const QString &secret_question, const QString &secret_answer);
    void login_request(const int &phone_number, const QString &password, const QString &time_zone);
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
    void group_is_typing_received(const int &groupID, const QString &sender_name);
    void update_info_received(const QString &first_name, const QString &last_name, const QString &password);
    void update_password(const int &phone_number, const QString &password);
    void retrieve_question(const int &phone_number);
    void remove_group_member(const int &groupID, QJsonArray group_members);
    void add_group_member(const int &groupID, QJsonArray group_members);
    void delete_message(const int &receiver, const int &chat_ID, const QString &full_time);
    void delete_group_message(const int &groupID, const QString &full_time);
    void update_unread_message(const int &chatID);
    void update_group_unread_message(const int &groupID);
    void delete_account();
    void audio_received(const int &chatID, const int &receiver, const QString &audio_name, const QString &audio_data, const QString &time);
    void group_audio_received(const int &groupID, const QString &sender_name, const QString &audio_name, const QString &audio_data, const QString &time);

private slots:
    void on_new_connection();
    void on_client_disconnected();
    void on_text_message_received(const QString &message);

private:
    QWebSocketServer *_server{nullptr};
    std::shared_ptr<QWebSocket> _socket{nullptr};

    static inline mongocxx::database _chatAppDB{};
    static inline QHash<int, std::shared_ptr<QWebSocket>> _clients{};
    static inline QHash<int, QString> _time_zone{};

    static inline Aws::SDKOptions _options{};
    static inline std::shared_ptr<Aws::S3::S3Client> _s3_client{};

    QHostAddress _ip{QHostAddress::Any};
    int _port{12345};

    void map_initialization();

    enum MessageType
    {
        SignUp = Qt::UserRole + 1,
        IsTyping,
        ProfileImage,
        GroupProfileImage,
        ProfileImageDeleted,
        File,
        Text,
        GroupFile,
        GroupText,
        LoginRequest,
        ClientDisconnected,
        ClientConnected,
        LookupFriend,
        NewGroup,
        AddedToGroup,
        GroupIsTyping,
        UpdateInfo,
        UpdatePassword,
        RetrieveQuestion,
        RemoveGroupMember,
        AddGroupMember,
        Audio,
        GroupAudio,
        DeleteMessage,
        DeleteGroupMessage,
        UpdateUnreadMessage,
        UpdateGroupUnreadMessage,
        DeleteAccount
    };
    static inline QHash<QString, MessageType> _map{};
};
