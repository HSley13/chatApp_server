#include "database.hpp"

std::string S3::get_data_from_s3(const Aws::S3::S3Client &s3_client, const std::string &key) {
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(std::getenv("CHAT_APP_BUCKET_NAME"));
    request.SetKey(key.c_str());

    Aws::S3::Model::GetObjectOutcome outcome = s3_client.GetObject(request);
    if (outcome.IsSuccess()) {
        Aws::IOStream &retrieved_object = outcome.GetResultWithOwnership().GetBody();
        std::stringstream ss;
        ss << retrieved_object.rdbuf();

        return ss.str();
    } else {
        std::cerr << "GetObject error: " << outcome.GetError().GetExceptionName() << " - " << outcome.GetError().GetMessage() << std::endl;

        return std::string();
    }
}

std::string S3::store_data_to_s3(Aws::S3::S3Client &s3_client, const std::string &key, const std::string &data) {
    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(std::getenv("CHAT_APP_BUCKET_NAME"));
    request.SetKey(key.c_str());

    auto data_stream = Aws::MakeShared<Aws::StringStream>("PutObjectStream");
    *data_stream << data;
    request.SetBody(data_stream);

    Aws::S3::Model::PutObjectOutcome outcome = s3_client.PutObject(request);
    if (outcome.IsSuccess()) {
        std::cout << "Successfully uploaded object " << key << std::endl;

        Aws::String presigned_url = s3_client.GeneratePresignedUrl(request.GetBucket(), request.GetKey(), Aws::Http::HttpMethod::HTTP_GET, 604800);

        return presigned_url.c_str();
    } else {
        std::cerr << "PutObject error: "
                  << outcome.GetError().GetExceptionName() << " - "
                  << outcome.GetError().GetMessage() << std::endl;

        return std::string();
    }
}

bool S3::delete_data_from_s3(const Aws::S3::S3Client &s3_client, const std::string &key) {
    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(std::getenv("CHAT_APP_BUCKET_NAME"));
    request.SetKey(key.c_str());

    Aws::S3::Model::DeleteObjectOutcome outcome = s3_client.DeleteObject(request);
    if (outcome.IsSuccess()) {
        std::cout << "Successfully deleted object from: " << std::getenv("CHAT_APP_BUCKET_NAME") << "/" << key << std::endl;

        return true;
    } else {
        std::cerr << "DeleteObject error: " << outcome.GetError().GetExceptionName() << " - " << outcome.GetError().GetMessage() << std::endl;

        return false;
    }
}

std::string Security::generate_random_salt(size_t length) {
    const std::string valid_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<> distribution(0, valid_chars.size() - 1);

    std::string salt;
    for (size_t i = 0; i < length; i++)
        salt.push_back(valid_chars[distribution(generator)]);

    return salt;
}

std::string Security::hashing_password(const std::string &password) {
    const size_t SALT_LENGTH = 32;

    std::string salt = generate_random_salt(SALT_LENGTH);

    const int t_cost = 2;
    const int m_cost = 65536;
    const int parallelism = 1;
    const int hash_length = 32;

    std::string hash;
    hash.resize(hash_length);

    int result = argon2_hash(t_cost, m_cost, parallelism, password.c_str(), password.length(), salt.c_str(), salt.length(), &hash[0], hash_length, nullptr, 0, Argon2_id, ARGON2_VERSION_NUMBER);

    if (result != ARGON2_OK) {
        std::cerr << "Error hashing password." << std::endl;

        exit(EXIT_FAILURE);
    }

    std::string hashed_password = salt;
    hashed_password.append(hash);

    return hashed_password;
}

bool Security::verifying_password(const std::string &inputPassword, const std::string &hashed_password) {
    const int t_cost = 2;
    const int m_cost = 65536;
    const int parallelism = 1;
    const int hash_length = 32;

    const size_t SALT_LENGTH = hashed_password.length() - hash_length;

    std::string hash;
    hash.resize(hash_length);

    int result = argon2_hash(t_cost, m_cost, parallelism, inputPassword.c_str(), inputPassword.length(), hashed_password.c_str(), SALT_LENGTH, &hash[0], hash_length, nullptr, 0, Argon2_id, ARGON2_VERSION_NUMBER);

    if (result != ARGON2_OK) {
        std::cerr << "Error verifying password." << std::endl;
        exit(EXIT_FAILURE);
    }

    return !hashed_password.substr(SALT_LENGTH, hash_length).compare(hash);
}

