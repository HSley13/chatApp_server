#include "server_manager.hpp"

server_manager::server_manager(QObject *parent)
    : QObject(parent) {
    _server = new QWebSocketServer(QString("ChatApp Server"), QWebSocketServer::NonSecureMode, this);
    connect(_server, &QWebSocketServer::newConnection, this, &server_manager::on_new_connection);

    map_initialization();

    static mongocxx::instance instance{};
    static mongocxx::uri uri{std::getenv("MONGODB_URI")};

    static mongocxx::client connection{uri};
    if (!connection) {
        qDebug() << "DB initialization failed";
        return;
    }

    _chatAppDB = connection.database("chatAppDB");

    Aws::InitAPI(_options);

    Aws::Auth::AWSCredentials credentials(std::getenv("CHAT_APP_ACCESS_KEY"), std::getenv("CHAT_APP_SECRET_ACCESS_KEY"));
    Aws::Client::ClientConfiguration clientConfig;
    clientConfig.region = std::getenv("CHAT_APP_BUCKET_REGION");

    _s3_client = std::make_shared<Aws::S3::S3Client>(credentials, clientConfig, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, true);
    if (!_s3_client) {
        qDebug() << "S3Client initialization failed";
        return;
    }

    _server->listen(_ip, _port);
    qDebug() << "Server is running on port:" << _port;
}

server_manager::~server_manager() {
    Aws::ShutdownAPI(_options);
}

server_manager::server_manager(std::shared_ptr<QWebSocket> client, QObject *parent)
    : QObject(parent), _socket(client) { connect(_socket.get(), &QWebSocket::textMessageReceived, this, &server_manager::on_text_message_received); }

void server_manager::on_new_connection() {
    std::shared_ptr<QWebSocket> client(_server->nextPendingConnection());

    connect(client.get(), &QWebSocket::disconnected, this, &server_manager::on_client_disconnected);

    server_manager *server = new server_manager(client, this);
}

void server_manager::on_client_disconnected() {
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());
    if (client) {
        int id = client->property("id").toInt();
        _clients.remove(id);

        qDebug() << "Client: " << id << " is disconnected";

        QJsonObject filter_object{{"_id", id}};
        QJsonObject update_field{{"$set", QJsonObject{{"status", false}}}};
        Account::update_document(_chatAppDB, "accounts", filter_object, update_field);

        QJsonArray contactIDs = Account::fetch_contactIDs(_chatAppDB, id);
        for (const QJsonValue &ID : contactIDs) {
            std::shared_ptr<QWebSocket> client = _clients.value(ID.toInt());
            if (client) {
                QJsonObject message{{"type", "client_disconnected"},
                                    {"phone_number", id}};

                client->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
            }
        }
    }
}

void server_manager::sign_up(const int &phone_number, const QString &first_name, const QString &last_name, const QString &password, const QString &secret_question, const QString &secret_answer) {
    const QString &hashed_password = QString::fromStdString(Security::hashing_password(password.toStdString()));

    QJsonObject json_object{{"_id", phone_number},
                            {"first_name", first_name},
                            {"last_name", last_name},
                            {"image_url", QString()},
                            {"status", false},
                            {"hashed_password", hashed_password},
                            {"secret_question", secret_question},
                            {"secret_answer", secret_answer},
                            {"contacts", QJsonArray{}},
                            {"groups", QJsonArray{}}};

    bool succeeded_or_failed = Account::insert_document(_chatAppDB, "accounts", json_object);

    QJsonObject response_object{{"type", "sign_up"},
                                {"status", succeeded_or_failed},
                                {"message", succeeded_or_failed ? "Account Created Successfully, Reconnect" : "Failed to Create Account, try again"}};

    QJsonDocument response_doc(response_object);

    _socket->sendTextMessage(QString::fromUtf8(response_doc.toJson()));
}

