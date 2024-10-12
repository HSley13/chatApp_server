package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"strconv"
	"sync"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/credentials"
	"github.com/aws/aws-sdk-go/aws/session"
	"github.com/aws/aws-sdk-go/service/s3"
	"github.com/gorilla/websocket"
	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/mongo"
	"go.mongodb.org/mongo-driver/mongo/options"
)

type ServerManager struct {
	upgrader  *websocket.Upgrader
	chatAppDB *mongo.Database
	s3Client  *s3.S3
	mutex     sync.Mutex
	clients   map[int]*websocket.Conn
}

func NewServerManager() *ServerManager {
	upgrader := &websocket.Upgrader{
		ReadBufferSize:  1024,
		WriteBufferSize: 1024,
		CheckOrigin: func(r *http.Request) bool {
			return true
		},
	}

	mongoURI := os.Getenv("MONGODB_URI")
	clientOptions := options.Client().ApplyURI(mongoURI)
	client, err := mongo.Connect(context.TODO(), clientOptions)
	if err != nil {
		log.Fatal("Failed to initialize MongoDB: ", err)
	}

	sess, err := session.NewSession(&aws.Config{
		Region:      aws.String(os.Getenv("CHAT_APP_BUCKET_REGION")),
		Credentials: credentials.NewStaticCredentials(os.Getenv("CHAT_APP_ACESS_KEY"), os.Getenv("CHAT_APP_SECRET_ACCESS_KEY"), ""),
	})
	if err != nil {
		log.Fatal("Failed to initialize AWS S3: ", err)
	}

	sm := &ServerManager{
		upgrader:  upgrader,
		chatAppDB: client.Database("chatAppDB"),
		s3Client:  s3.New(sess),
		clients:   make(map[int]*websocket.Conn),
	}

	return sm
}

func (sm *ServerManager) handleConnections(w http.ResponseWriter, r *http.Request) {
	conn, err := sm.upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println("Websocket upgrade failed: ", err)
		return
	}

	sm.mutex.Lock()
	clientID := len(sm.clients) + 1
	sm.clients[clientID] = conn
	sm.mutex.Unlock()

	log.Printf("Client %d is connected", clientID)

	go sm.handleClient(clientID, conn)
}

func (sm *ServerManager) handleClient(clientID int, conn *websocket.Conn) {
	defer conn.Close()

	for {
		_, msgBytes, err := conn.ReadMessage()
		if err != nil {
			log.Printf("Client %d is disconnected: %v", clientID, err)
			sm.handleDisconnections(clientID)
			break
		}
		go sm.handleMessage(clientID, msgBytes)
	}
}

func (sm *ServerManager) handleDisconnections(clientID int) {
	sm.mutex.Lock()
	delete(sm.clients, clientID)
	sm.mutex.Unlock()

	filter := bson.M{"_id": clientID}
	update := bson.M{"status": false}
	_, err := UpdateDocument(sm.chatAppDB, "accounts", filter, update)
	if err != nil {
		log.Printf("Failed to update Object: %v", err)
	}

	contactIDs, err := FetchContactIDs(sm.chatAppDB, clientID)
	if err != nil {
		log.Printf("Error fetching contact IDs: %v", err)
		return
	}

	for _, contactID := range contactIDs {
		sm.mutex.Lock()
		client, exists := sm.clients[contactID]
		sm.mutex.Unlock()
		if !exists {
			log.Printf("Client %d not found", contactID)
			continue
		}

		message := map[string]interface{}{
			"type":         "client_disconnected",
			"phone_number": clientID,
		}

		jsonMessage, err := json.Marshal(message)
		if err != nil {
			log.Printf("Failed to marshal JSON for client %d: %v", contactID, err)
			continue
		}

		err = client.WriteMessage(1, jsonMessage)
		if err != nil {
			log.Printf("Failed to send message to client %d: %v", contactID, err)
			continue
		}
	}
}

func (sm *ServerManager) Run() {
	http.HandleFunc("/", sm.handleConnections)

	ip := os.Getenv("CHAT_APP_SERVER_IP")
	port, err := strconv.Atoi(os.Getenv("CHAT_APP_SERVER_PORT"))
	if err != nil {
		log.Fatalf("Invalid server port: %v", err)
	}

	address := fmt.Sprintf("%s:%d", ip, port)
	fmt.Println("Server is Listening on: ", address)
	if err := http.ListenAndServe(address, nil); err != nil {
		log.Fatal("Server failed to Start: ", err)
	}
}

func main() {
	serverManager := NewServerManager()
	serverManager.Run()
}
