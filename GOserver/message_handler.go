package main

import (
	"encoding/json"
	"log"

	"github.com/gorilla/websocket"
	"go.mongodb.org/mongo-driver/bson"
)

type Client struct {
	conn *websocket.Conn
}

func (sm *ServerManager) signUp(clientID int, message map[string]interface{}) {
	sm.mutex.Lock()
	client, exists := sm.clients[clientID]
	sm.mutex.Unlock()
	if !exists {
		log.Printf("Client %d not found", clientID)
		return
	}

	hashedPassword, err := HashPassword(message["password"].(string))
	if err != nil {
		log.Printf("Error hashing the password: %v", err)
		response := map[string]interface{}{
			"type":    "sign_up",
			"status":  false,
			"message": "Error creating account, try again",
		}
		signupResponse, _ := json.Marshal(response)
		client.WriteMessage(1, signupResponse)
		return
	}

	// Handle phone number as a float64 and convert to int
	phoneNumberFloat, ok := message["phone_number"].(float64)
	if !ok {
		log.Printf("Error: phone_number is not a float64")
		return
	}
	phoneNumber := int(phoneNumberFloat) // Convert float64 to int

	bsonObject := bson.M{
		"_id":             phoneNumber,
		"first_name":      message["first_name"].(string),
		"last_name":       message["last_name"].(string),
		"image_url":       "",
		"status":          false,
		"hashed_password": hashedPassword,
		"secret_question": message["secret_question"].(string),
		"secret_answer":   message["secret_answer"].(string),
		"contacts":        bson.A{},
		"groups":          bson.A{},
	}

	success, err := InsertDocument(sm.chatAppDB, "accounts", bsonObject)
	if err != nil {
		log.Printf("Error Inserting the New account in the DB: %v", err)
		response := map[string]interface{}{
			"type":    "sign_up",
			"status":  false,
			"message": "Failed to Create Account, try again",
		}
		signupResponse, _ := json.Marshal(response)
		client.WriteMessage(1, signupResponse)
		return
	}

	response := map[string]interface{}{
		"type":    "sign_up",
		"status":  success,
		"message": "Account Created Successfully, Reconnect",
	}
	if !success {
		response["message"] = "Failed to Create Account, try again"
	}

	signupResponse, err := json.Marshal(response)
	if err != nil {
		log.Printf("Failed to marshal JSON response: %v", err)
		return
	}

	err = client.WriteMessage(1, signupResponse)
	if err != nil {
		log.Printf("Failed to send message via WebSocket: %v", err)
	}
}