void server_manager::login_request(const int &phone_number, const QString &password, const QString &time_zone) {
    if (!phone_number)
        return;

    QJsonObject filter_object{{"_id", phone_number}};
    QJsonDocument json_doc = Account::find_document(_chatAppDB, "accounts", filter_object);

    if (json_doc.isEmpty()) {
        QJsonObject json_message{{"type", "login_request"},
                                 {"status", false},
                                 {"message", "Account Doesn't exist in our Database, verify and try again"}};

        QJsonDocument json_doc(json_message);

        _socket->sendTextMessage(QString::fromUtf8(json_doc.toJson()));

        return;
    }

    if (!Security::verifying_password(password.toStdString(), json_doc.object()["hashed_password"].toString().toStdString())) {
        QJsonObject json_message{{"type", "login_request"},
                                 {"status", false},
                                 {"message", "Password Incorrect"}};

        QJsonDocument json_doc(json_message);

        _socket->sendTextMessage(QString::fromUtf8(json_doc.toJson()));

        return;
    }

    qDebug() << "Client: " << phone_number << " is connected";

    _clients.insert(phone_number, _socket);
    _socket->setProperty("id", phone_number);

    QJsonObject update_field{{"$set", QJsonObject{{"status", true}}}};
    Account::update_document(_chatAppDB, "accounts", filter_object, update_field);

    QJsonDocument contacts = Account::fetch_contacts_and_chats(_chatAppDB, phone_number);
    QJsonDocument groups = Account::fetch_groups_and_chats(_chatAppDB, phone_number);

    QJsonObject message{{"type", "login_request"},
                        {"status", true},
                        {"message", "loading your data..."},
                        {"my_info", json_doc.object()},
                        {"contacts", QJsonValue::fromVariant(contacts.toVariant())},
                        {"groups", QJsonValue::fromVariant(groups.toVariant())}};

    _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));

    QJsonArray contactIDs = Account::fetch_contactIDs(_chatAppDB, phone_number);
    for (const QJsonValue &ID : contactIDs) {
        std::shared_ptr<QWebSocket> client = _clients.value(ID.toInt());
        if (client) {
            QJsonObject message{{"type", "client_connected"},
                                {"phone_number", phone_number}};

            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
        }
    }
}

void server_manager::lookup_friend(const int &phone_number) {
    QJsonObject filter_object{{"_id", phone_number}};
    QJsonObject field{{"first_name", 1}};

    // Check if the account exists in the database
    QJsonDocument check_up = Account::find_document(_chatAppDB, "accounts", filter_object, field);
    if (check_up.isEmpty()) {
        QJsonObject message{{"type", "lookup_friend"},
                            {"status", "failed"},
                            {"message", "The Account: " + QString::number(phone_number) + " doesn't exist in our Database"}};

        _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
        return;
    }

    // Generate a new chat ID
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(1, std::numeric_limits<int>::max());
    int chatID = distribution(generator);

    // Add friend to the user's contact list
    if (_clients.key(_socket) != phone_number) // Check to avoid adding the user to their own contact list
    {
        QJsonObject push_object{{"contacts", QJsonObject{{"contactID", _clients.key(_socket)},
                                                         {"chatID", chatID},
                                                         {"unread_messages", 1}}}};
        QJsonObject update_object{{"$push", push_object}};
        Account::update_document(_chatAppDB, "accounts", filter_object, update_object);
    }

    // Prepare and insert the first message into the new chat
    QJsonArray messages_array;
    QJsonObject first_message{{"message", "Server: New Conversation"},
                              {"sender", chatID},
                              {"time", QDateTime::currentDateTimeUtc().toString()}};
    messages_array.append(first_message);

    QJsonObject insert_object{{"_id", chatID},
                              {"messages", messages_array}};
    Account::insert_document(_chatAppDB, "chats", insert_object);

    // Fetch contact info and send a message to the friend (if online)
    QJsonObject fields{{"_id", 1},
                       {"status", 1},
                       {"first_name", 1},
                       {"last_name", 1},
                       {"image_url", 1}};
    std::shared_ptr<QWebSocket> client = _clients.value(phone_number);
    if (client) {
        filter_object[QStringLiteral("_id")] = _clients.key(_socket);

        QJsonDocument contact_info = Account::find_document(_chatAppDB, "accounts", filter_object, fields);

        QJsonObject obj1{{"contactInfo", contact_info.object()},
                         {"chatMessages", messages_array},
                         {"chatID", chatID}};

        QJsonArray json_array;
        json_array.append(obj1);

        QJsonObject message{{"type", "added_you"},
                            {"message", _socket->property("id").toString() + " added You as Friend"},
                            {"json_array", json_array}};

        client->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
    }

    // Add the user to the friend's contact list if they're not the same user
    if (_clients.key(_socket) != phone_number) {
        QJsonObject push_object{{"contacts", QJsonObject{{"contactID", phone_number},
                                                         {"chatID", chatID},
                                                         {"unread_messages", 1}}}};
        QJsonObject update_object{{"$push", push_object}};
        Account::update_document(_chatAppDB, "accounts", filter_object, update_object);
    }

    // Fetch contact info and send a success message to the user
    filter_object[QStringLiteral("_id")] = phone_number;

    QJsonDocument contact_info2 = Account::find_document(_chatAppDB, "accounts", filter_object, fields);

    QJsonObject obj2{{"contactInfo", contact_info2.object()},
                     {"chatMessages", messages_array},
                     {"chatID", chatID}};

    QJsonArray json_array2;
    json_array2.append(obj2);

    QJsonObject message2{{"type", "lookup_friend"},
                         {"status", "succeeded"},
                         {"message", QString::number(phone_number) + " also known as " + check_up.object()["first_name"].toString() + " is now Your friend"},
                         {"json_array", json_array2}};

    _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message2).toJson()));
}

