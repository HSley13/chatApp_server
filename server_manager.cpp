#include "server_manager.h"

QHash<int, QWebSocket *> server_manager::_clients;
QHash<int, QString> server_manager::_time_zone;
mongocxx::database server_manager::_chatAppDB;
QHash<QString, server_manager::MessageType> server_manager::_map;

server_manager::server_manager(const QString &URI_string, QObject *parent)
    : QObject(parent)
{
    _server = new QWebSocketServer(QString("ChatApp Server"), QWebSocketServer::NonSecureMode, this);
    connect(_server, &QWebSocketServer::newConnection, this, &server_manager::on_new_connection);

    map_initialization();

    static mongocxx::instance instance{};
    static mongocxx::uri uri{URI_string.toStdString()};

    static mongocxx::client connection{uri};
    if (!connection)
    {
        qDebug() << "DB initialization failed";
        return;
    }

    _chatAppDB = connection.database("chatAppDB");

    Aws::InitAPI(_options);
    _s3_client = std::make_unique<Aws::S3::S3Client>();

    _server->listen(_ip, _port);
    qDebug() << "Server is running on port: " << _port;
}

server_manager::~server_manager()
{
    Aws::ShutdownAPI(_options);

    if (!_clients.isEmpty())
    {
        for (QWebSocket *cl : _clients)
            delete cl;
    }
}

server_manager::server_manager(QWebSocket *client, QObject *parent)
    : QObject(parent), _socket(client) { connect(_socket, &QWebSocket::textMessageReceived, this, &server_manager::on_text_message_received); }

void server_manager::on_new_connection()
{
    QWebSocket *client = _server->nextPendingConnection();
    qDebug() << "New Client Connected";

    connect(client, &QWebSocket::disconnected, this, &server_manager::on_client_disconnected);

    server_manager *server = new server_manager(client, this);
}

void server_manager::on_client_disconnected()
{
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());
    if (client)
    {
        int id = client->property("id").toInt();
        _clients.remove(id);

        QString message;
        if (!_clients.isEmpty())
        {
            for (QWebSocket *cl : _clients)
                cl->sendTextMessage(message);
        }
    }
}

void server_manager::sign_up(const int &phone_number, const QString &first_name, const QString &last_name, const QString &password, const QString &secret_question, const QString &secret_answer)
{
    const QString &hashed_password = QString::fromStdString(Security::hashing_password(password.toStdString()));

    QJsonObject json_object{{"_id", phone_number},
                            {"first_name", first_name},
                            {"last_name", last_name},
                            {"image_url", QString()},
                            {"status", false},
                            {"hashed_password", hashed_password},
                            {"secret_question", secret_question},
                            {"secret_answer", secret_answer},
                            {"chats", QJsonArray{}},
                            {"contacts", QJsonArray{}},
                            {"groups", QJsonArray{}}};

    bool succeeded_or_failed = Account::insert_document(_chatAppDB, "accounts", json_object);

    QJsonObject response_object{{"type", "sign_up"},
                                {"status", succeeded_or_failed ? "succeeded" : "failed"},
                                {"message", succeeded_or_failed ? "Account Created Successfully" : "Failed to Create Account, try again"}};

    QJsonDocument response_doc(response_object);

    _socket->sendTextMessage(QString::fromUtf8(response_doc.toJson()));
}

void server_manager::login_request(const int &phone_number, const QString &password, const QString &time_zone)
{
    if (!phone_number)
        return;

    QJsonObject filter_object{{"_id", phone_number}};
    QJsonDocument json_doc = Account::find_document(_chatAppDB, "accounts", filter_object);

    if (json_doc.isEmpty())
    {
        QJsonObject json_message{{"type", "login_request"},
                                 {"status", "failed"},
                                 {"message", "Account Doesn't exist in our Database, verify and try again"}};

        QJsonDocument json_doc(json_message);

        _socket->sendTextMessage(QString::fromUtf8(json_doc.toJson()));

        return;
    }

    if (Security::verifying_password(password.toStdString(), json_doc.object()["hashed_password"].toString().toStdString()))
    {
        QJsonObject json_message{{"type", "login_request"},
                                 {"status", "failed"},
                                 {"message", "Password Incorrect"}};

        QJsonDocument json_doc(json_message);

        _socket->sendTextMessage(QString::fromUtf8(json_doc.toJson()));

        return;
    }

    _time_zone.insert(phone_number, time_zone);
    _clients.insert(phone_number, _socket);
    _socket->setProperty("id", phone_number);

    QJsonObject update_object{{"status", true}};
    Account::update_document(_chatAppDB, "accounts", filter_object, update_object);

    QJsonDocument contacts = Account::fetch_contacts_and_chats(_chatAppDB, phone_number);

    QJsonArray groups_array = json_doc.object()["groups"].toArray();
    QJsonObject group_filter{{"_id", QJsonObject{{"$in", groups_array}}}};
    QJsonDocument groups = Account::find_document(_chatAppDB, "groups", group_filter);

    QJsonObject message{{"type", "login_request"},
                        {"status", "succeeded"},
                        {"message", "loading your data..."},
                        {"contacts", QJsonValue::fromVariant(contacts.toVariant())},
                        {"groups", QJsonValue::fromVariant(groups.toVariant())}};

    _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
}