func (sm *ServerManager) loginRequest(clientID int, message map[string]interface{}) {
	if clientID == 0 {
		return
	}

	phoneNumberFloat, ok := message["phone_number"].(float64)
	if !ok {
		log.Printf("Error: phone_number is not a float64")
		return
	}
	phoneNumber := int(phoneNumberFloat)

	sm.mutex.Lock()
	sm.clients[phoneNumber] = sm.clients[clientID]
	client := sm.clients[phoneNumber]
	delete(sm.clients, clientID)
	sm.mutex.Unlock()

	clientID = phoneNumber

	filter := bson.M{"_id": clientID}
	// Fetch account from database
	account, err := FindDocument(sm.chatAppDB, "accounts", filter, nil)
	if err != nil || len(account) == 0 {
		log.Printf("Error finding account: %v", err)

		// Inform client that account does not exist
		response := map[string]interface{}{
			"type":    "login_request",
			"status":  false,
			"message": "Account doesn't exist in our database, verify and try again",
		}
		jsonMessage, err := json.Marshal(response)
		if err != nil {
			log.Printf("Failed to marshal message: %v", err)
			return
		}
		client.WriteMessage(1, jsonMessage)
		return
	}

	log.Printf("Account: %v", account)
	// Verify the password
	password := message["password"].(string)
	// hashedPassword := account["hashed_password"].(string)
	hashedPassword := ""
	if success, err := VerifyPassword(password, hashedPassword); !success || err != nil {
		log.Printf("Password verification failed for client %d: %v", clientID, err)

		// Inform client that the password is incorrect
		response := map[string]interface{}{
			"type":    "login_request",
			"status":  false,
			"message": "Password Incorrect",
		}
		jsonMessage, err := json.Marshal(response)
		if err != nil {
			log.Printf("Failed to marshal message: %v", err)
			return
		}
		client.WriteMessage(1, jsonMessage)
		return
	}

	log.Printf("Client %d is connected", clientID)

	update := bson.M{"status": true}
	_, err = UpdateDocument(sm.chatAppDB, "accounts", filter, update)
	if err != nil {
		log.Printf("Error updating account status: %v", err)
	}

	// Fetch contacts and chats
	contacts, err := FetchContactAndChats(sm.chatAppDB, clientID)
	if err != nil {
		log.Printf("Error fetching contacts: %v", err)
		return
	}

	// Fetch groups and chats
	groups, err := FetchGroupsAndChats(sm.chatAppDB, clientID)
	if err != nil {
		log.Printf("Error fetching groups: %v", err)
		return
	}

	response := map[string]interface{}{
		"type":     "login_request",
		"status":   true,
		"message":  "Loading your data...",
		"my_info":  account,
		"contacts": contacts,
		"groups":   groups,
	}

	// Send successful response
	jsonMessage, err := json.Marshal(response)
	if err != nil {
		log.Printf("Failed to marshal message: %v", err)
		return
	}
	client.WriteMessage(1, jsonMessage)

	// Notify the contacts that this client has connected
	contactIDs, err := FetchContactIDs(sm.chatAppDB, clientID)
	if err != nil {
		log.Printf("Error fetching contact IDs: %v", err)
		return
	}

	for _, contactID := range contactIDs {
		sm.mutex.Lock()
		contactClient, exists := sm.clients[contactID]
		sm.mutex.Unlock()
		if !exists {
			continue
		}

		contactMessage := map[string]interface{}{
			"type":         "client_connected",
			"phone_number": clientID,
		}
		jsonMessage, err := json.Marshal(contactMessage)
		if err != nil {
			log.Printf("Failed to marshal message: %v", err)
			return
		}
		contactClient.WriteMessage(1, jsonMessage)
	}
}

func (sm *ServerManager) lookup_friend(clientID int, message map[string]interface{}) {
}

func (sm *ServerManager) profile_image(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) group_profile_image(message map[string]interface{}) {

}

func (sm *ServerManager) profile_image_deleted(clientID int) {

}

func (sm *ServerManager) text_message_received(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) new_group(message map[string]interface{}) {

}

func (sm *ServerManager) group_text_received(message map[string]interface{}) {

}

func (sm *ServerManager) file_received(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) group_file_received(message map[string]interface{}) {

}

func (sm *ServerManager) is_typing_received(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) group_is_typing_received(message map[string]interface{}) {

}

func (sm *ServerManager) update_info_received(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) update_password(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) retrieve_question(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) remove_group_member(message map[string]interface{}) {

}

func (sm *ServerManager) add_group_member(message map[string]interface{}) {

}

func (sm *ServerManager) delete_message(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) delete_group_message(message map[string]interface{}) {

}

func (sm *ServerManager) update_unread_message(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) update_group_unread_message(message map[string]interface{}) {

}

func (sm *ServerManager) delete_acount(clientID int) {

}

func (sm *ServerManager) audio_received(clientID int, message map[string]interface{}) {

}

func (sm *ServerManager) group_audio_received(message map[string]interface{}) {

}

