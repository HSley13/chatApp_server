package main

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/base64"
	"errors"
	"fmt"
	"io"
	"log"
	"math/big"
	"os"
	"strings"
	"time"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/config"
	"github.com/aws/aws-sdk-go-v2/service/s3"
	"github.com/aws/aws-sdk-go-v2/service/s3/types"
	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/mongo"
	"go.mongodb.org/mongo-driver/mongo/options"
	"golang.org/x/crypto/argon2"
)

const (
	memory      = 64 * 1024 // 64MB
	iterations  = 3
	parallelism = 2
	saltLength  = 16
	keyLength   = 32
)

const validChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@#&?!~^-$%*+"

func GenerateRandomSalt(length int) (string, error) {
	salt := make([]byte, length)

	for i := 0; i < length; i++ {
		index, err := rand.Int(rand.Reader, big.NewInt(int64(len(validChars))))
		if err != nil {
			return "", err
		}
		salt[i] = validChars[index.Int64()]
	}

	return string(salt), nil
}

func HashPassword(password string) (string, error) {
	salt, err := GenerateRandomSalt(saltLength)
	if err != nil {
		return "", err
	}

	hash := argon2.IDKey([]byte(password), []byte(salt), iterations, memory, uint8(parallelism), keyLength)

	saltEncoded := base64.RawStdEncoding.EncodeToString([]byte(salt))
	hashEncoded := base64.RawStdEncoding.EncodeToString(hash)

	return fmt.Sprintf("%s$%s", saltEncoded, hashEncoded), nil
}

func VerifyPassword(password, hashedPassword string) (bool, error) {
	parts := strings.Split(hashedPassword, "$")
	if len(parts) != 2 {
		return false, errors.New("Invalid hashed password format")
	}

	salt, err := base64.RawStdEncoding.DecodeString(parts[0])
	if err != nil {
		return false, err
	}

	storedHash, err := base64.RawStdEncoding.DecodeString(parts[1])
	if err != nil {
		return false, err
	}

	computedHash := argon2.IDKey([]byte(password), salt, iterations, memory, uint8(parallelism), keyLength)

	return string(computedHash) == string(storedHash), nil
}

func GetDataFromS3(ctx context.Context, s3Client *s3.Client, key string) (string, error) {
	bucket := os.Getenv("CHAT_APP_BUCKET_NAME")
	if bucket == "" {
		return "", fmt.Errorf("CHAT_APP_BUCKET_NAME environment variable is not set")
	}

	input := &s3.GetObjectInput{
		Bucket: aws.String(bucket),
		Key:    aws.String(key),
	}

	result, err := s3Client.GetObject(ctx, input)
	if err != nil {
		return "", fmt.Errorf("Failed to get object: %v", err)
	}
	defer result.Body.Close()

	body, err := io.ReadAll(result.Body)
	if err != nil {
		return "", fmt.Errorf("Failed to read object body: %v", err)
	}

	return string(body), nil
}

func StoreDataToS3(ctx context.Context, s3Client *s3.Client, key string, data string) (string, error) {
	bucket := os.Getenv("CHAT_APP_BUCKET_NAME")
	if bucket == "" {
		return "", fmt.Errorf("CHAT_APP_BUCKET_NAME environment variable is not set")
	}

	input := &s3.PutObjectInput{
		Bucket: aws.String(bucket),
		Key:    aws.String(key),
		Body:   bytes.NewReader([]byte(data)),
	}

	_, err := s3Client.PutObject(ctx, input)
	if err != nil {
		return "", fmt.Errorf("Failed to upload object: %v", err)
	}

	log.Printf("Successfully uploaded object: %s\n", key)

	psClient := s3.NewPresignClient(s3Client)
	psInput := &s3.GetObjectInput{
		Bucket: aws.String(bucket),
		Key:    aws.String(key),
	}
	psURL, err := psClient.PresignGetObject(ctx, psInput, func(po *s3.PresignOptions) {
		po.Expires = 7 * 24 * time.Hour // 7 days
	})
	if err != nil {
		return "", fmt.Errorf("Failed to generate presigned URL: %v", err)
	}

	return psURL.URL, nil
}

