// Copyright Epic Games, Inc. All Rights Reserved.

#include "ICESignalingInterface.h"
#include "OnlineSubsystemICEPackage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Guid.h"

// Convert FICESignalMessage to JSON
FString FICESignalMessage::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	
	// Message type
	JsonObject->SetStringField(TEXT("type"), 
		Type == EICESignalType::Offer ? TEXT("offer") :
		Type == EICESignalType::Answer ? TEXT("answer") : TEXT("candidate"));
	
	// Session and peer IDs
	JsonObject->SetStringField(TEXT("sessionId"), SessionId);
	JsonObject->SetStringField(TEXT("senderId"), SenderId);
	JsonObject->SetStringField(TEXT("receiverId"), ReceiverId);
	
	// Timestamp
	JsonObject->SetStringField(TEXT("timestamp"), Timestamp.ToIso8601());
	
	// Candidates
	TArray<TSharedPtr<FJsonValue>> CandidatesArray;
	for (const FICECandidate& Candidate : Candidates)
	{
		TSharedPtr<FJsonObject> CandidateObj = MakeShared<FJsonObject>();
		CandidateObj->SetStringField(TEXT("foundation"), Candidate.Foundation);
		CandidateObj->SetNumberField(TEXT("componentId"), Candidate.ComponentId);
		CandidateObj->SetStringField(TEXT("transport"), Candidate.Transport);
		CandidateObj->SetNumberField(TEXT("priority"), Candidate.Priority);
		CandidateObj->SetStringField(TEXT("address"), Candidate.Address);
		CandidateObj->SetNumberField(TEXT("port"), Candidate.Port);
		CandidateObj->SetStringField(TEXT("type"), 
			Candidate.Type == EICECandidateType::Host ? TEXT("host") :
			Candidate.Type == EICECandidateType::ServerReflexive ? TEXT("srflx") : TEXT("relay"));
		
		CandidatesArray.Add(MakeShared<FJsonValueObject>(CandidateObj));
	}
	JsonObject->SetArrayField(TEXT("candidates"), CandidatesArray);
	
	// Metadata
	TSharedPtr<FJsonObject> MetadataObj = MakeShared<FJsonObject>();
	for (const auto& Pair : Metadata)
	{
		MetadataObj->SetStringField(Pair.Key, Pair.Value);
	}
	JsonObject->SetObjectField(TEXT("metadata"), MetadataObj);
	
	// Serialize to string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	
	return OutputString;
}

// Create FICESignalMessage from JSON
FICESignalMessage FICESignalMessage::FromJson(const FString& JsonString)
{
	FICESignalMessage Message;
	
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Failed to parse ICE signal message JSON"));
		return Message;
	}
	
	// Type
	FString TypeStr = JsonObject->GetStringField(TEXT("type"));
	if (TypeStr == TEXT("offer"))
	{
		Message.Type = EICESignalType::Offer;
	}
	else if (TypeStr == TEXT("answer"))
	{
		Message.Type = EICESignalType::Answer;
	}
	else
	{
		Message.Type = EICESignalType::Candidate;
	}
	
	// Session and peer IDs
	Message.SessionId = JsonObject->GetStringField(TEXT("sessionId"));
	Message.SenderId = JsonObject->GetStringField(TEXT("senderId"));
	Message.ReceiverId = JsonObject->GetStringField(TEXT("receiverId"));
	
	// Timestamp
	FString TimestampStr = JsonObject->GetStringField(TEXT("timestamp"));
	FDateTime::ParseIso8601(*TimestampStr, Message.Timestamp);
	
	// Candidates
	const TArray<TSharedPtr<FJsonValue>>* CandidatesArray;
	if (JsonObject->TryGetArrayField(TEXT("candidates"), CandidatesArray))
	{
		for (const TSharedPtr<FJsonValue>& CandidateValue : *CandidatesArray)
		{
			TSharedPtr<FJsonObject> CandidateObj = CandidateValue->AsObject();
			if (CandidateObj.IsValid())
			{
				FICECandidate Candidate;
				Candidate.Foundation = CandidateObj->GetStringField(TEXT("foundation"));
				Candidate.ComponentId = CandidateObj->GetIntegerField(TEXT("componentId"));
				Candidate.Transport = CandidateObj->GetStringField(TEXT("transport"));
				Candidate.Priority = CandidateObj->GetIntegerField(TEXT("priority"));
				Candidate.Address = CandidateObj->GetStringField(TEXT("address"));
				Candidate.Port = CandidateObj->GetIntegerField(TEXT("port"));
				
				FString TypeStr = CandidateObj->GetStringField(TEXT("type"));
				if (TypeStr == TEXT("host"))
				{
					Candidate.Type = EICECandidateType::Host;
				}
				else if (TypeStr == TEXT("srflx"))
				{
					Candidate.Type = EICECandidateType::ServerReflexive;
				}
				else if (TypeStr == TEXT("relay"))
				{
					Candidate.Type = EICECandidateType::Relayed;
				}
				
				Message.Candidates.Add(Candidate);
			}
		}
	}
	
	// Metadata
	const TSharedPtr<FJsonObject>* MetadataObj;
	if (JsonObject->TryGetObjectField(TEXT("metadata"), MetadataObj))
	{
		for (const auto& Pair : (*MetadataObj)->Values)
		{
			Message.Metadata.Add(Pair.Key, Pair.Value->AsString());
		}
	}
	
	return Message;
}

// FLocalFileSignaling implementation

