// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemICEPackage.h"

class FSocket;

/**
 * Types of ICE candidates
 */
enum class EICECandidateType : uint8
{
	Host,        // Local network interface
	ServerReflexive,  // STUN-discovered public address
	Relayed      // TURN relay address
};

/**
 * Represents an ICE candidate (potential connection path)
 */
struct FICECandidate
{
	FString Foundation;
	int32 ComponentId;
	FString Transport;
	int32 Priority;
	FString Address;
	int32 Port;
	EICECandidateType Type;
	FString RelatedAddress;
	int32 RelatedPort;

	FICECandidate()
		: ComponentId(0)
		, Transport(TEXT("UDP"))
		, Priority(0)
		, Port(0)
		, Type(EICECandidateType::Host)
		, RelatedPort(0)
	{}

	FString ToString() const;
	static FICECandidate FromString(const FString& CandidateString);
};

/**
 * Configuration for ICE agent
 */
struct FICEAgentConfig
{
	TArray<FString> STUNServers;
	TArray<FString> TURNServers;
	FString TURNUsername;
	FString TURNCredential;
	bool bEnableIPv6;

	FICEAgentConfig()
		: bEnableIPv6(false)
	{}
};

/**
 * ICE Agent implementation
 * Handles candidate gathering, connectivity checks, and connection establishment
 */
class FICEAgent
{
public:
	FICEAgent(const FICEAgentConfig& Config);
	~FICEAgent();

	/**
	 * Start gathering ICE candidates
	 */
	bool GatherCandidates();

	/**
	 * Get all gathered candidates
	 */
	TArray<FICECandidate> GetLocalCandidates() const;

	/**
	 * Add a remote candidate received from peer
	 */
	void AddRemoteCandidate(const FICECandidate& Candidate);

	/**
	 * Start connectivity checks with remote candidates
	 */
	bool StartConnectivityChecks();

	/**
	 * Check if connection is established
	 */
	bool IsConnected() const;

	/**
	 * Send data through the established connection
	 */
	bool SendData(const uint8* Data, int32 Size);

	/**
	 * Receive data from the connection
	 */
	bool ReceiveData(uint8* Data, int32 MaxSize, int32& OutSize);

	/**
	 * Tick function for periodic processing
	 */
	void Tick(float DeltaTime);

	/**
	 * Close the connection
	 */
	void Close();

private:
	/** Agent configuration */
	FICEAgentConfig Config;

	/** Local candidates */
	TArray<FICECandidate> LocalCandidates;

	/** Remote candidates */
	TArray<FICECandidate> RemoteCandidates;

	/** Socket for communication */
	FSocket* Socket;

	/** Connection state */
	bool bIsConnected;

	/** Selected candidate pair */
	FICECandidate SelectedLocalCandidate;
	FICECandidate SelectedRemoteCandidate;

	/** Gather host candidates (local network interfaces) */
	void GatherHostCandidates();

	/** Gather server reflexive candidates (via STUN) */
	void GatherServerReflexiveCandidates();

	/** Gather relayed candidates (via TURN) */
	void GatherRelayedCandidates();

	/** Perform STUN binding request */
	bool PerformSTUNRequest(const FString& ServerAddress, FString& OutPublicIP, int32& OutPublicPort);

	/** Calculate candidate priority */
	int32 CalculatePriority(EICECandidateType Type, int32 LocalPreference, int32 ComponentId);
};
