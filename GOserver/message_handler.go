package main

import (
	"encoding/json"
	"fmt"
)

func (sm *ServerManager) signUp(message map[string]interface{}) {
}

func (sm *ServerManager) loginRequest(clientID int, message map[string]interface{}) {
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
		fmt.Println("Error unmarshalling JSON:", err)
		return
	}

	msgType, ok := message["type"].(string)
	if !ok {
		fmt.Println("Error: message type not found or is not a string")
		return
	}

	if messageType, found := FromString(msgType); found {
		switch messageType {
		case SignUp:
			go sm.signUp(message)
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
			fmt.Println("Unknown message type")
		}
	} else {
		fmt.Println("Invalid message type")
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