bool Account::insert_document(mongocxx::database &db, const std::string &collection_name, const QJsonObject &json_object) {
    try {
        mongocxx::collection collection = db.collection(collection_name);

        QString json_string = QJsonDocument(json_object).toJson(QJsonDocument::Compact);
        bsoncxx::document::value document = bsoncxx::from_json(json_string.toStdString());

        mongocxx::stdx::optional<mongocxx::result::insert_one> result = collection.insert_one(document.view());

        return result && result->result().inserted_count() == 1;
    } catch (const mongocxx::exception &e) {
        std::cerr << "MongoDB Exception: " << e.what() << std::endl;

        return false;
    } catch (const std::exception &e) {
        std::cerr << "Error inserting document: " << e.what() << std::endl;

        return false;
    }
}

bool Account::delete_document(mongocxx::database &db, const std::string &collection_name, const QJsonObject &filter_object) {
    try {
        mongocxx::collection collection = db.collection(collection_name);

        QString json_string = QJsonDocument(filter_object).toJson(QJsonDocument::Compact);
        bsoncxx::document::value filter = bsoncxx::from_json(json_string.toStdString());

        mongocxx::stdx::optional<mongocxx::result::delete_result> result = collection.delete_one(filter.view());

        return result && result->deleted_count() == 1;
    } catch (const mongocxx::exception &e) {
        std::cerr << "MongoDB Exception: " << e.what() << std::endl;

        return false;
    } catch (const std::exception &e) {
        std::cerr << "Error deleting document: " << e.what() << std::endl;

        return false;
    }
}

bool Account::update_document(mongocxx::database &db, const std::string &collection_name, const QJsonObject &filter_object, const QJsonObject &update_object) {
    try {
        mongocxx::collection collection = db.collection(collection_name);

        QString filter_json_string = QJsonDocument(filter_object).toJson(QJsonDocument::Compact);

        bsoncxx::document::value filter = bsoncxx::from_json(filter_json_string.toStdString());

        QString update_json_string = QJsonDocument(update_object).toJson(QJsonDocument::Compact);

        bsoncxx::document::value update = bsoncxx::from_json(update_json_string.toStdString());

        mongocxx::stdx::optional<mongocxx::result::update> result = collection.update_one(filter.view(), update.view());

        return result && result->modified_count() == 1;
    } catch (const mongocxx::exception &e) {
        std::cerr << "MongoDB Exception: " << e.what() << std::endl;

        return false;
    } catch (const std::exception &e) {
        std::cerr << "std Exception: " << e.what() << std::endl;

        return false;
    }
}

QJsonDocument Account::find_document(mongocxx::database &db, const std::string &collection_name, const QJsonObject &filter_object, const QJsonObject &fields) {
    try {
        mongocxx::collection collection = db.collection(collection_name);

        QString filter_json_string = QJsonDocument(filter_object).toJson(QJsonDocument::Compact);
        bsoncxx::document::value filter = bsoncxx::from_json(filter_json_string.toStdString());

        QString fields_json_string = QJsonDocument(fields).toJson(QJsonDocument::Compact);
        bsoncxx::document::value projection = bsoncxx::from_json(fields_json_string.toStdString());

        mongocxx::options::find find_options;
        find_options.projection(projection.view());

        mongocxx::cursor cursor = collection.find(filter.view(), find_options);

        QJsonArray result_array;
        for (const bsoncxx::document::view &doc : cursor) {
            QString json_string = QString::fromStdString(bsoncxx::to_json(doc));
            QJsonDocument json_doc = QJsonDocument::fromJson(json_string.toUtf8());

            result_array.append(json_doc.object());
        }

        if (result_array.isEmpty())
            return QJsonDocument();

        return (result_array.size() == 1) ? QJsonDocument(result_array.first().toObject()) : QJsonDocument(result_array);
    } catch (const mongocxx::exception &e) {
        std::cerr << "MongoDB Exception: " << e.what() << std::endl;

        return QJsonDocument();
    } catch (const std::exception &e) {
        std::cerr << "std Exception: " << e.what() << std::endl;

        return QJsonDocument();
    }
}