func DeleteDataFromS3(ctx context.Context, s3Client *s3.Client, key string) (bool, error) {
	bucket := os.Getenv("CHAT_APP_BUCKET_NAME")
	if bucket == "" {
		return false, fmt.Errorf("CHAT_APP_BUCKET_NAME environment variable is not set")
	}

	input := &s3.DeleteObjectInput{
		Bucket: aws.String(bucket),
		Key:    aws.String(key),
	}

	_, err := s3Client.DeleteObject(ctx, input)
	if err != nil {
		return false, fmt.Errorf("Failed to delete object: %v", err)
	}

	log.Printf("Successfully deleted object: %s/%s\n", bucket, key)
	return true, nil
}

func InsertDocument(db *mongo.Database, collectionName string, document bson.M) (bool, error) {
	collection := db.Collection(collectionName)

	result, err := collection.InsertOne(context.Background(), document)
	if err != nil {
		return false, fmt.Errorf("Error inserting document: %v", err)
	}

	return result.InsertedID != nil, nil
}

func deleteDocument(db *mongo.Database, collectionName string, filter bson.D) (bool, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	collection := db.Collection(collectionName)

	result, err := collection.DeleteOne(ctx, filter)
	if err != nil {
		return false, fmt.Errorf("Error deleting document: %v", err)
	}

	return result.DeletedCount > 0, nil
}

func UpdateDocument(db *mongo.Database, collectionName string, filter bson.M, update bson.M) (bool, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	collection := db.Collection(collectionName)

	updateDoc := bson.M{"$set": update}

	result, err := collection.UpdateOne(ctx, filter, updateDoc)
	if err != nil {
		return false, fmt.Errorf("error updating document: %v", err)
	}

	return result.ModifiedCount == 1, nil
}

func FindDocument(db *mongo.Database, collectionName string, filter bson.M, projection bson.M) ([]bson.M, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	collection := db.Collection(collectionName)

	findOptions := options.Find().SetProjection(projection)

	cursor, err := collection.Find(ctx, filter, findOptions)
	if err != nil {
		return nil, fmt.Errorf("Error Finding Document: %v", err)
	}
	defer cursor.Close(ctx)

	var results []bson.M
	for cursor.Next(ctx) {
		var doc bson.M
		if err := cursor.Decode(&doc); err != nil {
			return nil, fmt.Errorf("Error Decoding Document: %v", err)
		}
		results = append(results, doc)
	}

	if err := cursor.Err(); err != nil {
		return nil, fmt.Errorf("Error iteration over the cursor: %v", err)
	}

	return results, nil
}

func FetchContactAndChats(ctx context.Context, db *mongo.Database, accountID int64) ([]bson.M, error) {
	collection := db.Collection("accounts")

	pipeline := mongo.Pipeline{
		bson.D{
			{"$match", bson.D{{"_id", accountID}}},
		},
		bson.D{
			{"$unwind", "$contacts"},
		},
		bson.D{
			{"$lookup", bson.D{
				{"from", "accounts"},
				{"localField", "contacts.contactID"},
				{"foreignField", "_id"},
				{"as", "contactInfo"},
			}},
		},
		bson.D{
			{"$unwind", "$contactInfo"},
		},
		bson.D{
			{"$lookup", bson.D{
				{"from", "chats"},
				{"localField", "contacts.chatID"},
				{"foreignField", "_id"},
				{"as", "chatMessages"},
			}},
		},
		bson.D{
			{"$unwind", "$chatMessages"},
		},
		bson.D{
			{"$unwind", "$chatMessages.messages"},
		},
		bson.D{
			{"$group", bson.D{
				{"_id", bson.D{
					{"contactID", "$contactInfo._id"},
					{"first_name", "$contactInfo.first_name"},
					{"last_name", "$contactInfo.last_name"},
					{"status", "$contactInfo.status"},
					{"image_url", "$contactInfo.image_url"},
					{"chatID", "$contacts.chatID"},
					{"unread_messages", "$contacts.unread_messages"},
				}},
				{"messages", bson.D{
					{"$push", "$chatMessages.messages"},
				}},
			}},
		},
		bson.D{
			{"$project", bson.D{
				{"_id", 0},
				{"contactInfo", bson.D{
					{"_id", "$_id.contactID"},
					{"first_name", "$_id.first_name"},
					{"last_name", "$_id.last_name"},
					{"status", "$_id.status"},
					{"image_url", "$_id.image_url"},
				}},
				{"chatID", "$_id.chatID"},
				{"unread_messages", "$_id.unread_messages"},
				{"chatMessages", "$messages"},
			}},
		},
	}

	cursor, err := collection.Aggregate(ctx, pipeline)
	if err != nil {
		return nil, fmt.Errorf("error aggregating: %v", err)
	}
	defer cursor.Close(ctx)

	var result []bson.M
	for cursor.Next(ctx) {
		var doc bson.M
		if err := cursor.Decode(&doc); err != nil {
			return nil, fmt.Errorf("error decoding document: %v", err)
		}
		result = append(result, doc)
	}

	if err := cursor.Err(); err != nil {
		return nil, fmt.Errorf("cursor error: %v", err)
	}

	return result, nil
}