void server_manager::profile_image(const QString &file_name, const QString &data) {
    QByteArray decoded_data = QByteArray::fromBase64(data.toUtf8());
    std::string decoded_string = decoded_data.toStdString();

    std::string presigned_url = S3::store_data_to_s3(*_s3_client, file_name.toStdString(), decoded_string);

    QJsonObject filter_object{{"_id", _clients.key(_socket)}};
    QJsonObject update_field{{"$set", QJsonObject{{"image_url", QString::fromStdString(presigned_url)}}}};
    Account::update_document(_chatAppDB, "accounts", filter_object, update_field);

    QJsonObject message1{{"type", "profile_image"},
                         {"image_url", QString::fromStdString(presigned_url)}};

    _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message1).toJson()));

    QJsonArray contactIDs = Account::fetch_contactIDs(_chatAppDB, _clients.key(_socket));
    for (const QJsonValue &ID : contactIDs) {
        std::shared_ptr<QWebSocket> client = _clients.value(ID.toInt());
        if (client) {
            QJsonObject message2{{"type", "client_profile_image"},
                                 {"phone_number", _clients.key(_socket)},
                                 {"image_url", QString::fromStdString(presigned_url)}};

            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message2).toJson()));
        };
    }
}

void server_manager::group_profile_image(const int &group_ID, const QString &file_name, const QString &data) {
    QByteArray decoded_data = QByteArray::fromBase64(data.toUtf8());
    std::string decoded_string = decoded_data.toStdString();

    std::string url = S3::store_data_to_s3(*_s3_client, file_name.toStdString(), decoded_string);

    QJsonObject filter_object{{"_id", group_ID}};
    QJsonObject update_field{{"$set", QJsonObject{{"group_image_url", QString::fromStdString(url)}}}};
    Account::update_document(_chatAppDB, "groups", filter_object, update_field);

    QJsonObject message{{"type", "group_profile_image"},
                        {"groupID", group_ID},
                        {"group_image_url", QString::fromStdString(url)}};

    QJsonDocument json_doc = Account::find_document(_chatAppDB, "groups", filter_object, QJsonObject{{"_id", 0}, {"group_members", 1}});
    for (const QJsonValue &phone_number : json_doc.object().value("group_members").toArray()) {
        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client)
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson()));
    }
}

void server_manager::profile_image_deleted() {
    QJsonObject filter_object{{"_id", _clients.key(_socket)}};
    QJsonObject update_field{{"$set", QJsonObject{{"image_url", QString(std::getenv("AWS_LINK")) + "contact.png"}}}};
    Account::update_document(_chatAppDB, "accounts", filter_object, update_field);

    QJsonArray contactIDs = Account::fetch_contactIDs(_chatAppDB, _clients.key(_socket));
    for (const QJsonValue &ID : contactIDs) {
        std::shared_ptr<QWebSocket> client = _clients.value(ID.toInt());
        if (client) {
            QJsonObject message2{{"type", "client_profile_image"},
                                 {"phone_number", _clients.key(_socket)},
                                 {"image_url", QString(std::getenv("AWS_LINK")) + "contact.png"}};

            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message2).toJson()));
        };
    }
}

void server_manager::text_received(const int &receiver, const QString &message, const QString &time, const int &chat_ID) {
    QJsonObject message_obj{{"type", "text"},
                            {"chatID", chat_ID},
                            {"sender_ID", _clients.key(_socket)},
                            {"message", message},
                            {"time", time}};

    _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));

    std::shared_ptr<QWebSocket> client = _clients.value(receiver);
    if (client)
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));

    QJsonObject filter_object{{"_id", chat_ID}};

    QJsonObject push_object{{"messages", QJsonObject{{"message", message},
                                                     {"sender", _clients.key(_socket)},
                                                     {"time", time}}}};

    QJsonObject update_object{{"$push", push_object}};
    Account::update_document(_chatAppDB, "chats", filter_object, update_object);

    QJsonObject filter_object2{{"_id", receiver}, {"contacts.chatID", chat_ID}};
    QJsonObject increment_object{{"$inc", QJsonObject{{"contacts.$.unread_messages", 1}}}};

    Account::update_document(_chatAppDB, "accounts", filter_object2, increment_object);
}