void server_manager::on_text_message_received(const QString &message)
{
    QJsonDocument json_doc = QJsonDocument::fromJson(message.toUtf8());
    if (json_doc.isNull() || !json_doc.isObject())
    {
        qWarning() << "Invalid JSON received.";
        return;
    }

    QJsonObject json_object = json_doc.object();
    MessageType type = _map.value(json_object["type"].toString());

    switch (type)
    {
    case SignUp:
        sign_up(json_object["phone_number"].toInt(), json_object["first_name"].toString(), json_object["last_name"].toString(), json_object["password"].toString(), json_object["secret_question"].toString(), json_object["secret_answer"].toString());
        break;
    case LoginRequest:
        login_request(json_object["phone_number"].toInt(), json_object["password"].toString(), json_object["time_zone"].toString());
        break;
    case TextMessage:
        break;
    case IsTyping:
        break;
    case SetName:
        break;
    case FileMessage:
        break;
    case AudioMessage:
        break;
    case SaveData:
        break;
    case ClientNewName:
        break;
    case ClientDisconnected:
        break;
    case ClientConnected:
        break;
    case AddedYou:
        break;
    case LookupFriend:
        break;
    case CreateConversation:
        break;
    case SaveMessage:
        break;
    case NewPasswordRequest:
        break;
    case UpdatePassword:
        break;
    case DeleteMessage:
        break;
    case DeleteGroupMessage:
        break;
    case NewGroup:
        break;
    case AddedToGroup:
        break;
    case GroupIsTyping:
        break;
    case GroupText:
        break;
    case GroupFile:
        break;
    case GroupAudio:
        break;
    case NewGroupMember:
        break;
    case RemoveGroupMember:
        break;
    case RequestData:
        break;
    case DeleteAccount:
        break;
    case LastMessageRead:
        break;
    case GroupLastMessageRead:
        break;
    default:
        qWarning() << "Unknown message type: " << json_object["type"].toString();
        break;
    }
}

void server_manager::map_initialization()
{
    _map["sign_up"] = SignUp;
    _map["login_request"] = LoginRequest;
    _map["is_typing"] = IsTyping;
    _map["set_name"] = SetName;
    _map["file"] = FileMessage;
    _map["audio"] = AudioMessage;
    _map["save_data"] = SaveData;
    _map["client_new_name"] = ClientNewName;
    _map["client_disconnected"] = ClientDisconnected;
    _map["client_connected"] = ClientConnected;
    _map["added_you"] = AddedYou;
    _map["lookup_friend"] = LookupFriend;
    _map["create_conversation"] = CreateConversation;
    _map["save_message"] = SaveMessage;
    _map["text"] = TextMessage;
    _map["new_password_request"] = NewPasswordRequest;
    _map["update_password"] = UpdatePassword;
    _map["delete_message"] = DeleteMessage;
    _map["delete_group_message"] = DeleteGroupMessage;
    _map["new_group"] = NewGroup;
    _map["added_to_group"] = AddedToGroup;
    _map["group_is_typing"] = GroupIsTyping;
    _map["group_text"] = GroupText;
    _map["group_file"] = GroupFile;
    _map["group_audio"] = GroupAudio;
    _map["new_group_member"] = NewGroupMember;
    _map["remove_group_member"] = RemoveGroupMember;
    _map["request_data"] = RequestData;
    _map["delete_account"] = DeleteAccount;
    _map["last_message_read"] = LastMessageRead;
    _map["group_last_message_read"] = GroupLastMessageRead;
    _map["invalid_type"] = InvalidType;
}