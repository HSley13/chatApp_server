#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <QtWidgets>
#include <QtCore>
#include <QtMultimedia>
#include <QWebSocket>
#include <QWebSocketServer>

#include <iostream>

#include <mongocxx/client.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pipeline.hpp>
#include <mongocxx/exception/exception.hpp>

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>

class Security
{
public:
    static QString generate_random_salt(std::size_t len);

    static QString hashing_password(const QString &password);

    static bool verifying_password(const QString &password, const QString &hashed_password);
};

class Account
{
public:
    static bool insert_document(mongocxx::database &db, const std::string &collection_name, const QJsonObject &json_object);

    static bool delete_document(mongocxx::database &db, const std::string &collection_name, const QJsonObject &filter_object);

    static bool update_document(mongocxx::database &db, const std::string &collection_name, const QJsonObject &filter_object, const QJsonObject &update_object);

    static QJsonDocument find_document(mongocxx::database &db, const std::string &collection_name, const QJsonObject &filter_object, const QJsonObject &fields = QJsonObject());

    static QJsonDocument fetch_contacts_and_chats(mongocxx::database &db, const int &account_id);

    static QJsonArray fetch_contactIDs(mongocxx::database &db, const int &account_id);
};

class S3
{
public:
    static std::string get_data_from_s3(const Aws::S3::S3Client &s3_client, const std::string &key);

    static std::string store_data_to_s3(const Aws::S3::S3Client &s3_client, const std::string &key, const std::string &data);

    static bool delete_data_from_s3(const Aws::S3::S3Client &s3_client, const std::string &key);
};