void server_manager::new_group(const QString &group_name, QJsonArray group_members) {
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(1, std::numeric_limits<int>::max());

    int groupID = distribution(generator);

    QJsonArray messages_array;
    QJsonObject first_message{{"message", "New Group Created"},
                              {"sender_ID", groupID},
                              {"sender_name", "Server"},
                              {"time", QDateTime::currentDateTimeUtc().toString()}};
    messages_array.append(first_message);

    QJsonObject new_group{{"_id", groupID},
                          {"group_name", group_name},
                          {"group_admin", _clients.key(_socket)},
                          {"group_image_url", QString(std::getenv("AWS_LINK")) + "networking.png"},
                          {"group_members", group_members},
                          {"group_messages", messages_array}};
    Account::insert_document(_chatAppDB, "groups", new_group);

    QJsonObject push_object{{"groups", QJsonObject{{"groupID", groupID},
                                                   {"group_unread_messages", 1}}}};
    QJsonObject update_object{{"$push", push_object}};
    for (const QJsonValue &phone_number : group_members) {
        QJsonObject filter_object{{"_id", phone_number.toInt()}};
        Account::update_document(_chatAppDB, "accounts", filter_object, update_object);

        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client) {
            QJsonObject group_info{{"_id", groupID},
                                   {"group_name", group_name},
                                   {"group_admin", _clients.key(_socket)},
                                   {"group_messages", messages_array},
                                   {"group_members", group_members},
                                   {"group_image_url", QString(std::getenv("AWS_LINK")) + "networking.png"},
                                   {"group_unread_messages", 1}};

            QJsonArray groups;
            groups.append(group_info);

            QString notification = QString("%1 %2").arg("You were added to a new Group name: ", group_name);

            QJsonObject message1{{"type", "added_to_group"},
                                 {"message", notification},
                                 {"groups", groups}};

            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message1).toJson()));
        }
    }
}

void server_manager::group_text_received(const int &groupID, QString sender_name, const QString &message, const QString &time) {
    QJsonObject filter_object{{"_id", groupID}};

    QJsonObject message_obj{{"type", "group_text"},
                            {"groupID", groupID},
                            {"sender_ID", _clients.key(_socket)},
                            {"sender_name", sender_name},
                            {"message", message},
                            {"time", time}};

    QJsonObject increment_object{{"$inc", QJsonObject{{"groups.$.group_unread_messages", 1}}}};

    QJsonDocument json_doc = Account::find_document(_chatAppDB, "groups", filter_object, QJsonObject{{"_id", 0}, {"group_members", 1}});
    for (const QJsonValue &phone_number : json_doc.object().value("group_members").toArray()) {
        QJsonObject account_filter{{"_id", phone_number.toInt()}, {"groups.groupID", groupID}};
        Account::update_document(_chatAppDB, "accounts", account_filter, increment_object);

        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client)
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
    }

    QJsonObject push_object{{"group_messages", QJsonObject{{"message", message},
                                                           {"sender_ID", _clients.key(_socket)},
                                                           {"sender_name", sender_name},
                                                           {"time", time}}}};

    QJsonObject update_object{{"$push", push_object}};
    Account::update_document(_chatAppDB, "groups", filter_object, update_object);
}

void server_manager::file_received(const int &chatID, const int &receiver, const QString &file_name, const QString &file_data, const QString &time) {
    QByteArray decoded_data = QByteArray::fromBase64(file_data.toUtf8());
    std::string decoded_string = decoded_data.toStdString();

    std::string file_url = S3::store_data_to_s3(*_s3_client, file_name.toStdString(), decoded_string);

    QJsonObject message_obj{{"type", "file"},
                            {"chatID", chatID},
                            {"sender_ID", _clients.key(_socket)},
                            {"file_url", QString::fromStdString(file_url)},
                            {"time", time}};
    std::shared_ptr<QWebSocket> client = _clients.value(receiver);
    if (client)
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));

    _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));

    QJsonObject filter_object{{"_id", chatID}};

    QJsonObject push_field{{"file_url", QString::fromStdString(file_url)},
                           {"sender", _clients.key(_socket)},
                           {"time", time}};
    QJsonObject push_object{{"messages", push_field}};

    QJsonObject update_object{{"$push", push_object}};

    Account::update_document(_chatAppDB, "chats", filter_object, update_object);

    QJsonObject account_filter{{"_id", receiver}, {"contacts.chatID", chatID}};
    QJsonObject increment_object{{"$inc", QJsonObject{{"contacts.$.unread_messages", 1}}}};

    Account::update_document(_chatAppDB, "accounts", account_filter, increment_object);
}

