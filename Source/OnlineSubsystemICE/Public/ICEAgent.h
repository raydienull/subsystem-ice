// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemICEPackage.h"
#include "Delegates/Delegate.h"

class FSocket;

/**
 * Estados de conexión ICE
 */
enum class EICEConnectionState : uint8
{
	/** No iniciado */
	New,
	/** Recopilando candidatos */
	Gathering,
	/** Conectando usando candidatos host/srflx */
	ConnectingDirect,
	/** Conectando usando TURN */
	ConnectingRelay,
	/** Realizando handshake para verificar conexión bidireccional */
	PerformingHandshake,
	/** Conexión establecida */
	Connected,
	/** Error o desconexión */
	Failed
};

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
	 * @return True if gathering process started successfully
	 */
	bool GatherCandidates();

	/**
	 * Get all gathered candidates
	 * @return Array of gathered ICE candidates
	 */
	TArray<FICECandidate> GetLocalCandidates() const;

	/**
	 * Add a remote candidate received from peer
	 * @param Candidate - The remote candidate to add
	 */
	void AddRemoteCandidate(const FICECandidate& Candidate);

	/**
	 * Start connectivity checks with remote candidates
	 * Will attempt direct connection first, then fall back to relay if needed
	 * @return True if the process started successfully
	 */
	bool StartConnectivityChecks();

	/**
	 * Check if connection is established
	 * @return True if connected
	 */
	bool IsConnected() const;

	/**
	 * Tick function for periodic processing
	 * Handles connection retries and timeouts
	 * @param DeltaTime - Time elapsed since last tick
	 */
	void Tick(float DeltaTime);

	/**
	 * Get current connection state
	 * @return Current state of the ICE connection
	 */
	EICEConnectionState GetConnectionState() const 
	{
		FScopeLock Lock(&ConnectionLock);
		return ConnectionState;
	}

	/**
	 * Delegate for state change notifications
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectionStateChanged, EICEConnectionState);

	/**
	 * Bind to connection state changes
	 * @param InDelegate - The delegate to call when connection state changes
	 * @return Handle to the bound delegate
	 */
	FDelegateHandle OnConnectionStateChanged_Handle(const FOnConnectionStateChanged::FDelegate& InDelegate)
	{
		return OnConnectionStateChanged.Add(InDelegate);
	}

private:
	/** Event that fires when connection state changes */
	FOnConnectionStateChanged OnConnectionStateChanged;

	/**
	 * Send data through the established connection
	 * @param Data - The data to send
	 * @param Size - Size of the data in bytes
	 * @return True if send was successful
	 */
	bool SendData(const uint8* Data, int32 Size);

	/**
	 * Receive data from the connection
	 * @param Data - Buffer to receive data into
	 * @param MaxSize - Maximum size of the buffer
	 * @param OutSize - Number of bytes actually received
	 * @return True if receive was successful
	 */
	bool ReceiveData(uint8* Data, int32 MaxSize, int32& OutSize);

	/**
	 * Close the connection and clean up resources
	 */
	void Close();

	/**
	 * Send handshake packet to verify bidirectional communication
	 * @return True if handshake packet was sent successfully
	 */
	bool SendHandshake();

	/**
	 * Process received data and handle handshake packets
	 * @return True if data was processed successfully
	 */
	bool ProcessReceivedData();

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

	/** Current connection state */
	EICEConnectionState ConnectionState;

	/** Number of direct connection attempts made */
	int32 DirectConnectionAttempts;

	/** Maximum number of direct connection attempts before falling back to TURN */
	static constexpr int32 MAX_DIRECT_ATTEMPTS = 3;

	/** Delay between connection attempts (seconds) */
	float RetryDelay;

	/** Time elapsed since last connection attempt */
	float TimeSinceLastAttempt;

	/** Thread-safe access to connection state */
	mutable FCriticalSection ConnectionLock;

	/** Handshake state tracking */
	bool bHandshakeSent;
	bool bHandshakeReceived;
	float HandshakeTimeout;
	float TimeSinceHandshakeStart;
	float TimeSinceLastHandshakeSend;
	
	/** Maximum time to wait for handshake response (seconds) */
	static constexpr float MAX_HANDSHAKE_TIMEOUT = 5.0f;
	
	/** Retry interval for handshake packets (seconds) */
	static constexpr float HANDSHAKE_RETRY_INTERVAL = 1.0f;

	/**
	 * Update the current connection state and notify listeners
	 * @param NewState - The new connection state to transition to
	 */
	void UpdateConnectionState(EICEConnectionState NewState);

	/**
	 * Get string representation of connection state
	 * @param State - The state to convert to string
	 * @return String representation of the state
	 */
	FString GetConnectionStateName(EICEConnectionState State) const;

	/** Gather host candidates (local network interfaces) */
	void GatherHostCandidates();

	/** Gather server reflexive candidates (via STUN) */
	void GatherServerReflexiveCandidates();

	/** Gather relayed candidates (via TURN) */
	void GatherRelayedCandidates();

	/** Perform STUN binding request */
	bool PerformSTUNRequest(const FString& ServerAddress, FString& OutPublicIP, int32& OutPublicPort);

	/** Perform TURN allocation request */
	bool PerformTURNAllocation(const FString& ServerAddress, const FString& Username, const FString& Credential, FString& OutRelayIP, int32& OutRelayPort);

	/** Perform TURN allocation request with optional authentication */
	bool PerformTURNAllocationRequest(FSocket* TURNSocket, const TSharedPtr<FInternetAddr>& TURNAddr, 
		const FString& Username, const FString& Credential, const FString& Realm, const FString& Nonce,
		FString& OutRelayIP, int32& OutRelayPort, bool bIsRetry = false);

	/** Calculate candidate priority */
	int32 CalculatePriority(EICECandidateType Type, int32 LocalPreference, int32 ComponentId);

	/** Helper function to calculate MD5 hash for TURN authentication */
	void CalculateMD5(const FString& Input, uint8* OutHash);

	/** Helper function to calculate HMAC-SHA1 for MESSAGE-INTEGRITY */
	void CalculateHMACSHA1(const uint8* Data, int32 DataLen, const uint8* Key, int32 KeyLen, uint8* OutHash);
};