func (sm *ServerManager) handleMessage(clientID int, data []byte) {
	var message map[string]interface{}

	err := json.Unmarshal(data, &message)
	if err != nil {
		log.Println("Error unmarshalling JSON:", err)
		return
	}

	msgType, ok := message["type"].(string)
	if !ok {
		log.Println("Error: message type not found or is not a string")
		return
	}

	if messageType, found := FromString(msgType); found {
		switch messageType {
		case SignUp:
			go sm.signUp(clientID, message)
		case LoginRequest:
			go sm.loginRequest(clientID, message)
		case LookupFriend:
			go sm.lookup_friend(clientID, message)
		case ProfileImage:
			go sm.profile_image(clientID, message)
		case GroupProfileImage:
			go sm.group_file_received(message)
		case ProfileImageDeleted:
			go sm.profile_image_deleted(clientID)
		case Text:
			go sm.text_message_received(clientID, message)
		case GroupText:
			go sm.group_text_received(message)
		case File:
			go sm.file_received(clientID, message)
		case GroupFile:
			go sm.group_file_received(message)
		case NewGroup:
			go sm.new_group(message)
		case IsTyping:
			go sm.is_typing_received(clientID, message)
		case GroupIsTyping:
			go sm.group_is_typing_received(message)
		case UpdateInfo:
			go sm.update_info_received(clientID, message)
		case UpdatePassword:
			go sm.update_password(clientID, message)
		case RetrieveQuestion:
			go sm.retrieve_question(clientID, message)
		case RemoveGroupMember:
			go sm.remove_group_member(message)
		case AddGroupMember:
			go sm.add_group_member(message)
		case DeleteMessage:
			go sm.delete_message(clientID, message)
		case DeleteGroupMessage:
			go sm.delete_group_message(message)
		case UpdateUnreadMessage:
			go sm.update_unread_message(clientID, message)
		case UpdateGroupUnreadMessage:
			go sm.update_group_unread_message(message)
		case Audio:
			go sm.audio_received(clientID, message)
		case GroupAudio:
			go sm.group_audio_received(message)
		case DeleteAccount:
			go sm.delete_acount(clientID)
		default:
			log.Println("Unknown message type")
		}
	} else {
		log.Println("Invalid message type")
	}
}

type MessageType int

func FromString(s string) (MessageType, bool) {
	messageType, exists := stringToMessageType[s]
	return messageType, exists
}

const (
	SignUp MessageType = iota
	IsTyping
	ProfileImage
	GroupProfileImage
	ProfileImageDeleted
	File
	Text
	GroupFile
	GroupText
	LoginRequest
	ClientDisconnected
	ClientConnected
	LookupFriend
	NewGroup
	AddedToGroup
	GroupIsTyping
	UpdateInfo
	UpdatePassword
	RetrieveQuestion
	RemoveGroupMember
	AddGroupMember
	Audio
	GroupAudio
	DeleteMessage
	DeleteGroupMessage
	UpdateUnreadMessage
	UpdateGroupUnreadMessage
	DeleteAccount
)

var stringToMessageType = map[string]MessageType{
	"sign_up":                     SignUp,
	"is_typing":                   IsTyping,
	"profile_image":               ProfileImage,
	"group_profile_image":         GroupProfileImage,
	"profile_image_deleted":       ProfileImageDeleted,
	"file":                        File,
	"text":                        Text,
	"group_file":                  GroupFile,
	"group_text":                  GroupText,
	"login_request":               LoginRequest,
	"client_disconnected":         ClientDisconnected,
	"client_connected":            ClientConnected,
	"lookup_friend":               LookupFriend,
	"new_group":                   NewGroup,
	"added_to_group":              AddedToGroup,
	"group_is_typing":             GroupIsTyping,
	"contact_info_updated":        UpdateInfo,
	"update_password":             UpdatePassword,
	"retrieve_question":           RetrieveQuestion,
	"remove_group_member":         RemoveGroupMember,
	"add_group_member":            AddGroupMember,
	"delete_message":              DeleteMessage,
	"delete_group_message":        DeleteGroupMessage,
	"update_unread_message":       UpdateUnreadMessage,
	"update_group_unread_message": UpdateGroupUnreadMessage,
	"delete_account":              DeleteAccount,
	"audio":                       Audio,
	"group_audio":                 GroupAudio,
}