void server_manager::group_file_received(const int &groupID, const QString &sender_name, const QString &file_name, const QString &file_data, const QString &time) {
    QByteArray decoded_data = QByteArray::fromBase64(file_data.toUtf8());
    std::string decoded_string = decoded_data.toStdString();

    std::string file_url = S3::store_data_to_s3(*_s3_client, file_name.toStdString(), decoded_string);

    QJsonObject filter_object{{"_id", groupID}};

    QJsonObject message_obj{{"type", "group_file"},
                            {"groupID", groupID},
                            {"sender_ID", _clients.key(_socket)},
                            {"sender_name", sender_name},
                            {"file_url", QString::fromStdString(file_url)},
                            {"time", time}};

    QJsonObject increment_object{{"$inc", QJsonObject{{"groups.$.group_unread_messages", 1}}}};

    QJsonDocument json_doc = Account::find_document(_chatAppDB, "groups", filter_object, QJsonObject{{"_id", 0}, {"group_members", 1}});
    for (const QJsonValue &phone_number : json_doc.object().value("group_members").toArray()) {
        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client)
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
    }

    QJsonObject push_object{{"group_messages", QJsonObject{{"file_url", QString::fromStdString(file_url)},
                                                           {"sender_ID", _clients.key(_socket)},
                                                           {"sender_name", sender_name},
                                                           {"time", time}}}};

    QJsonObject update_object{{"$push", push_object}};
    Account::update_document(_chatAppDB, "groups", filter_object, update_object);
}

void server_manager::is_typing_received(const int &receiver) {
    std::shared_ptr<QWebSocket> client = _clients.value(receiver);
    if (client) {
        QJsonObject message_obj{{"type", "is_typing"},
                                {"sender_ID", _clients.key(_socket)}};

        client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
    }
}

void server_manager::group_is_typing_received(const int &groupID, const QString &sender_name) {
    QJsonObject filter_object{{"_id", groupID}};

    QJsonObject message_obj{{"type", "group_is_typing"},
                            {"groupID", groupID},
                            {"sender_name", sender_name}};

    QJsonDocument json_doc = Account::find_document(_chatAppDB, "groups", filter_object, QJsonObject{{"_id", 0}, {"group_members", 1}});
    for (const QJsonValue &phone_number : json_doc.object().value("group_members").toArray()) {
        if (phone_number.toInt() == _clients.key(_socket))
            continue;

        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client)
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
    }
}

void server_manager::update_info_received(const QString &first_name, const QString &last_name, const QString &password) {
    const QString &hashed_password = QString::fromStdString(Security::hashing_password(password.toStdString()));

    QJsonObject filter_object{{"_id", _clients.key(_socket)}};
    QJsonObject update_field{{"$set", QJsonObject{{"first_name", first_name},
                                                  {"last_name", last_name},
                                                  {"hashed_password", hashed_password}}}};
    Account::update_document(_chatAppDB, "accounts", filter_object, update_field);

    QJsonArray contactIDs = Account::fetch_contactIDs(_chatAppDB, _clients.key(_socket));
    for (const QJsonValue &ID : contactIDs) {
        std::shared_ptr<QWebSocket> client = _clients.value(ID.toInt());
        if (client) {
            QJsonObject message2{{"type", "contact_info_updated"},
                                 {"phone_number", _clients.key(_socket)},
                                 {"first_name", first_name},
                                 {"last_name", last_name}};

            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message2).toJson()));
        };
    }
}

void server_manager::update_password(const int &phone_number, const QString &password) {
    const QString &hashed_password = QString::fromStdString(Security::hashing_password(password.toStdString()));

    QJsonObject filter_object{{"_id", phone_number}};
    QJsonObject update_field{{"$set", QJsonObject{{"hashed_password", hashed_password}}}};
    Account::update_document(_chatAppDB, "accounts", filter_object, update_field);
}

void server_manager::retrieve_question(const int &phone_number) {
    QJsonObject filter_object{{"_id", phone_number}};

    QJsonDocument json_doc = Account::find_document(_chatAppDB, "accounts", filter_object, QJsonObject{{"secret_question", 1}, {"secret_answer", 1}});

    QJsonObject message_obj{{"type", "question_answer"},
                            {"secret_question", json_doc.object()["secret_question"].toString()},
                            {"secret_answer", json_doc.object()["secret_answer"].toString()}};

    _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
}

