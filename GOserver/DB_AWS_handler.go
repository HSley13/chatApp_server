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
		return false, errors.New("invalid hashed password format")
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
		return "", fmt.Errorf("failed to get object: %v", err)
	}
	defer result.Body.Close()

	body, err := io.ReadAll(result.Body)
	if err != nil {
		return "", fmt.Errorf("failed to read object body: %v", err)
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
		return "", fmt.Errorf("failed to upload object: %v", err)
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
		return "", fmt.Errorf("failed to generate presigned URL: %v", err)
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
		return false, fmt.Errorf("failed to delete object: %v", err)
	}

	log.Printf("Successfully deleted object: %s/%s\n", bucket, key)
	return true, nil
}