QJsonDocument Account::fetch_contacts_and_chats(mongocxx::database &db, const int &account_id) {
    try {
        mongocxx::collection collection = db.collection("accounts");

        mongocxx::pipeline pipeline{};

        pipeline.match(bsoncxx::builder::stream::document{}
                       << "_id" << bsoncxx::types::b_int64{static_cast<int64_t>(account_id)}
                       << bsoncxx::builder::stream::finalize);

        pipeline.unwind("$contacts");

        pipeline.lookup(bsoncxx::builder::stream::document{}
                        << "from" << "accounts"
                        << "localField" << "contacts.contactID"
                        << "foreignField" << "_id"
                        << "as" << "contactInfo"
                        << bsoncxx::builder::stream::finalize);

        pipeline.unwind("$contactInfo");

        pipeline.lookup(bsoncxx::builder::stream::document{}
                        << "from" << "chats"
                        << "localField" << "contacts.chatID"
                        << "foreignField" << "_id"
                        << "as" << "chatMessages"
                        << bsoncxx::builder::stream::finalize);

        pipeline.unwind("$chatMessages");

        pipeline.unwind("$chatMessages.messages");

        pipeline.group(bsoncxx::builder::stream::document{}
                       << "_id" << bsoncxx::builder::stream::open_document
                       << "contactID" << "$contactInfo._id"
                       << "first_name" << "$contactInfo.first_name"
                       << "last_name" << "$contactInfo.last_name"
                       << "status" << "$contactInfo.status"
                       << "image_url" << "$contactInfo.image_url"
                       << "chatID" << "$contacts.chatID"
                       << "unread_messages" << "$contacts.unread_messages"
                       << bsoncxx::builder::stream::close_document
                       << "messages" << bsoncxx::builder::stream::open_document
                       << "$push" << "$chatMessages.messages"
                       << bsoncxx::builder::stream::close_document
                       << bsoncxx::builder::stream::finalize);

        pipeline.project(bsoncxx::builder::stream::document{}
                         << "_id" << 0
                         << "contactInfo" << bsoncxx::builder::stream::open_document
                         << "_id" << "$_id.contactID"
                         << "first_name" << "$_id.first_name"
                         << "last_name" << "$_id.last_name"
                         << "status" << "$_id.status"
                         << "image_url" << "$_id.image_url"
                         << bsoncxx::builder::stream::close_document
                         << "chatID" << "$_id.chatID"
                         << "unread_messages" << "$_id.unread_messages"
                         << "chatMessages" << "$messages"
                         << bsoncxx::builder::stream::finalize);

        mongocxx::cursor cursor = collection.aggregate(pipeline);

        QJsonArray result_array;
        for (const bsoncxx::document::view &doc : cursor) {
            QString json_string = QString::fromStdString(bsoncxx::to_json(doc));
            QJsonDocument json_doc = QJsonDocument::fromJson(json_string.toUtf8());

            if (!json_doc.isNull())
                result_array.append(json_doc.object());
        }

        return result_array.isEmpty() ? QJsonDocument() : QJsonDocument(result_array);
    } catch (const mongocxx::exception &e) {
        std::cerr << "MongoDB Exception: " << e.what() << std::endl;
        return QJsonDocument();
    } catch (const std::exception &e) {
        std::cerr << "std Exception: " << e.what() << std::endl;
        return QJsonDocument();
    }
}

QJsonDocument Account::fetch_groups_and_chats(mongocxx::database &db, const int &account_id) {
    try {
        mongocxx::collection collection = db.collection("accounts");

        mongocxx::pipeline pipeline{};

        pipeline.match(bsoncxx::builder::stream::document{}
                       << "_id" << bsoncxx::types::b_int64{static_cast<int64_t>(account_id)}
                       << bsoncxx::builder::stream::finalize);

        pipeline.unwind("$groups");

        pipeline.lookup(bsoncxx::builder::stream::document{}
                        << "from" << "groups"
                        << "localField" << "groups.groupID"
                        << "foreignField" << "_id"
                        << "as" << "groupInfo"
                        << bsoncxx::builder::stream::finalize);

        pipeline.unwind("$groupInfo");

        pipeline.project(bsoncxx::builder::stream::document{}
                         << "_id" << "$groupInfo._id"
                         << "group_name" << "$groupInfo.group_name"
                         << "group_unread_messages" << "$groups.group_unread_messages"
                         << "group_image_url" << "$groupInfo.group_image_url"
                         << "group_admin" << "$groupInfo.group_admin"
                         << "group_members" << "$groupInfo.group_members"
                         << "group_messages" << "$groupInfo.group_messages"
                         << bsoncxx::builder::stream::finalize);

        mongocxx::cursor cursor = collection.aggregate(pipeline);

        QJsonArray result_array;
        for (const bsoncxx::document::view &doc : cursor) {
            QString json_string = QString::fromStdString(bsoncxx::to_json(doc));
            QJsonDocument json_doc = QJsonDocument::fromJson(json_string.toUtf8());

            if (!json_doc.isNull())
                result_array.append(json_doc.object());
        }

        return result_array.isEmpty() ? QJsonDocument() : QJsonDocument(result_array);
    } catch (const mongocxx::exception &e) {
        std::cerr << "MongoDB Exception: " << e.what() << std::endl;
        return QJsonDocument();
    } catch (const std::exception &e) {
        std::cerr << "std Exception: " << e.what() << std::endl;
        return QJsonDocument();
    }
}