void server_manager::remove_group_member(const int &groupID, QJsonArray group_members) {
    QJsonObject filter_object{{"_id", groupID}};

    QJsonObject pull_elements{{"$in", group_members}};
    QJsonObject pull_object{{"group_members", pull_elements}};
    QJsonObject update_object{{"$pull", pull_object}};
    Account::update_document(_chatAppDB, "groups", filter_object, update_object);

    for (const QJsonValue &phone_number : group_members) {
        QJsonObject filter_object2{{"_id", phone_number}};

        QJsonObject pull_object2{{"groups", QJsonObject{{"groupID", groupID}}}};
        QJsonObject update_object2{{"$pull", pull_object2}};

        Account::update_document(_chatAppDB, "accounts", filter_object2, update_object2);
        QString message = QString("You have been removed from the group: %1").arg(QString::number(groupID));

        QJsonObject message_obj{{"type", "removed_from_group"},
                                {"message", message},
                                {"groupID", groupID}};

        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client)
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
    }

    Account::update_document(_chatAppDB, "accounts", filter_object, update_object);

    QJsonDocument json_doc = Account::find_document(_chatAppDB, "groups", filter_object, QJsonObject{{"_id", 0}, {"group_members", 1}});
    for (const QJsonValue &phone_number : json_doc.object().value("group_members").toArray()) {
        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client) {
            QJsonObject message_obj{{"type", "remove_group_member"},
                                    {"groupID", groupID},
                                    {"group_members", group_members}};

            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
        }
    }
}

void server_manager::add_group_member(const int &groupID, QJsonArray group_members) {
    QJsonObject filter_object{{"_id", groupID}};

    QJsonDocument json_doc = Account::find_document(_chatAppDB, "groups", filter_object, QJsonObject{{"_id", 0}, {"group_members", 1}});

    QJsonArray current_group_members = json_doc.object().value("group_members").toArray();
    for (const QJsonValue &phone_number : current_group_members) {
        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client) {
            QJsonObject message_obj{{"type", "add_group_member"},
                                    {"groupID", groupID},
                                    {"group_members", group_members}};

            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
        }
    }

    QJsonObject push_elements{{"$each", group_members}};
    QJsonObject push_object{{"group_members", push_elements}};
    QJsonObject update_object{{"$push", push_object}};

    Account::update_document(_chatAppDB, "groups", filter_object, update_object);

    QJsonDocument updated_group_doc = Account::find_document(_chatAppDB, "groups", filter_object);
    QJsonObject updated_group = updated_group_doc.object();

    for (const QJsonValue &phone_number : group_members) {
        QJsonObject filter_object2{{"_id", phone_number.toInt()}};

        QJsonObject push_object2{{"groups", QJsonObject{{"groupID", groupID},
                                                        {"group_unread_messages", 1}}}};
        QJsonObject update_object2{{"$push", push_object2}};
        Account::update_document(_chatAppDB, "accounts", filter_object2, update_object2);

        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client) {
            QJsonObject group_info{{"_id", groupID},
                                   {"group_name", updated_group.value("group_name").toString()},
                                   {"group_admin", updated_group.value("group_admin").toInt()},
                                   {"group_messages", updated_group.value("group_messages").toArray()},
                                   {"group_members", updated_group.value("group_members").toArray()},
                                   {"group_image_url", updated_group.value("group_image_url").toString()},
                                   {"unread_messages", 1}};

            QJsonArray groups;
            groups.append(group_info);

            QJsonObject message1{{"type", "added_to_group"},
                                 {"groups", groups}};

            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message1).toJson()));
        }
    }
}

void server_manager::delete_message(const int &receiver, const int &chat_ID, const QString &full_time) {
    QJsonObject message_obj{{"type", "delete_message"},
                            {"chatID", chat_ID},
                            {"full_time", full_time}};

    _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));

    std::shared_ptr<QWebSocket> client = _clients.value(receiver);
    if (client)
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));

    QJsonObject filter_object{{"_id", chat_ID}};

    QJsonObject pull_field{{"messages", QJsonObject{{"time", full_time}}}};
    QJsonObject update_object{{"$pull", pull_field}};

    Account::update_document(_chatAppDB, "chats", filter_object, update_object);
}

void server_manager::delete_group_message(const int &groupID, const QString &full_time) {
    QJsonObject filter_object{{"_id", groupID}};

    QJsonObject message_obj{{"type", "delete_group_message"},
                            {"groupID", groupID},
                            {"full_time", full_time}};

    QJsonDocument json_doc = Account::find_document(_chatAppDB, "groups", filter_object, QJsonObject{{"_id", 0}, {"group_members", 1}});
    for (const QJsonValue &phone_number : json_doc.object().value("group_members").toArray()) {
        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client)
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
    }

    QJsonObject pull_field{{"group_messages", QJsonObject{{"time", full_time}}}};
    QJsonObject update_object{{"$pull", pull_field}};

    Account::update_document(_chatAppDB, "groups", filter_object, update_object);
}

