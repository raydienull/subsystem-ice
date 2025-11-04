// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ICEAgent.h"

/**
 * ICE signaling message types
 */
enum class EICESignalType : uint8
{
	/** Session offer (host candidates) */
	Offer,
	/** Session answer (client candidates) */
	Answer,
	/** Individual ICE candidate */
	Candidate
};

/**
 * ICE signaling message
 */
struct FICESignalMessage
{
	/** Message type */
	EICESignalType Type;
	
	/** Session ID */
	FString SessionId;
	
	/** Sender peer ID */
	FString SenderId;
	
	/** Receiver peer ID (empty for broadcast) */
	FString ReceiverId;
	
	/** ICE candidates */
	TArray<FICECandidate> Candidates;
	
	/** Additional message metadata */
	TMap<FString, FString> Metadata;
	
	/** Message timestamp */
	FDateTime Timestamp;
	
	FICESignalMessage()
		: Type(EICESignalType::Candidate)
		, Timestamp(FDateTime::UtcNow())
	{}
	
	/** Convert message to JSON */
	FString ToJson() const;
	
	/** Create message from JSON */
	static FICESignalMessage FromJson(const FString& JsonString);
};

/**
 * Delegate for signaling message received notifications
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSignalMessageReceived, const FICESignalMessage&);

/**
 * Abstract interface for ICE signaling mechanisms
 * Allows candidate exchange between peers
 */
class IICESignaling
{
public:
	virtual ~IICESignaling() = default;
	
	/**
	 * Initialize the signaling system
	 * @return True if initialization was successful
	 */
	virtual bool Initialize() = 0;
	
	/**
	 * Shutdown the signaling system
	 */
	virtual void Shutdown() = 0;
	
	/**
	 * Send a signaling message
	 * @param Message - Message to send
	 * @return True if send was successful
	 */
	virtual bool SendSignal(const FICESignalMessage& Message) = 0;
	
	/**
	 * Process pending signaling messages
	 * Should be called periodically (tick)
	 */
	virtual void ProcessSignals() = 0;
	
	/**
	 * Check if signaling system is active
	 * @return True if connected/active
	 */
	virtual bool IsActive() const = 0;
	
	/**
	 * Get unique ID of this peer
	 * @return Local peer ID
	 */
	virtual FString GetLocalPeerId() const = 0;
	
	/**
	 * Register callback for received messages
	 */
	FOnSignalMessageReceived OnSignalReceived;
};

/**
 * File-based signaling implementation for local testing
 * Useful for local testing without a signaling server
 */
class FLocalFileSignaling : public IICESignaling
{
public:
	FLocalFileSignaling(const FString& SharedDirectory);
	virtual ~FLocalFileSignaling();
	
	// IICESignaling Interface
	virtual bool Initialize() override;
	virtual void Shutdown() override;
	virtual bool SendSignal(const FICESignalMessage& Message) override;
	virtual void ProcessSignals() override;
	virtual bool IsActive() const override;
	virtual FString GetLocalPeerId() const override;
	
private:
	/** Shared directory for signaling files */
	FString SignalingDirectory;
	
	/** Unique ID of this peer */
	FString PeerId;
	
	/** Index of last processed message */
	int32 LastProcessedMessageIndex;
	
	/** Whether system is active */
	bool bIsActive;
	
	/** Create signaling directory if it doesn't exist */
	bool EnsureSignalingDirectory();
	
	/** Generate file name for message */
	FString GenerateMessageFileName() const;
	
	/** Read messages from files */
	TArray<FICESignalMessage> ReadPendingMessages();
	
	/** Cleanup old messages */
	void CleanupOldMessages();
};