QJsonArray Account::fetch_contactIDs(mongocxx::database &db, const int &account_id) {
    try {
        mongocxx::collection collection = db.collection("accounts");

        mongocxx::pipeline pipeline{};

        pipeline.match(bsoncxx::builder::stream::document{}
                       << "_id" << account_id
                       << bsoncxx::builder::stream::finalize);

        pipeline.unwind("$contacts");

        pipeline.group(bsoncxx::builder::stream::document{}
                       << "_id" << bsoncxx::types::b_null{}
                       << "contactIDs" << bsoncxx::builder::stream::open_document
                       << "$addToSet" << "$contacts.contactID"
                       << bsoncxx::builder::stream::close_document
                       << bsoncxx::builder::stream::finalize);

        pipeline.project(bsoncxx::builder::stream::document{}
                         << "_id" << 0
                         << "contactIDs" << 1
                         << bsoncxx::builder::stream::finalize);

        mongocxx::cursor cursor = collection.aggregate(pipeline);

        QJsonArray contact_ids_array;
        for (const bsoncxx::document::view &doc : cursor) {
            QString json_string = QString::fromStdString(bsoncxx::to_json(doc));
            QJsonDocument json_doc = QJsonDocument::fromJson(json_string.toUtf8());

            if (!json_doc.isNull() && json_doc.isObject()) {
                QJsonObject json_obj = json_doc.object();
                QJsonArray ids = json_obj["contactIDs"].toArray();

                for (const QJsonValue &val : ids)
                    contact_ids_array.append(val.toInt());
            }
        }

        return contact_ids_array;
    } catch (const mongocxx::exception &e) {
        std::cerr << "MongoDB Exception: " << e.what() << std::endl;

        return QJsonArray();
    } catch (const std::exception &e) {
        std::cerr << "std Exception: " << e.what() << std::endl;

        return QJsonArray();
    }
}

void Account::delete_account(mongocxx::database &db, const int &account_id) {
    try {
        mongocxx::collection account_collection = db["accounts"];
        mongocxx::collection group_collection = db["groups"];
        mongocxx::collection chats_collection = db["chats"];

        bsoncxx::stdx::optional<bsoncxx::document::value> account_doc = account_collection.find_one(
            bsoncxx::builder::stream::document{} << "_id" << account_id << bsoncxx::builder::stream::finalize);

        if (!account_doc) {
            std::cerr << "Account not found." << std::endl;
            return;
        }

        bsoncxx::document::view account_view = account_doc->view();

        bsoncxx::array::view groups = account_view["groups"].get_array().value;
        for (bsoncxx::array::element group : groups) {
            int groupID = group["groupID"].get_int32();

            group_collection.update_one(
                bsoncxx::builder::stream::document{} << "_id" << groupID << bsoncxx::builder::stream::finalize,
                bsoncxx::builder::stream::document{} << "$pull" << bsoncxx::builder::stream::open_document
                                                     << "group_members" << account_id
                                                     << bsoncxx::builder::stream::close_document << bsoncxx::builder::stream::finalize);
        }

        bsoncxx::array::view contacts = account_view["contacts"].get_array().value;
        for (bsoncxx::array::element contact : contacts) {
            int chatID = contact["chatID"].get_int32();

            account_collection.update_many(
                bsoncxx::builder::stream::document{} << "contacts.chatID" << chatID << bsoncxx::builder::stream::finalize,
                bsoncxx::builder::stream::document{} << "$pull" << bsoncxx::builder::stream::open_document
                                                     << "contacts" << bsoncxx::builder::stream::open_document
                                                     << "chatID" << chatID
                                                     << bsoncxx::builder::stream::close_document
                                                     << bsoncxx::builder::stream::close_document << bsoncxx::builder::stream::finalize);
        }

        for (bsoncxx::array::element contact : contacts) {
            int chatID = contact["chatID"].get_int32();

            chats_collection.delete_one(
                bsoncxx::builder::stream::document{} << "_id" << chatID << bsoncxx::builder::stream::finalize);
        }

        account_collection.delete_one(
            bsoncxx::builder::stream::document{} << "_id" << account_id << bsoncxx::builder::stream::finalize);

        std::cout << "Account, associated group memberships, and chats deleted successfully." << std::endl;
    } catch (const mongocxx::exception &e) {
        std::cerr << "MongoDB Exception: " << e.what() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "std Exception: " << e.what() << std::endl;
    }
}