void server_manager::update_unread_message(const int &chatID) {
    QJsonObject filter_object{{"_id", _clients.key(_socket)},
                              {"contacts.chatID", chatID}};

    QJsonObject update_object{{"$set", QJsonObject{{"contacts.$.unread_messages", 0}}}};

    Account::update_document(_chatAppDB, "accounts", filter_object, update_object);
}

void server_manager::update_group_unread_message(const int &groupID) {
    QJsonObject filter_object{{"_id", _clients.key(_socket)},
                              {"groups.groupID", groupID}};

    QJsonObject update_object{{"$set", QJsonObject{{"groups.$.group_unread_messages", 0}}}};

    Account::update_document(_chatAppDB, "accounts", filter_object, update_object);
}

void server_manager::delete_account() {
    Account::delete_account(_chatAppDB, _clients.key(_socket));
}

void server_manager::audio_received(const int &chatID, const int &receiver, const QString &audio_name, const QString &audio_data, const QString &time) {
    QByteArray decoded_data = QByteArray::fromBase64(audio_data.toUtf8());
    std::string decoded_string = decoded_data.toStdString();

    std::string audio_url = S3::store_data_to_s3(*_s3_client, audio_name.toStdString(), decoded_string);

    QJsonObject message_obj{{"type", "audio"},
                            {"chatID", chatID},
                            {"sender_ID", _clients.key(_socket)},
                            {"audio_url", QString::fromStdString(audio_url)},
                            {"time", time}};
    std::shared_ptr<QWebSocket> client = _clients.value(receiver);
    if (client)
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));

    _socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));

    QJsonObject filter_object{{"_id", chatID}};

    QJsonObject push_field{{"audio_url", QString::fromStdString(audio_url)},
                           {"sender", _clients.key(_socket)},
                           {"time", time}};
    QJsonObject push_object{{"messages", push_field}};

    QJsonObject update_object{{"$push", push_object}};

    Account::update_document(_chatAppDB, "chats", filter_object, update_object);

    QJsonObject account_filter{{"_id", receiver}, {"contacts.chatID", chatID}};
    QJsonObject increment_object{{"$inc", QJsonObject{{"contacts.$.unread_messages", 1}}}};

    Account::update_document(_chatAppDB, "accounts", account_filter, increment_object);
}

void server_manager::group_audio_received(const int &groupID, const QString &sender_name, const QString &audio_name, const QString &audio_data, const QString &time) {
    QByteArray decoded_data = QByteArray::fromBase64(audio_data.toUtf8());
    std::string decoded_string = decoded_data.toStdString();

    std::string audio_url = S3::store_data_to_s3(*_s3_client, audio_name.toStdString(), decoded_string);

    QJsonObject filter_object{{"_id", groupID}};

    QJsonObject message_obj{{"type", "group_audio"},
                            {"groupID", groupID},
                            {"sender_ID", _clients.key(_socket)},
                            {"sender_name", sender_name},
                            {"audio_url", QString::fromStdString(audio_url)},
                            {"time", time}};

    QJsonObject increment_object{{"$inc", QJsonObject{{"groups.$.group_unread_messages", 1}}}};

    QJsonDocument json_doc = Account::find_document(_chatAppDB, "groups", filter_object, QJsonObject{{"_id", 0}, {"group_members", 1}});
    for (const QJsonValue &phone_number : json_doc.object().value("group_members").toArray()) {
        std::shared_ptr<QWebSocket> client = _clients.value(phone_number.toInt());
        if (client)
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(message_obj).toJson()));
    }

    QJsonObject push_object{{"group_messages", QJsonObject{{"audio_url", QString::fromStdString(audio_url)},
                                                           {"sender_ID", _clients.key(_socket)},
                                                           {"sender_name", sender_name},
                                                           {"time", time}}}};

    QJsonObject update_object{{"$push", push_object}};
    Account::update_document(_chatAppDB, "groups", filter_object, update_object);
}