FLocalFileSignaling::FLocalFileSignaling(const FString& SharedDirectory)
	: SignalingDirectory(SharedDirectory)
	, LastProcessedMessageIndex(0)
	, bIsActive(false)
{
	// Generate unique peer ID
	PeerId = FGuid::NewGuid().ToString();
}

FLocalFileSignaling::~FLocalFileSignaling()
{
	Shutdown();
}

bool FLocalFileSignaling::Initialize()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Initializing LocalFileSignaling: PeerId=%s, Directory=%s"), 
		*PeerId, *SignalingDirectory);
	
	if (!EnsureSignalingDirectory())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to create signaling directory: %s"), *SignalingDirectory);
		return false;
	}
	
	bIsActive = true;
	UE_LOG(LogOnlineICE, Log, TEXT("LocalFileSignaling initialized successfully"));
	return true;
}

void FLocalFileSignaling::Shutdown()
{
	if (bIsActive)
	{
		UE_LOG(LogOnlineICE, Log, TEXT("Shutting down LocalFileSignaling"));
		bIsActive = false;
	}
}

bool FLocalFileSignaling::SendSignal(const FICESignalMessage& Message)
{
	if (!bIsActive)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot send signal: signaling not active"));
		return false;
	}
	
	// Convert message to JSON
	FString JsonString = Message.ToJson();
	
	// Generate unique file name
	FString FileName = GenerateMessageFileName();
	FString FilePath = FPaths::Combine(SignalingDirectory, FileName);
	
	// Write to file
	if (FFileHelper::SaveStringToFile(JsonString, *FilePath))
	{
		UE_LOG(LogOnlineICE, Verbose, TEXT("Signal sent: %s (Type: %d)"), *FileName, (int32)Message.Type);
		return true;
	}
	else
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to write signal file: %s"), *FilePath);
		return false;
	}
}

void FLocalFileSignaling::ProcessSignals()
{
	if (!bIsActive)
	{
		return;
	}
	
	// Read pending messages
	TArray<FICESignalMessage> Messages = ReadPendingMessages();
	
	// Process each message
	for (const FICESignalMessage& Message : Messages)
	{
		// Filter own messages
		if (Message.SenderId == PeerId)
		{
			continue;
		}
		
		// Filter messages for others
		if (!Message.ReceiverId.IsEmpty() && Message.ReceiverId != PeerId)
		{
			continue;
		}
		
		UE_LOG(LogOnlineICE, Verbose, TEXT("Signal received from %s (Type: %d, Candidates: %d)"), 
			*Message.SenderId, (int32)Message.Type, Message.Candidates.Num());
		
		// Notify listeners
		OnSignalReceived.Broadcast(Message);
	}
	
	// Cleanup old messages periodically
	CleanupOldMessages();
}

bool FLocalFileSignaling::IsActive() const
{
	return bIsActive;
}

FString FLocalFileSignaling::GetLocalPeerId() const
{
	return PeerId;
}

bool FLocalFileSignaling::EnsureSignalingDirectory()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	if (!PlatformFile.DirectoryExists(*SignalingDirectory))
	{
		if (!PlatformFile.CreateDirectoryTree(*SignalingDirectory))
		{
			return false;
		}
	}
	
	return true;
}

FString FLocalFileSignaling::GenerateMessageFileName() const
{
	// Format: signal_[timestamp]_[peerId]_[guid].json
	FDateTime Now = FDateTime::UtcNow();
	FString Timestamp = FString::Printf(TEXT("%lld"), Now.ToUnixTimestamp());
	FString Guid = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	
	return FString::Printf(TEXT("signal_%s_%s_%s.json"), *Timestamp, *PeerId, *Guid);
}

TArray<FICESignalMessage> FLocalFileSignaling::ReadPendingMessages()
{
	TArray<FICESignalMessage> Messages;
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	// List files in directory
	TArray<FString> Files;
	PlatformFile.FindFiles(Files, *SignalingDirectory, TEXT(".json"));
	
	// Sort by name (includes timestamp)
	Files.Sort();
	
	// Process new files
	for (int32 i = LastProcessedMessageIndex; i < Files.Num(); ++i)
	{
		FString FilePath = FPaths::Combine(SignalingDirectory, Files[i]);
		
		// Read file content
		FString JsonString;
		if (FFileHelper::LoadFileToString(JsonString, *FilePath))
		{
			FICESignalMessage Message = FICESignalMessage::FromJson(JsonString);
			Messages.Add(Message);
		}
		else
		{
			UE_LOG(LogOnlineICE, Warning, TEXT("Failed to read signal file: %s"), *FilePath);
		}
	}
	
	// Update index
	LastProcessedMessageIndex = Files.Num();
	
	return Messages;
}

void FLocalFileSignaling::CleanupOldMessages()
{
	// Cleanup messages older than 5 minutes
	const double MaxAgeSeconds = 300.0;
	FDateTime Now = FDateTime::UtcNow();
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	TArray<FString> Files;
	PlatformFile.FindFiles(Files, *SignalingDirectory, TEXT(".json"));
	
	for (const FString& FileName : Files)
	{
		FString FilePath = FPaths::Combine(SignalingDirectory, FileName);
		
		// Get file modification time
		FDateTime ModTime = PlatformFile.GetTimeStamp(*FilePath);
		
		// Calculate age
		FTimespan Age = Now - ModTime;
		
		if (Age.GetTotalSeconds() > MaxAgeSeconds)
		{
			PlatformFile.DeleteFile(*FilePath);
			UE_LOG(LogOnlineICE, VeryVerbose, TEXT("Cleaned up old signal file: %s"), *FileName);
		}
	}
}