func fetchGroupsAndChats(db *mongo.Database, accountID int64) ([]bson.M, error) {
	collection := db.Collection("accounts")

	pipeline := mongo.Pipeline{
		{
			{"$match", bson.M{"_id": accountID}},
		},
		{
			{"$unwind", "$groups"},
		},
		{
			{"$lookup", bson.M{
				"from":         "groups",
				"localField":   "groups.groupID",
				"foreignField": "_id",
				"as":           "groupInfo",
			}},
		},
		{
			{"$unwind", "$groupInfo"},
		},
		{
			{"$project", bson.M{
				"_id":                   "$groupInfo._id",
				"group_name":            "$groupInfo.group_name",
				"group_unread_messages": "$groups.group_unread_messages",
				"group_image_url":       "$groupInfo.group_image_url",
				"group_admin":           "$groupInfo.group_admin",
				"group_members":         "$groupInfo.group_members",
				"group_messages":        "$groupInfo.group_messages",
			}},
		},
	}

	cursor, err := collection.Aggregate(context.TODO(), pipeline)
	if err != nil {
		return nil, fmt.Errorf("MongoDB Exception: %v", err)
	}
	defer cursor.Close(context.TODO())

	var results []bson.M
	for cursor.Next(context.TODO()) {
		var result bson.M
		if err := cursor.Decode(&result); err != nil {
			return nil, fmt.Errorf("Error decoding cursor: %v", err)
		}
		results = append(results, result)
	}

	if err := cursor.Err(); err != nil {
		return nil, fmt.Errorf("Cursor iteration error: %v", err)
	}

	return results, nil
}

func deleteAccount(db *mongo.Database, accountID int64) error {
	accountCollection := db.Collection("accounts")
	groupCollection := db.Collection("groups")
	chatsCollection := db.Collection("chats")

	accountDoc := accountCollection.FindOne(context.TODO(), bson.M{"_id": accountID})
	if err := accountDoc.Err(); err != nil {
		if err == mongo.ErrNoDocuments {
			fmt.Println("Account not found.")
			return nil
		}
		return fmt.Errorf("MongoDB Exception: %v", err)
	}

	var account bson.M
	if err := accountDoc.Decode(&account); err != nil {
		return fmt.Errorf("Error decoding account document: %v", err)
	}

	if groups, ok := account["groups"].(bson.A); ok {
		for _, group := range groups {
			if groupID, ok := group.(bson.M)["groupID"].(int32); ok {
				_, err := groupCollection.UpdateOne(context.TODO(),
					bson.M{"_id": groupID},
					bson.M{"$pull": bson.M{"group_members": accountID}})
				if err != nil {
					return fmt.Errorf("Error updating group: %v", err)
				}
			}
		}
	}

	if contacts, ok := account["contacts"].(bson.A); ok {
		for _, contact := range contacts {
			if chatID, ok := contact.(bson.M)["chatID"].(int32); ok {
				_, err := accountCollection.UpdateMany(context.TODO(),
					bson.M{"contacts.chatID": chatID},
					bson.M{"$pull": bson.M{"contacts": bson.M{"chatID": chatID}}})
				if err != nil {
					return fmt.Errorf("Error updating contacts: %v", err)
				}

				_, err = chatsCollection.DeleteOne(context.TODO(),
					bson.M{"_id": chatID})
				if err != nil {
					return fmt.Errorf("Error deleting chat: %v", err)
				}
			}
		}
	}

	_, err := accountCollection.DeleteOne(context.TODO(),
		bson.M{"_id": accountID})
	if err != nil {
		return fmt.Errorf("Error deleting account: %v", err)
	}

	return nil
}