void server_manager::on_text_message_received(const QString &message) {
    QJsonDocument json_doc = QJsonDocument::fromJson(message.toUtf8());
    if (json_doc.isNull() || !json_doc.isObject()) {
        qWarning() << "Invalid JSON received.";
        return;
    }

    QJsonObject json_object = json_doc.object();
    MessageType type = _map.value(json_object["type"].toString());

    switch (type) {
    case SignUp:
        sign_up(json_object["phone_number"].toInt(), json_object["first_name"].toString(), json_object["last_name"].toString(), json_object["password"].toString(), json_object["secret_question"].toString(), json_object["secret_answer"].toString());
        break;
    case LoginRequest:
        login_request(json_object["phone_number"].toInt(), json_object["password"].toString(), json_object["time_zone"].toString());
        break;
    case LookupFriend:
        lookup_friend(json_object["phone_number"].toInt());
        break;
    case ProfileImage:
        profile_image(json_object["file_name"].toString(), json_object["file_data"].toString());
        break;
    case GroupProfileImage:
        group_profile_image(json_object["groupID"].toInt(), json_object["file_name"].toString(), json_object["file_data"].toString());
        break;
    case ProfileImageDeleted:
        profile_image_deleted();
        break;
    case Text:
        text_received(json_object["receiver"].toInt(), json_object["message"].toString(), json_object["time"].toString(), json_object["chatID"].toInt());
        break;
    case NewGroup:
        new_group(json_object["group_name"].toString(), json_object["group_members"].toArray());
        break;
    case GroupText:
        group_text_received(json_object["groupID"].toInt(), json_object["sender_name"].toString(), json_object["message"].toString(), json_object["time"].toString());
        break;
    case File:
        file_received(json_object["chatID"].toInt(), json_object["receiver"].toInt(), json_object["file_name"].toString(), json_object["file_data"].toString(), json_object["time"].toString());
        break;
    case GroupFile:
        group_file_received(json_object["groupID"].toInt(), json_object["sender_name"].toString(), json_object["file_name"].toString(), json_object["file_data"].toString(), json_object["time"].toString());
        break;
    case IsTyping:
        is_typing_received(json_object["receiver"].toInt());
        break;
    case GroupIsTyping:
        group_is_typing_received(json_object["groupID"].toInt(), json_object["sender_name"].toString());
        break;
    case UpdateInfo:
        update_info_received(json_object["first_name"].toString(), json_object["last_name"].toString(), json_object["password"].toString());
        break;
    case UpdatePassword:
        update_password(json_object["phone_number"].toInt(), json_object["password"].toString());
        break;
    case RetrieveQuestion:
        retrieve_question(json_object["phone_number"].toInt());
        break;
    case RemoveGroupMember:
        remove_group_member(json_object["groupID"].toInt(), json_object["group_members"].toArray());
        break;
    case AddGroupMember:
        add_group_member(json_object["groupID"].toInt(), json_object["group_members"].toArray());
        break;
    case DeleteMessage:
        delete_message(json_object["receiver"].toInt(), json_object["chatID"].toInt(), json_object["full_time"].toString());
        break;
    case DeleteGroupMessage:
        delete_group_message(json_object["groupID"].toInt(), json_object["full_time"].toString());
        break;
    case UpdateUnreadMessage:
        update_unread_message(json_object["chatID"].toInt());
        break;
    case UpdateGroupUnreadMessage:
        update_group_unread_message(json_object["groupID"].toInt());
        break;
    case DeleteAccount:
        delete_account();
        break;
    case Audio:
        audio_received(json_object["chatID"].toInt(), json_object["receiver"].toInt(), json_object["audio_name"].toString(), json_object["audio_data"].toString(), json_object["time"].toString());
        break;
    case GroupAudio:
        group_audio_received(json_object["groupID"].toInt(), json_object["sender_name"].toString(), json_object["audio_name"].toString(), json_object["audio_data"].toString(), json_object["time"].toString());
        break;
    default:
        qWarning() << "Unknown message type: " << json_object["type"].toString();
        break;
    }
}

void server_manager::map_initialization() {
    _map["sign_up"] = SignUp;
    _map["login_request"] = LoginRequest;
    _map["is_typing"] = IsTyping;
    _map["profile_image"] = ProfileImage;
    _map["group_profile_image"] = GroupProfileImage;
    _map["profile_image_deleted"] = ProfileImageDeleted;
    _map["client_disconnected"] = ClientDisconnected;
    _map["client_connected"] = ClientConnected;
    _map["lookup_friend"] = LookupFriend;
    _map["new_group"] = NewGroup;
    _map["text"] = Text;
    _map["group_text"] = GroupText;
    _map["added_to_group"] = AddedToGroup;
    _map["file"] = File;
    _map["group_file"] = GroupFile;
    _map["group_is_typing"] = GroupIsTyping;
    _map["contact_info_updated"] = UpdateInfo;
    _map["update_password"] = UpdatePassword;
    _map["retrieve_question"] = RetrieveQuestion;
    _map["remove_group_member"] = RemoveGroupMember;
    _map["add_group_member"] = AddGroupMember;
    _map["delete_message"] = DeleteMessage;
    _map["delete_group_message"] = DeleteGroupMessage;
    _map["update_unread_message"] = UpdateUnreadMessage;
    _map["update_group_unread_message"] = UpdateGroupUnreadMessage;
    _map["delete_account"] = DeleteAccount;
    _map["audio"] = Audio;
    _map["group_audio"] = GroupAudio;
}
