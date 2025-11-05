// Copyright Epic Games, Inc. All Rights Reserved.

#include "ICEAgent.h"
#include "OnlineSubsystemICEPackage.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/SecureHash.h"

// ICEAgent.cpp ya no necesita definir la categoría de log, se mueve al módulo

// STUN/TURN protocol constants (RFC 5389/5766)
namespace STUNConstants
{
	constexpr int32 TRANSACTION_ID_LENGTH = 12;
	constexpr int32 MESSAGE_INTEGRITY_ATTR_SIZE = 24; // Header(4) + HMAC-SHA1(20)
	constexpr int32 HMAC_SHA1_SIZE = 20;
	constexpr uint8 ERROR_CLASS_MASK = 0x07;
	constexpr int32 ERROR_CLASS_MULTIPLIER = 100;
	constexpr int32 SHA1_BLOCK_SIZE = 64;
	
	// STUN Magic Cookie (RFC 5389)
	constexpr uint32 MAGIC_COOKIE = 0x2112A442;
	constexpr uint16 MAGIC_COOKIE_HIGH = 0x2112; // First 16 bits of magic cookie
	
	// TURN Channel number range (RFC 5766)
	constexpr uint16 CHANNEL_NUMBER_MIN = 0x4000;
	constexpr uint16 CHANNEL_NUMBER_MAX = 0x7FFF;
	
	// Packet format detection bits
	constexpr uint8 PACKET_TYPE_MASK = 0xC0;
	constexpr uint8 PACKET_TYPE_STUN = 0x00;      // STUN message: bits 00
	constexpr uint8 PACKET_TYPE_CHANNEL_DATA = 0x40; // ChannelData: bits 01
}

// Handshake protocol constants
namespace HandshakeConstants
{
	static const uint8 MAGIC_NUMBER[4] = {0x49, 0x43, 0x45, 0x48}; // "ICEH"
	constexpr uint8 PACKET_TYPE_HELLO_REQUEST = 0x01;
	constexpr uint8 PACKET_TYPE_HELLO_RESPONSE = 0x02;
	constexpr int32 HANDSHAKE_PACKET_SIZE = 9;
	constexpr int32 MAX_RECEIVE_BUFFER_SIZE = 1024;
}

FString FICECandidate::ToString() const
{
	return FString::Printf(TEXT("candidate:%s %d %s %d %s %d typ %s"),
		*Foundation,
		ComponentId,
		*Transport,
		Priority,
		*Address,
		Port,
		Type == EICECandidateType::Host ? TEXT("host") :
		Type == EICECandidateType::ServerReflexive ? TEXT("srflx") : TEXT("relay"));
}

FICECandidate FICECandidate::FromString(const FString& CandidateString)
{
	FICECandidate Candidate;
	
	// Strip "candidate:" prefix if present
	FString ParseString = CandidateString;
	if (ParseString.StartsWith(TEXT("candidate:")))
	{
		ParseString = ParseString.RightChop(10); // Remove "candidate:" (10 characters)
	}
	
	// Parse ICE candidate string (simplified parser)
	TArray<FString> Parts;
	ParseString.ParseIntoArray(Parts, TEXT(" "));
	
	if (Parts.Num() >= 8)
	{
		Candidate.Foundation = Parts[0];
		Candidate.ComponentId = FCString::Atoi(*Parts[1]);
		Candidate.Transport = Parts[2];
		Candidate.Priority = FCString::Atoi(*Parts[3]);
		Candidate.Address = Parts[4];
		Candidate.Port = FCString::Atoi(*Parts[5]);
		
		if (Parts[6] == TEXT("typ"))
		{
			if (Parts[7] == TEXT("host"))
			{
				Candidate.Type = EICECandidateType::Host;
			}
			else if (Parts[7] == TEXT("srflx"))
			{
				Candidate.Type = EICECandidateType::ServerReflexive;
			}
			else if (Parts[7] == TEXT("relay"))
			{
				Candidate.Type = EICECandidateType::Relayed;
			}
		}
	}
	
	return Candidate;
}

FICEAgent::FICEAgent(const FICEAgentConfig& InConfig)
	: Config(InConfig)
	, Socket(nullptr)
	, TURNSocket(nullptr)
	, TURNAllocationLifetime(600)
	, TimeSinceTURNRefresh(0.0f)
	, TURNChannelNumber(STUNConstants::CHANNEL_NUMBER_MIN)
	, bTURNAllocationActive(false)
	, bIsConnected(false)
	, ConnectionState(EICEConnectionState::New)
	, DirectConnectionAttempts(0)
	, TotalConnectionAttempts(0)
	, RetryDelay(1.0f)
	, TimeSinceLastAttempt(0.0f)
	, bHandshakeSent(false)
	, bHandshakeReceived(false)
	, HandshakeTimeout(MAX_HANDSHAKE_TIMEOUT)
	, TimeSinceHandshakeStart(0.0f)
	, TimeSinceLastHandshakeSend(0.0f)
{
	FMemory::Memzero(TURNTransactionID, sizeof(TURNTransactionID));
}

FICEAgent::~FICEAgent()
{
	Close();
}

bool FICEAgent::GatherCandidates()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Gathering ICE candidates"));

	LocalCandidates.Empty();

	// Gather host candidates
	GatherHostCandidates();

	// Gather server reflexive candidates (STUN)
	if (Config.STUNServers.Num() > 0)
	{
		GatherServerReflexiveCandidates();
	}

	// Gather relayed candidates (TURN)
	if (Config.TURNServers.Num() > 0)
	{
		GatherRelayedCandidates();
	}

	UE_LOG(LogOnlineICE, Log, TEXT("Gathered %d ICE candidates"), LocalCandidates.Num());
	return LocalCandidates.Num() > 0;
}

void FICEAgent::GatherHostCandidates()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Gathering host candidates"));

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to get socket subsystem"));
		return;
	}

	// Get local address
	bool bCanBindAll;
	TSharedPtr<FInternetAddr> LocalAddr = SocketSubsystem->GetLocalHostAddr(*GLog, bCanBindAll);
	if (!LocalAddr.IsValid() || !LocalAddr->IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to get local address"));
		return;
	}

	FICECandidate HostCandidate;
	HostCandidate.Foundation = TEXT("1");
	HostCandidate.ComponentId = 1;
	HostCandidate.Transport = TEXT("UDP");
	HostCandidate.Priority = CalculatePriority(EICECandidateType::Host, 65535, 1);
	HostCandidate.Address = LocalAddr->ToString(false);
	// Port will be assigned when socket is created during connectivity checks
	// We use 0 to indicate "any available port"
	HostCandidate.Port = 0;
	HostCandidate.Type = EICECandidateType::Host;

	LocalCandidates.Add(HostCandidate);

	UE_LOG(LogOnlineICE, Log, TEXT("Added host candidate: %s"), *HostCandidate.ToString());
}

void FICEAgent::GatherServerReflexiveCandidates()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Gathering server reflexive candidates"));

	for (const FString& STUNServer : Config.STUNServers)
	{
		FString PublicIP;
		int32 PublicPort;

		if (PerformSTUNRequest(STUNServer, PublicIP, PublicPort))
		{
			FICECandidate SrflxCandidate;
			SrflxCandidate.Foundation = TEXT("2");
			SrflxCandidate.ComponentId = 1;
			SrflxCandidate.Transport = TEXT("UDP");
			SrflxCandidate.Priority = CalculatePriority(EICECandidateType::ServerReflexive, 65535, 1);
			SrflxCandidate.Address = PublicIP;
			SrflxCandidate.Port = PublicPort;
			SrflxCandidate.Type = EICECandidateType::ServerReflexive;

			LocalCandidates.Add(SrflxCandidate);

			UE_LOG(LogOnlineICE, Log, TEXT("Added server reflexive candidate: %s"), *SrflxCandidate.ToString());
			break; // Only need one STUN server to succeed
		}
	}
}

void FICEAgent::GatherRelayedCandidates()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Gathering relayed candidates via TURN"));

	if (Config.TURNServers.Num() == 0)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("No TURN servers configured"));
		return;
	}

	for (const FString& TURNServer : Config.TURNServers)
	{
		FString RelayIP;
		int32 RelayPort;

		if (PerformTURNAllocation(TURNServer, Config.TURNUsername, Config.TURNCredential, RelayIP, RelayPort))
		{
			FICECandidate RelayCandidate;
			RelayCandidate.Foundation = TEXT("3");
			RelayCandidate.ComponentId = 1;
			RelayCandidate.Transport = TEXT("UDP");
			RelayCandidate.Priority = CalculatePriority(EICECandidateType::Relayed, 65535, 1);
			RelayCandidate.Address = RelayIP;
			RelayCandidate.Port = RelayPort;
			RelayCandidate.Type = EICECandidateType::Relayed;

			LocalCandidates.Add(RelayCandidate);

			UE_LOG(LogOnlineICE, Log, TEXT("Added relay candidate: %s"), *RelayCandidate.ToString());
			break; // Only need one TURN server to succeed
		}
	}
}

bool FICEAgent::PerformSTUNRequest(const FString& ServerAddress, FString& OutPublicIP, int32& OutPublicPort)
{
	UE_LOG(LogOnlineICE, Log, TEXT("Performing STUN request to: %s"), *ServerAddress);

	// Parse server address
	FString Host;
	int32 Port = 3478; // Default STUN port
	
	int32 ColonPos;
	if (ServerAddress.FindChar(':', ColonPos))
	{
		Host = ServerAddress.Left(ColonPos);
		Port = FCString::Atoi(*ServerAddress.Mid(ColonPos + 1));
	}
	else
	{
		Host = ServerAddress;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to get socket subsystem"));
		return false;
	}

	// Resolve STUN server address
	TSharedPtr<FInternetAddr> STUNAddr = SocketSubsystem->GetAddressFromString(Host);
	if (!STUNAddr.IsValid() || !STUNAddr->IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to resolve STUN server: %s"), *Host);
		return false;
	}

	STUNAddr->SetPort(Port);

	// Create a temporary socket for STUN request
	FSocket* STUNSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("STUN"), STUNAddr->GetProtocolType());
	if (!STUNSocket)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to create STUN socket"));
		return false;
	}

	// Build STUN Binding Request
	// STUN message format: Type (2 bytes) | Length (2 bytes) | Magic Cookie (4 bytes) | Transaction ID (12 bytes)
	uint8 STUNRequest[20];
	FMemory::Memzero(STUNRequest, sizeof(STUNRequest));

	// Message Type: Binding Request (0x0001)
	STUNRequest[0] = 0x00;
	STUNRequest[1] = 0x01;

	// Message Length: 0 (no attributes for basic request)
	STUNRequest[2] = 0x00;
	STUNRequest[3] = 0x00;

	// Magic Cookie: 0x2112A442
	STUNRequest[4] = 0x21;
	STUNRequest[5] = 0x12;
	STUNRequest[6] = 0xA4;
	STUNRequest[7] = 0x42;

	// Transaction ID: Random 12 bytes
	for (int32 i = 8; i < 20; i++)
	{
		STUNRequest[i] = FMath::Rand() & 0xFF;
	}

	// Send STUN request
	int32 BytesSent;
	if (!STUNSocket->SendTo(STUNRequest, sizeof(STUNRequest), BytesSent, *STUNAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to send STUN request"));
		SocketSubsystem->DestroySocket(STUNSocket);
		return false;
	}

	// Wait for response with timeout
	if (!STUNSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0)))
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("STUN request timeout"));
		SocketSubsystem->DestroySocket(STUNSocket);
		return false;
	}

	// Receive STUN response
	uint8 STUNResponse[1024];
	int32 BytesRead = 0;
	TSharedRef<FInternetAddr> FromAddr = SocketSubsystem->CreateInternetAddr();

	if (!STUNSocket->RecvFrom(STUNResponse, sizeof(STUNResponse), BytesRead, *FromAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to receive STUN response"));
		SocketSubsystem->DestroySocket(STUNSocket);
		return false;
	}

	// Parse STUN response (simplified)
	if (BytesRead >= 20)
	{
		uint16 MessageType = (STUNResponse[0] << 8) | STUNResponse[1];
		uint16 MessageLength = (STUNResponse[2] << 8) | STUNResponse[3];

		// Check if it's a Binding Response (0x0101)
		if (MessageType == 0x0101 && BytesRead >= 20 + MessageLength)
		{
			// Parse attributes to find XOR-MAPPED-ADDRESS (0x0020)
			int32 Offset = 20;
			while (Offset < BytesRead)
			{
				if (Offset + 4 > BytesRead) break;

				uint16 AttrType = (STUNResponse[Offset] << 8) | STUNResponse[Offset + 1];
				uint16 AttrLength = (STUNResponse[Offset + 2] << 8) | STUNResponse[Offset + 3];

				if (AttrType == 0x0020 && AttrLength >= 8) // XOR-MAPPED-ADDRESS
				{
					// Ensure we have enough bytes to read the attribute value
					if (Offset + 4 + AttrLength > BytesRead)
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("Incomplete XOR-MAPPED-ADDRESS attribute"));
						break;
					}

					// Family (1 byte at offset 5)
					uint8 Family = STUNResponse[Offset + 5];

					if (Family == 0x01) // IPv4
					{
						// XOR-ed Port (2 bytes at offset 6-7)
						uint16 XorPort = (STUNResponse[Offset + 6] << 8) | STUNResponse[Offset + 7];
						OutPublicPort = XorPort ^ STUNConstants::MAGIC_COOKIE_HIGH;

						// XOR-ed IP (4 bytes at offset 8-11)
						uint32 XorIP = (STUNResponse[Offset + 8] << 24) | (STUNResponse[Offset + 9] << 16) |
						              (STUNResponse[Offset + 10] << 8) | STUNResponse[Offset + 11];
						uint32 PublicIPValue = XorIP ^ STUNConstants::MAGIC_COOKIE;

						// Convert to string
						OutPublicIP = FString::Printf(TEXT("%d.%d.%d.%d"),
							(PublicIPValue >> 24) & 0xFF,
							(PublicIPValue >> 16) & 0xFF,
							(PublicIPValue >> 8) & 0xFF,
							PublicIPValue & 0xFF);

						UE_LOG(LogOnlineICE, Log, TEXT("STUN discovered public address: %s:%d"), *OutPublicIP, OutPublicPort);

						SocketSubsystem->DestroySocket(STUNSocket);
						return true;
					}
				}

				// Move to next attribute (pad to 4-byte boundary)
				Offset += 4 + ((AttrLength + 3) & ~3);
			}
		}
	}

	UE_LOG(LogOnlineICE, Warning, TEXT("Failed to parse STUN response"));
	SocketSubsystem->DestroySocket(STUNSocket);
	return false;
}

bool FICEAgent::PerformTURNAllocation(const FString& ServerAddress, const FString& Username, const FString& Credential, FString& OutRelayIP, int32& OutRelayPort)
{
	UE_LOG(LogOnlineICE, Log, TEXT("Performing TURN allocation to: %s"), *ServerAddress);

	if (Username.IsEmpty() || Credential.IsEmpty())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("TURN username or credential not configured"));
		return false;
	}

	// Parse server address
	FString Host;
	int32 Port = 3478; // Default TURN port

	int32 ColonPos;
	if (ServerAddress.FindChar(':', ColonPos))
	{
		Host = ServerAddress.Left(ColonPos);
		Port = FCString::Atoi(*ServerAddress.Mid(ColonPos + 1));
	}
	else
	{
		Host = ServerAddress;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to get socket subsystem"));
		return false;
	}

	// Resolve TURN server address
	TSharedPtr<FInternetAddr> TURNAddr = SocketSubsystem->GetAddressFromString(Host);
	if (!TURNAddr.IsValid() || !TURNAddr->IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to resolve TURN server: %s"), *Host);
		return false;
	}

	TURNAddr->SetPort(Port);

	// Clean up existing TURN socket if any
	if (TURNSocket)
	{
		SocketSubsystem->DestroySocket(TURNSocket);
		TURNSocket = nullptr;
	}

	// Create socket for TURN (keep it persistent for refresh and data relay)
	TURNSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("TURN"), TURNAddr->GetProtocolType());
	if (!TURNSocket)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to create TURN socket"));
		return false;
	}

	// Store TURN server address for later use (refresh, permissions, etc.)
	TURNServerAddr = TURNAddr;

	// First attempt: Send request without authentication to get realm and nonce
	// The server will respond with 401 Unauthorized if authentication is required
	bool bSuccess = PerformTURNAllocationRequest(TURNSocket, TURNAddr, Username, Credential, FString(), FString(), OutRelayIP, OutRelayPort, false);
	
	if (bSuccess)
	{
		// Store relay address for data transmission
		TURNRelayAddr = SocketSubsystem->GetAddressFromString(OutRelayIP);
		if (TURNRelayAddr.IsValid())
		{
			TURNRelayAddr->SetPort(OutRelayPort);
		}
		
		bTURNAllocationActive = true;
		TimeSinceTURNRefresh = 0.0f;
		
		UE_LOG(LogOnlineICE, Log, TEXT("TURN allocation successful, keeping socket open for data relay"));
	}
	else
	{
		// Clean up on failure
		SocketSubsystem->DestroySocket(TURNSocket);
		TURNSocket = nullptr;
	}
	
	return bSuccess;
}

bool FICEAgent::PerformTURNAllocationRequest(FSocket* TURNSocket, const TSharedPtr<FInternetAddr>& TURNAddr, 
	const FString& Username, const FString& Credential, const FString& Realm, const FString& Nonce,
	FString& OutRelayIP, int32& OutRelayPort, bool bIsRetry)
{
	// Build TURN Allocate Request (RFC 5766)
	// Message format: Type (2) | Length (2) | Magic Cookie (4) | Transaction ID (12) | Attributes
	TArray<uint8> TURNRequest;
	// 512 bytes to accommodate header(20) + REQUESTED-TRANSPORT(8) + USERNAME(variable) + 
	// REALM(variable) + NONCE(variable) + MESSAGE-INTEGRITY(24) with padding
	TURNRequest.SetNum(512);
	FMemory::Memzero(TURNRequest.GetData(), TURNRequest.Num());

	int32 Offset = 0;

	// Message Type: Allocate Request (0x0003)
	TURNRequest[Offset++] = 0x00;
	TURNRequest[Offset++] = 0x03;

	// Message Length: Will be set later
	int32 LengthOffset = Offset;
	Offset += 2;

	// Magic Cookie: 0x2112A442
	TURNRequest[Offset++] = 0x21;
	TURNRequest[Offset++] = 0x12;
	TURNRequest[Offset++] = 0xA4;
	TURNRequest[Offset++] = 0x42;

	// Transaction ID: Random bytes (RFC 5389)
	uint8 TransactionID[STUNConstants::TRANSACTION_ID_LENGTH];
	for (int32 i = 0; i < STUNConstants::TRANSACTION_ID_LENGTH; i++)
	{
		TransactionID[i] = FMath::Rand() & 0xFF;
		TURNRequest[Offset++] = TransactionID[i];
	}

	// Add REQUESTED-TRANSPORT attribute (UDP = 17)
	// Type: 0x0019, Length: 4
	TURNRequest[Offset++] = 0x00;
	TURNRequest[Offset++] = 0x19;
	TURNRequest[Offset++] = 0x00;
	TURNRequest[Offset++] = 0x04;
	TURNRequest[Offset++] = 17; // UDP protocol
	TURNRequest[Offset++] = 0x00;
	TURNRequest[Offset++] = 0x00;
	TURNRequest[Offset++] = 0x00;

	// Add USERNAME attribute
	// Type: 0x0006
	int32 UsernameLen = Username.Len();
	TURNRequest[Offset++] = 0x00;
	TURNRequest[Offset++] = 0x06;
	TURNRequest[Offset++] = (UsernameLen >> 8) & 0xFF;
	TURNRequest[Offset++] = UsernameLen & 0xFF;
	for (int32 i = 0; i < UsernameLen; i++)
	{
		TURNRequest[Offset++] = Username[i];
	}
	// Pad to 4-byte boundary
	while (Offset % 4 != 0)
	{
		TURNRequest[Offset++] = 0x00;
	}

	// If we have Realm and Nonce, add authentication attributes
	if (!Realm.IsEmpty() && !Nonce.IsEmpty())
	{
		// Add REALM attribute
		// Type: 0x0014
		int32 RealmLen = Realm.Len();
		TURNRequest[Offset++] = 0x00;
		TURNRequest[Offset++] = 0x14;
		TURNRequest[Offset++] = (RealmLen >> 8) & 0xFF;
		TURNRequest[Offset++] = RealmLen & 0xFF;
		for (int32 i = 0; i < RealmLen; i++)
		{
			TURNRequest[Offset++] = Realm[i];
		}
		while (Offset % 4 != 0)
		{
			TURNRequest[Offset++] = 0x00;
		}

		// Add NONCE attribute
		// Type: 0x0015
		int32 NonceLen = Nonce.Len();
		TURNRequest[Offset++] = 0x00;
		TURNRequest[Offset++] = 0x15;
		TURNRequest[Offset++] = (NonceLen >> 8) & 0xFF;
		TURNRequest[Offset++] = NonceLen & 0xFF;
		for (int32 i = 0; i < NonceLen; i++)
		{
			TURNRequest[Offset++] = Nonce[i];
		}
		while (Offset % 4 != 0)
		{
			TURNRequest[Offset++] = 0x00;
		}

		// Add MESSAGE-INTEGRITY attribute (20 bytes HMAC-SHA1)
		// Type: 0x0008, Length: 20
		// Note: This must be the last attribute before FINGERPRINT (which we don't use)
		int32 MessageIntegrityOffset = Offset;
		TURNRequest[Offset++] = 0x00;
		TURNRequest[Offset++] = 0x08;
		TURNRequest[Offset++] = 0x00;
		TURNRequest[Offset++] = 0x14; // Length = 20 bytes
		
		// Placeholder for HMAC-SHA1 (will be calculated)
		for (int32 i = 0; i < 20; i++)
		{
			TURNRequest[Offset++] = 0x00;
		}

		// Calculate HMAC-SHA1 for MESSAGE-INTEGRITY
		// According to RFC 5389 Section 15.4, the length field is adjusted to point to 
		// the end of MESSAGE-INTEGRITY attribute before calculating HMAC
		int32 MessageLengthForIntegrity = MessageIntegrityOffset - 20 + STUNConstants::MESSAGE_INTEGRITY_ATTR_SIZE;
		TURNRequest[LengthOffset] = (MessageLengthForIntegrity >> 8) & 0xFF;
		TURNRequest[LengthOffset + 1] = MessageLengthForIntegrity & 0xFF;

		// Key = MD5(username:realm:password) for long-term credentials
		FString KeyString = Username + TEXT(":") + Realm + TEXT(":") + Credential;
		uint8 KeyMD5[16];
		CalculateMD5(KeyString, KeyMD5);

		// Calculate HMAC-SHA1 over the message from the STUN header up to (and including)
		// the attribute preceding MESSAGE-INTEGRITY, which means excluding MESSAGE-INTEGRITY itself
		// RFC 5389 Section 15.4: "from the STUN header up to, and including, the attribute preceding MESSAGE-INTEGRITY"
		uint8 HMAC[20];
		CalculateHMACSHA1(TURNRequest.GetData(), MessageIntegrityOffset, KeyMD5, 16, HMAC);
		
		// Copy HMAC to MESSAGE-INTEGRITY attribute
		for (int32 i = 0; i < 20; i++)
		{
			TURNRequest[MessageIntegrityOffset + 4 + i] = HMAC[i];
		}
		
		// Note: The length field has already been set to MessageLengthForIntegrity for 
		// MESSAGE-INTEGRITY validation. We must NOT overwrite it, as per RFC 5389 Section 15.4.
	}
	else
	{
		// Calculate final message length (everything after the header)
		// Only do this when MESSAGE-INTEGRITY is not present
		int32 MessageLength = Offset - 20;
		TURNRequest[LengthOffset] = (MessageLength >> 8) & 0xFF;
		TURNRequest[LengthOffset + 1] = MessageLength & 0xFF;
	}

	// Resize to actual size
	TURNRequest.SetNum(Offset);

	// Send TURN Allocate request
	int32 BytesSent;
	if (!TURNSocket->SendTo(TURNRequest.GetData(), TURNRequest.Num(), BytesSent, *TURNAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to send TURN Allocate request"));
		return false;
	}

	// Wait for response with timeout
	if (!TURNSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0)))
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("TURN Allocate request timeout"));
		return false;
	}

	// Receive TURN response
	uint8 TURNResponse[1024];
	int32 BytesRead = 0;
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to get socket subsystem"));
		return false;
	}
	TSharedRef<FInternetAddr> FromAddr = SocketSubsystem->CreateInternetAddr();

	if (!TURNSocket->RecvFrom(TURNResponse, sizeof(TURNResponse), BytesRead, *FromAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to receive TURN response"));
		return false;
	}

	// Parse TURN response
	if (BytesRead >= 20)
	{
		uint16 MessageType = (TURNResponse[0] << 8) | TURNResponse[1];
		uint16 MessageLength = (TURNResponse[2] << 8) | TURNResponse[3];

		// Check if it's an Allocate Success Response (0x0103)
		if (MessageType == 0x0103 && BytesRead >= 20 + MessageLength)
		{
			UE_LOG(LogOnlineICE, Log, TEXT("TURN Allocate successful"));

			bool bFoundRelayAddress = false;

			// Parse attributes to find XOR-RELAYED-ADDRESS (0x0016) and LIFETIME (0x000D)
			int32 AttrOffset = 20;
			while (AttrOffset < BytesRead)
			{
				if (AttrOffset + 4 > BytesRead) break;

				uint16 AttrType = (TURNResponse[AttrOffset] << 8) | TURNResponse[AttrOffset + 1];
				uint16 AttrLength = (TURNResponse[AttrOffset + 2] << 8) | TURNResponse[AttrOffset + 3];

				if (AttrType == 0x0016 && AttrLength >= 8) // XOR-RELAYED-ADDRESS
				{
					// Ensure we have enough bytes to read the attribute value
					if (AttrOffset + 4 + AttrLength > BytesRead)
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("Incomplete XOR-RELAYED-ADDRESS attribute"));
						break;
					}

					// Family (1 byte at offset 5)
					uint8 Family = TURNResponse[AttrOffset + 5];

					if (Family == 0x01) // IPv4
					{
						// XOR-ed Port (2 bytes at offset 6-7)
						uint16 XorPort = (TURNResponse[AttrOffset + 6] << 8) | TURNResponse[AttrOffset + 7];
						OutRelayPort = XorPort ^ STUNConstants::MAGIC_COOKIE_HIGH;

						// XOR-ed IP (4 bytes at offset 8-11)
						uint32 XorIP = (TURNResponse[AttrOffset + 8] << 24) | (TURNResponse[AttrOffset + 9] << 16) |
						              (TURNResponse[AttrOffset + 10] << 8) | TURNResponse[AttrOffset + 11];
						uint32 RelayIPValue = XorIP ^ STUNConstants::MAGIC_COOKIE;

						// Convert to string
						OutRelayIP = FString::Printf(TEXT("%d.%d.%d.%d"),
							(RelayIPValue >> 24) & 0xFF,
							(RelayIPValue >> 16) & 0xFF,
							(RelayIPValue >> 8) & 0xFF,
							RelayIPValue & 0xFF);

						UE_LOG(LogOnlineICE, Log, TEXT("TURN allocated relay address: %s:%d"), *OutRelayIP, OutRelayPort);
						bFoundRelayAddress = true;
					}
				}
				else if (AttrType == 0x000D && AttrLength == 4) // LIFETIME
				{
					// Parse lifetime value (4 bytes, big-endian)
					if (AttrOffset + 8 <= BytesRead)
					{
						TURNAllocationLifetime = (TURNResponse[AttrOffset + 4] << 24) | 
						                         (TURNResponse[AttrOffset + 5] << 16) |
						                         (TURNResponse[AttrOffset + 6] << 8) | 
						                         TURNResponse[AttrOffset + 7];
						UE_LOG(LogOnlineICE, Log, TEXT("TURN allocation lifetime: %d seconds"), TURNAllocationLifetime);
					}
				}

				// Move to next attribute (pad to 4-byte boundary)
				AttrOffset += 4 + ((AttrLength + 3) & ~3);
			}

			return bFoundRelayAddress;
		}
		else if (MessageType == 0x0113) // Allocate Error Response
		{
			// Parse error response to extract error code and authentication info
			int32 ErrorCode = 0;
			FString ErrorRealm;
			FString ErrorNonce;
			
			int32 AttrOffset = 20;
			while (AttrOffset < BytesRead)
			{
				if (AttrOffset + 4 > BytesRead) break;

				uint16 AttrType = (TURNResponse[AttrOffset] << 8) | TURNResponse[AttrOffset + 1];
				uint16 AttrLength = (TURNResponse[AttrOffset + 2] << 8) | TURNResponse[AttrOffset + 3];

				if (AttrOffset + 4 + AttrLength > BytesRead) break;

				// ERROR-CODE attribute (0x0009)
				if (AttrType == 0x0009 && AttrLength >= 4)
				{
					// Error code format: 21 bits reserved, 3 bits class, 8 bits number (RFC 5389)
					// Error code is at offset 2-3 within attribute value
					uint8 ClassByte = TURNResponse[AttrOffset + 4 + 2]; // Contains class in lower 3 bits
					uint8 NumberByte = TURNResponse[AttrOffset + 4 + 3];
					ErrorCode = ((ClassByte & STUNConstants::ERROR_CLASS_MASK) * STUNConstants::ERROR_CLASS_MULTIPLIER) + NumberByte;
					
					// Extract error reason phrase if present (starts at byte 4 of attribute value)
					if (AttrLength > 4)
					{
						FString ErrorReason;
						for (int32 i = 4; i < AttrLength; i++)
						{
							ErrorReason += (char)TURNResponse[AttrOffset + 4 + i];
						}
						UE_LOG(LogOnlineICE, Warning, TEXT("TURN Error %d: %s"), ErrorCode, *ErrorReason);
					}
					else
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("TURN Error %d"), ErrorCode);
					}
				}
				// REALM attribute (0x0014)
				else if (AttrType == 0x0014)
				{
					for (int32 i = 0; i < AttrLength; i++)
					{
						ErrorRealm += (char)TURNResponse[AttrOffset + 4 + i];
					}
				}
				// NONCE attribute (0x0015)
				else if (AttrType == 0x0015)
				{
					for (int32 i = 0; i < AttrLength; i++)
					{
						ErrorNonce += (char)TURNResponse[AttrOffset + 4 + i];
					}
				}

				// Move to next attribute (pad to 4-byte boundary)
				AttrOffset += 4 + ((AttrLength + 3) & ~3);
			}

			// If error is 401 Unauthorized and we haven't retried yet, retry with authentication
			if (ErrorCode == 401 && !ErrorRealm.IsEmpty() && !ErrorNonce.IsEmpty() && !bIsRetry)
			{
				UE_LOG(LogOnlineICE, Log, TEXT("TURN requires authentication, retrying with credentials"));
				UE_LOG(LogOnlineICE, Log, TEXT("Realm: %s, Nonce: %s"), *ErrorRealm, *ErrorNonce);
				
				// Retry with authentication (set bIsRetry to true to prevent infinite loops)
				return PerformTURNAllocationRequest(TURNSocket, TURNAddr, Username, Credential, 
					ErrorRealm, ErrorNonce, OutRelayIP, OutRelayPort, true);
			}
			
			UE_LOG(LogOnlineICE, Error, TEXT("TURN Allocate failed - error %d received"), ErrorCode);
			return false;
		}
		else
		{
			UE_LOG(LogOnlineICE, Warning, TEXT("Unexpected TURN response type: 0x%04X"), MessageType);
		}
	}

	UE_LOG(LogOnlineICE, Warning, TEXT("Failed to parse TURN Allocate response"));
	return false;
}

int32 FICEAgent::CalculatePriority(EICECandidateType Type, int32 LocalPreference, int32 ComponentId)
{
	// ICE priority calculation as per RFC 8445
	int32 TypePreference;
	switch (Type)
	{
		case EICECandidateType::Host:
			TypePreference = 126;
			break;
		case EICECandidateType::ServerReflexive:
			TypePreference = 100;
			break;
		case EICECandidateType::Relayed:
			TypePreference = 0;
			break;
		default:
			TypePreference = 0;
			break;
	}

	return (TypePreference << 24) | (LocalPreference << 8) | (256 - ComponentId);
}

TArray<FICECandidate> FICEAgent::GetLocalCandidates() const
{
	return LocalCandidates;
}

void FICEAgent::AddRemoteCandidate(const FICECandidate& Candidate)
{
	UE_LOG(LogOnlineICE, Log, TEXT("Adding remote candidate: %s"), *Candidate.ToString());
	RemoteCandidates.Add(Candidate);
}

void FICEAgent::UpdateConnectionState(EICEConnectionState NewState)
{
	FScopeLock Lock(&ConnectionLock);
	
	if (ConnectionState == NewState)
	{
		return;
	}

	// Logging para diagnóstico
	UE_LOG(LogOnlineICE, Log, TEXT("ICE state change: %s -> %s"),
		*GetConnectionStateName(ConnectionState),
		*GetConnectionStateName(NewState));

	ConnectionState = NewState;

	// Notificar a los delegados
	OnConnectionStateChanged.Broadcast(NewState);

	// Reset timers en cambio de estado
	TimeSinceLastAttempt = 0.0f;
}

bool FICEAgent::StartConnectivityChecks()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Starting ICE connectivity checks - Current state: %s"), *GetConnectionStateName(ConnectionState));

	// Validar estado previo - evitar llamadas cuando ya estamos conectados
	if (ConnectionState == EICEConnectionState::Connected)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Already connected, ignoring StartConnectivityChecks call"));
		return true;
	}

	// Evitar reintentos infinitos - límite total de intentos
	if (TotalConnectionAttempts >= MAX_TOTAL_ATTEMPTS)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Max total connection attempts (%d) reached, giving up"), MAX_TOTAL_ATTEMPTS);
		UpdateConnectionState(EICEConnectionState::Failed);
		return false;
	}

	// Incrementar contador total de intentos
	TotalConnectionAttempts++;

	if (LocalCandidates.Num() == 0 || RemoteCandidates.Num() == 0)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("No candidates available for connectivity checks (Local: %d, Remote: %d)"), 
			LocalCandidates.Num(), RemoteCandidates.Num());
		UpdateConnectionState(EICEConnectionState::Failed);
		return false;
	}

	// Limpiar socket existente si hay uno (evitar sockets huérfanos)
	if (Socket)
	{
		UE_LOG(LogOnlineICE, Log, TEXT("Cleaning up existing socket before creating new connection"));
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}

	// Resetear estado de handshake para nuevo intento
	bHandshakeSent = false;
	bHandshakeReceived = false;
	TimeSinceHandshakeStart = 0.0f;
	TimeSinceLastHandshakeSend = 0.0f;

	// Intentar primero con candidatos directos (host y server reflexive)
	if (DirectConnectionAttempts < MAX_DIRECT_ATTEMPTS)
	{
		UpdateConnectionState(EICEConnectionState::ConnectingDirect);
		DirectConnectionAttempts++;

		// Seleccionar candidatos priorizando host y server reflexive
		TArray<FICECandidate> DirectLocalCandidates;
		TArray<FICECandidate> DirectRemoteCandidates;

		for (const FICECandidate& Cand : LocalCandidates)
		{
			if (Cand.Type == EICECandidateType::Host || Cand.Type == EICECandidateType::ServerReflexive)
			{
				DirectLocalCandidates.Add(Cand);
			}
		}

		for (const FICECandidate& Cand : RemoteCandidates)
		{
			if (Cand.Type == EICECandidateType::Host || Cand.Type == EICECandidateType::ServerReflexive)
			{
				DirectRemoteCandidates.Add(Cand);
			}
		}

		if (DirectLocalCandidates.Num() > 0 && DirectRemoteCandidates.Num() > 0)
		{
			// Seleccionar candidatos de mayor prioridad
			SelectedLocalCandidate = SelectHighestPriorityCandidate(DirectLocalCandidates);
			SelectedRemoteCandidate = SelectHighestPriorityCandidate(DirectRemoteCandidates);

			UE_LOG(LogOnlineICE, Log, TEXT("Attempting direct connection (try %d/%d) - Local: %s (priority: %d), Remote: %s (priority: %d)"),
				DirectConnectionAttempts, MAX_DIRECT_ATTEMPTS,
				*SelectedLocalCandidate.ToString(), SelectedLocalCandidate.Priority,
				*SelectedRemoteCandidate.ToString(), SelectedRemoteCandidate.Priority);
		}
		else
		{
			UE_LOG(LogOnlineICE, Warning, TEXT("No direct candidates available (Local: %d, Remote: %d), falling back to relay"),
				DirectLocalCandidates.Num(), DirectRemoteCandidates.Num());
			UpdateConnectionState(EICEConnectionState::ConnectingRelay);
		}
	}
	else
	{
		// Si los intentos directos fallaron, intentar con candidatos relay
		UpdateConnectionState(EICEConnectionState::ConnectingRelay);
		UE_LOG(LogOnlineICE, Log, TEXT("Direct connection attempts failed, trying relay candidates"));

		// Seleccionar candidatos relay
		TArray<FICECandidate> RelayLocalCandidates;
		TArray<FICECandidate> RelayRemoteCandidates;

		for (const FICECandidate& Cand : LocalCandidates)
		{
			if (Cand.Type == EICECandidateType::Relayed)
			{
				RelayLocalCandidates.Add(Cand);
			}
		}

		for (const FICECandidate& Cand : RemoteCandidates)
		{
			if (Cand.Type == EICECandidateType::Relayed)
			{
				RelayRemoteCandidates.Add(Cand);
			}
		}

		if (RelayLocalCandidates.Num() > 0 && RelayRemoteCandidates.Num() > 0)
		{
			// Seleccionar candidatos relay de mayor prioridad
			SelectedLocalCandidate = SelectHighestPriorityCandidate(RelayLocalCandidates);
			SelectedRemoteCandidate = SelectHighestPriorityCandidate(RelayRemoteCandidates);

			UE_LOG(LogOnlineICE, Log, TEXT("Selected relay candidates - Local: %s (priority: %d), Remote: %s (priority: %d)"),
				*SelectedLocalCandidate.ToString(), SelectedLocalCandidate.Priority,
				*SelectedRemoteCandidate.ToString(), SelectedRemoteCandidate.Priority);
		}
		else
		{
			UE_LOG(LogOnlineICE, Error, TEXT("No relay candidates available after direct connection failed (Local relay: %d, Remote relay: %d)"),
				RelayLocalCandidates.Num(), RelayRemoteCandidates.Num());
			UpdateConnectionState(EICEConnectionState::Failed);
			return false;
		}
	}

	// Create socket for communication
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to get socket subsystem"));
		UpdateConnectionState(EICEConnectionState::Failed);
		return false;
	}

	TSharedPtr<FInternetAddr> RemoteAddr = SocketSubsystem->GetAddressFromString(SelectedRemoteCandidate.Address);
	if (!RemoteAddr.IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to parse remote address: %s"), *SelectedRemoteCandidate.Address);
		UpdateConnectionState(EICEConnectionState::Failed);
		return false;
	}
	RemoteAddr->SetPort(SelectedRemoteCandidate.Port);

	// Parse local candidate address for binding
	TSharedPtr<FInternetAddr> LocalAddr = SocketSubsystem->GetAddressFromString(SelectedLocalCandidate.Address);
	if (!LocalAddr.IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to parse local address: %s"), *SelectedLocalCandidate.Address);
		UpdateConnectionState(EICEConnectionState::Failed);
		return false;
	}
	
	// For server reflexive and relay candidates, we need to bind to the local interface (0.0.0.0)
	// and let the OS assign a port, as we can't bind directly to the public/relay address
	if (SelectedLocalCandidate.Type == EICECandidateType::ServerReflexive || 
	    SelectedLocalCandidate.Type == EICECandidateType::Relayed)
	{
		// Bind to any address (0.0.0.0) and let OS assign port
		LocalAddr->SetAnyAddress();
		LocalAddr->SetPort(0); // OS will assign an ephemeral port
	}
	else
	{
		// For host candidates, we can bind to the specific local address
		LocalAddr->SetPort(SelectedLocalCandidate.Port);
	}

	Socket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("ICE"), RemoteAddr->GetProtocolType());
	if (!Socket)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to create ICE socket"));
		UpdateConnectionState(EICEConnectionState::Failed);
		return false;
	}

	// Bind socket to local address
	if (!Socket->Bind(*LocalAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to bind socket to local address: %s:%d"), 
			*LocalAddr->ToString(false), LocalAddr->GetPort());
		CleanupSocketOnError();
		return false;
	}

	// Set socket to non-blocking mode for async operations
	Socket->SetNonBlocking(true);
	
	// Enable address reuse to allow multiple ICE agents or reconnections on the same port
	// This is necessary for ICE as we may need to quickly rebind after connection failures
	Socket->SetReuseAddr(true);
	
	// Disable receive error notifications to prevent socket from becoming invalid on ICMP errors
	Socket->SetRecvErr(false);

	// Get the actual bound port
	TSharedRef<FInternetAddr> BoundAddr = SocketSubsystem->CreateInternetAddr();
	Socket->GetAddress(*BoundAddr);
	if (BoundAddr->IsValid())
	{
		int32 ActualPort = BoundAddr->GetPort();
		UE_LOG(LogOnlineICE, Log, TEXT("Socket bound to %s:%d"), 
			*BoundAddr->ToString(false), ActualPort);
		
		// Update local candidate port if it was 0 (OS assigned)
		if (SelectedLocalCandidate.Port == 0)
		{
			SelectedLocalCandidate.Port = ActualPort;
			
			// Also update the port in the LocalCandidates array for future use
			for (FICECandidate& Candidate : LocalCandidates)
			{
				if (Candidate.Address == SelectedLocalCandidate.Address && 
				    Candidate.Type == SelectedLocalCandidate.Type &&
				    Candidate.Port == 0)
				{
					Candidate.Port = ActualPort;
					UE_LOG(LogOnlineICE, Log, TEXT("Updated local candidate port to %d in candidates list"), ActualPort);
					break;
				}
			}
			
			UE_LOG(LogOnlineICE, Log, TEXT("Updated selected local candidate port to %d"), ActualPort);
		}
	}

	// If using TURN relay, create permission and bind channel
	if (SelectedLocalCandidate.Type == EICECandidateType::Relayed)
	{
		// Validar que la asignación TURN esté activa y el socket exista
		if (!bTURNAllocationActive)
		{
			UE_LOG(LogOnlineICE, Error, TEXT("Selected relay candidate but TURN allocation is not active"));
			CleanupSocketOnError();
			return false;
		}

		if (!TURNSocket)
		{
			UE_LOG(LogOnlineICE, Error, TEXT("Selected relay candidate but TURN socket is not available"));
			CleanupSocketOnError();
			return false;
		}

		UE_LOG(LogOnlineICE, Log, TEXT("Setting up TURN relay for communication"));
		
		// Create permission for remote peer
		if (PerformTURNCreatePermission(SelectedRemoteCandidate.Address, SelectedRemoteCandidate.Port))
		{
			UE_LOG(LogOnlineICE, Log, TEXT("TURN permission created for peer"));
			
			// Bind channel for efficient data transfer
			if (PerformTURNChannelBind(SelectedRemoteCandidate.Address, SelectedRemoteCandidate.Port, TURNChannelNumber))
			{
				UE_LOG(LogOnlineICE, Log, TEXT("TURN channel bound successfully"));
			}
			else
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("TURN channel binding failed, will use Send indication"));
			}
		}
		else
		{
			UE_LOG(LogOnlineICE, Warning, TEXT("TURN permission creation failed"));
		}
	}

	// Start handshake to verify bidirectional connection
	UE_LOG(LogOnlineICE, Log, TEXT("Socket created, starting handshake to verify connection"));
	UpdateConnectionState(EICEConnectionState::PerformingHandshake);
	
	// Send initial handshake packet
	if (!SendHandshake())
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Failed to send initial handshake packet"));
	}

	return true;
}

bool FICEAgent::IsConnected() const
{
	return bIsConnected;
}

bool FICEAgent::SendData(const uint8* Data, int32 Size)
{
	if (!bIsConnected)
	{
		return false;
	}

	// Use TURN relay if the local candidate is relayed
	if (SelectedLocalCandidate.Type == EICECandidateType::Relayed && bTURNAllocationActive)
	{
		return SendDataThroughTURN(Data, Size, SelectedRemoteCandidate.Address, SelectedRemoteCandidate.Port);
	}

	// Otherwise use direct connection
	if (!Socket)
	{
		return false;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		return false;
	}

	TSharedPtr<FInternetAddr> RemoteAddr = SocketSubsystem->GetAddressFromString(SelectedRemoteCandidate.Address);
	if (!RemoteAddr.IsValid())
	{
		return false;
	}
	RemoteAddr->SetPort(SelectedRemoteCandidate.Port);

	int32 BytesSent;
	return Socket->SendTo(Data, Size, BytesSent, *RemoteAddr) && BytesSent == Size;
}

bool FICEAgent::ReceiveData(uint8* Data, int32 MaxSize, int32& OutSize)
{
	if (!bIsConnected)
	{
		OutSize = 0;
		return false;
	}

	// Use TURN relay if the local candidate is relayed
	if (SelectedLocalCandidate.Type == EICECandidateType::Relayed && bTURNAllocationActive)
	{
		return ReceiveDataFromTURN(Data, MaxSize, OutSize);
	}

	// Otherwise use direct connection
	if (!Socket)
	{
		OutSize = 0;
		return false;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		OutSize = 0;
		return false;
	}

	TSharedRef<FInternetAddr> FromAddr = SocketSubsystem->CreateInternetAddr();
	return Socket->RecvFrom(Data, MaxSize, OutSize, *FromAddr);
}

void FICEAgent::Tick(float DeltaTime)
{
	TimeSinceLastAttempt += DeltaTime;

	// Handle TURN allocation refresh if active
	if (bTURNAllocationActive)
	{
		TimeSinceTURNRefresh += DeltaTime;
		
		// Refresh TURN allocation before it expires (refresh at 80% of lifetime)
		float RefreshInterval = TURNAllocationLifetime * 0.8f;
		if (TimeSinceTURNRefresh >= RefreshInterval)
		{
			UE_LOG(LogOnlineICE, Log, TEXT("TURN allocation needs refresh (%.1f seconds elapsed, lifetime: %d)"),
				TimeSinceTURNRefresh, TURNAllocationLifetime);
			
			if (!PerformTURNRefresh())
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("TURN refresh failed, allocation may expire"));
				// Try to refresh again sooner
				TimeSinceTURNRefresh = RefreshInterval - 30.0f; // Retry in 30 seconds
			}
		}
	}

	switch (ConnectionState)
	{
		case EICEConnectionState::ConnectingDirect:
		{
			// Si el tiempo de espera se cumplió y no estamos conectados, reintentar
			if (TimeSinceLastAttempt >= RetryDelay && !bIsConnected)
			{
				UE_LOG(LogOnlineICE, Log, TEXT("Direct connection attempt timed out, retrying..."));
				StartConnectivityChecks();
			}
			break;
		}
		case EICEConnectionState::ConnectingRelay:
		{
			// Si el tiempo de espera se cumplió y no estamos conectados, marcar como fallido
			if (TimeSinceLastAttempt >= RetryDelay && !bIsConnected)
			{
				UE_LOG(LogOnlineICE, Error, TEXT("Relay connection attempt timed out"));
				UpdateConnectionState(EICEConnectionState::Failed);
			}
			break;
		}
		case EICEConnectionState::PerformingHandshake:
		{
			// Process received data for handshake
			ProcessReceivedData();
			
			TimeSinceHandshakeStart += DeltaTime;
			TimeSinceLastHandshakeSend += DeltaTime;
			
			// Check handshake timeout
			if (TimeSinceHandshakeStart >= HandshakeTimeout)
			{
				// Use handshake flags instead of bIsConnected to avoid race conditions
				// Fail if either part of handshake is incomplete: !sent OR !received == !(sent AND received)
				if (!bHandshakeSent || !bHandshakeReceived)
				{
					UE_LOG(LogOnlineICE, Error, TEXT("Handshake timeout - no response from peer"));
					UpdateConnectionState(EICEConnectionState::Failed);
				}
			}
			// Retry handshake send every second if we haven't received a response
			else if (ShouldRetryHandshake())
			{
				UE_LOG(LogOnlineICE, Log, TEXT("Retrying handshake (%.1f seconds elapsed)"), TimeSinceHandshakeStart);
				SendHandshake();
				TimeSinceLastHandshakeSend = 0.0f; // Reset retry timer
			}
			break;
		}
		case EICEConnectionState::Connected:
		{
			// Process received data in connected state (for possible future keepalives)
			ProcessReceivedData();
			break;
		}
		default:
			break;
	}
}

bool FICEAgent::SendHandshake()
{
	if (!Socket)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Cannot send handshake: socket is null"));
		return false;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Cannot send handshake: socket subsystem unavailable"));
		return false;
	}

	// Create simple handshake packet
	// Format: [Magic Number (4 bytes)] [Type (1 byte)] [Timestamp (4 bytes)]
	uint8 HandshakePacket[HandshakeConstants::HANDSHAKE_PACKET_SIZE];
	
	// Magic number "ICEH"
	for (int32 i = 0; i < 4; i++)
	{
		HandshakePacket[i] = HandshakeConstants::MAGIC_NUMBER[i];
	}
	
	// Type: HELLO request or HELLO response
	HandshakePacket[4] = bHandshakeReceived ? 
		HandshakeConstants::PACKET_TYPE_HELLO_RESPONSE : 
		HandshakeConstants::PACKET_TYPE_HELLO_REQUEST;
	
	// Timestamp (simple counter for correlation)
	uint32 Timestamp = FPlatformTime::Cycles();
	HandshakePacket[5] = (Timestamp >> 24) & 0xFF;
	HandshakePacket[6] = (Timestamp >> 16) & 0xFF;
	HandshakePacket[7] = (Timestamp >> 8) & 0xFF;
	HandshakePacket[8] = Timestamp & 0xFF;

	TSharedPtr<FInternetAddr> RemoteAddr = SocketSubsystem->GetAddressFromString(SelectedRemoteCandidate.Address);
	if (!RemoteAddr.IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Cannot send handshake: invalid remote address"));
		return false;
	}
	RemoteAddr->SetPort(SelectedRemoteCandidate.Port);

	int32 BytesSent = 0;
	bool bSuccess = Socket->SendTo(HandshakePacket, sizeof(HandshakePacket), BytesSent, *RemoteAddr);
	
	if (bSuccess && BytesSent == sizeof(HandshakePacket))
	{
		if (!bHandshakeReceived)
		{
			// Only initialize timer on first send, not on retries
			if (!bHandshakeSent)
			{
				bHandshakeSent = true;
				TimeSinceHandshakeStart = 0.0f;
				TimeSinceLastHandshakeSend = 0.0f;
			}
			UE_LOG(LogOnlineICE, Log, TEXT("Handshake HELLO request sent to %s:%d"), 
				*SelectedRemoteCandidate.Address, SelectedRemoteCandidate.Port);
		}
		else
		{
			UE_LOG(LogOnlineICE, Log, TEXT("Handshake HELLO response sent to %s:%d"), 
				*SelectedRemoteCandidate.Address, SelectedRemoteCandidate.Port);
		}
		return true;
	}
	else
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Failed to send handshake packet: sent %d of %d bytes"), 
			BytesSent, (int32)sizeof(HandshakePacket));
		return false;
	}
}

bool FICEAgent::ProcessReceivedData()
{
	if (!Socket)
	{
		return false;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		return false;
	}

	// Check if data is available
	uint32 PendingDataSize = 0;
	if (!Socket->HasPendingData(PendingDataSize) || PendingDataSize == 0)
	{
		return false;
	}

	// Buffer to receive data
	uint8 ReceiveBuffer[HandshakeConstants::MAX_RECEIVE_BUFFER_SIZE];
	int32 BytesRead = 0;
	TSharedRef<FInternetAddr> FromAddr = SocketSubsystem->CreateInternetAddr();

	if (!Socket->RecvFrom(ReceiveBuffer, sizeof(ReceiveBuffer), BytesRead, *FromAddr))
	{
		return false;
	}

	if (BytesRead < HandshakeConstants::HANDSHAKE_PACKET_SIZE)
	{
		// Packet too small to be a valid handshake
		return false;
	}

	// Verify magic number
	bool bIsMagicNumberValid = true;
	for (int32 i = 0; i < 4; i++)
	{
		if (ReceiveBuffer[i] != HandshakeConstants::MAGIC_NUMBER[i])
		{
			bIsMagicNumberValid = false;
			break;
		}
	}

	if (bIsMagicNumberValid)
	{
		uint8 PacketType = ReceiveBuffer[4];
		
		if (PacketType == HandshakeConstants::PACKET_TYPE_HELLO_REQUEST) // HELLO request
		{
			UE_LOG(LogOnlineICE, Log, TEXT("Received handshake HELLO request from %s"), 
				*FromAddr->ToString(true));
			
			bHandshakeReceived = true;
			
			// Respond with HELLO response
			SendHandshake();
			
			// Check if handshake is complete
			CompleteHandshake();
			
			return true;
		}
		else if (PacketType == HandshakeConstants::PACKET_TYPE_HELLO_RESPONSE) // HELLO response
		{
			UE_LOG(LogOnlineICE, Log, TEXT("Received handshake HELLO response from %s"), 
				*FromAddr->ToString(true));
			
			bHandshakeReceived = true;
			
			// Check if handshake is complete
			CompleteHandshake();
			
			return true;
		}
	}

	return false;
}

void FICEAgent::Close()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	
	if (Socket)
	{
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}

	// Clean up TURN socket and resources
	if (TURNSocket)
	{
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(TURNSocket);
		}
		TURNSocket = nullptr;
	}

	{
		FScopeLock Lock(&ConnectionLock);
		ConnectionState = EICEConnectionState::New;
	}

	bIsConnected = false;
	bHandshakeSent = false;
	bHandshakeReceived = false;
	bTURNAllocationActive = false;
	DirectConnectionAttempts = 0;
	TotalConnectionAttempts = 0;
	TimeSinceLastAttempt = 0.0f;
	TimeSinceHandshakeStart = 0.0f;
	TimeSinceLastHandshakeSend = 0.0f;
	TimeSinceTURNRefresh = 0.0f;
	TURNServerAddr.Reset();
	TURNRelayAddr.Reset();
	LocalCandidates.Empty();
	RemoteCandidates.Empty();
}

void FICEAgent::CalculateMD5(const FString& Input, uint8* OutHash)
{
	// Calculate MD5 hash for TURN authentication using Unreal's built-in FMD5
	// This is used to create the key for HMAC-SHA1: MD5(username:realm:password)
	
	// Convert FString to UTF8
	FTCHARToUTF8 UTF8String(*Input);
	
	// Calculate MD5 using Unreal's secure hash implementation
	FMD5 MD5Context;
	MD5Context.Update((const uint8*)UTF8String.Get(), UTF8String.Length());
	MD5Context.Final(OutHash);
}

bool FICEAgent::ShouldRetryHandshake() const
{
	return TimeSinceLastHandshakeSend >= HANDSHAKE_RETRY_INTERVAL && 
	       bHandshakeSent && 
	       !bHandshakeReceived;
}

void FICEAgent::CompleteHandshake()
{
	if (bHandshakeSent && bHandshakeReceived)
	{
		bIsConnected = true;
		UpdateConnectionState(EICEConnectionState::Connected);
		UE_LOG(LogOnlineICE, Log, TEXT("ICE connection fully established - handshake complete"));
	}
}

void FICEAgent::CleanupSocketOnError()
{
	if (Socket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	UpdateConnectionState(EICEConnectionState::Failed);
}

FICECandidate FICEAgent::SelectHighestPriorityCandidate(const TArray<FICECandidate>& Candidates) const
{
	if (Candidates.Num() == 0)
	{
		return FICECandidate();
	}

	const FICECandidate* BestCandidate = &Candidates[0];
	for (int32 i = 1; i < Candidates.Num(); ++i)
	{
		if (Candidates[i].Priority > BestCandidate->Priority)
		{
			BestCandidate = &Candidates[i];
		}
	}
	
	return *BestCandidate;
}

FString FICEAgent::GetConnectionStateName(EICEConnectionState State) const
{
	switch (State)
	{
		case EICEConnectionState::New:
			return TEXT("New");
		case EICEConnectionState::Gathering:
			return TEXT("Gathering");
		case EICEConnectionState::ConnectingDirect:
			return TEXT("ConnectingDirect");
		case EICEConnectionState::ConnectingRelay:
			return TEXT("ConnectingRelay");
		case EICEConnectionState::PerformingHandshake:
			return TEXT("PerformingHandshake");
		case EICEConnectionState::Connected:
			return TEXT("Connected");
		case EICEConnectionState::Failed:
			return TEXT("Failed");
		default:
			return TEXT("Unknown");
	}
}

void FICEAgent::CalculateHMACSHA1(const uint8* Data, int32 DataLen, const uint8* Key, int32 KeyLen, uint8* OutHash)
{
	// HMAC-SHA1 implementation as per RFC 2104
	const int32 BlockSize = STUNConstants::SHA1_BLOCK_SIZE;
	const int32 HashSize = STUNConstants::HMAC_SHA1_SIZE;
	
	uint8 KeyPadded[BlockSize];
	FMemory::Memzero(KeyPadded, BlockSize);
	
	// If key is longer than block size, hash it first
	if (KeyLen > BlockSize)
	{
		FSHA1 SHA1Context;
		SHA1Context.Update(Key, KeyLen);
		SHA1Context.Final();
		SHA1Context.GetHash(KeyPadded);
	}
	else
	{
		FMemory::Memcpy(KeyPadded, Key, KeyLen);
	}
	
	// Create inner and outer padded keys
	uint8 InnerPad[BlockSize];
	uint8 OuterPad[BlockSize];
	
	for (int32 i = 0; i < BlockSize; i++)
	{
		InnerPad[i] = KeyPadded[i] ^ 0x36;
		OuterPad[i] = KeyPadded[i] ^ 0x5C;
	}
	
	// Calculate inner hash: SHA1(InnerPad || Data)
	FSHA1 InnerSHA1;
	InnerSHA1.Update(InnerPad, BlockSize);
	InnerSHA1.Update(Data, DataLen);
	InnerSHA1.Final();
	
	uint8 InnerHash[HashSize];
	InnerSHA1.GetHash(InnerHash);
	
	// Calculate outer hash: SHA1(OuterPad || InnerHash)
	FSHA1 OuterSHA1;
	OuterSHA1.Update(OuterPad, BlockSize);
	OuterSHA1.Update(InnerHash, HashSize);
	OuterSHA1.Final();
	
	OuterSHA1.GetHash(OutHash);
}

bool FICEAgent::PerformTURNCreatePermission(const FString& PeerAddress, int32 PeerPort)
{
	if (!TURNSocket || !bTURNAllocationActive || !TURNServerAddr.IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Cannot create TURN permission: TURN not allocated"));
		return false;
	}

	UE_LOG(LogOnlineICE, Log, TEXT("Creating TURN permission for peer %s:%d"), *PeerAddress, PeerPort);

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		return false;
	}

	// Parse peer address
	TSharedPtr<FInternetAddr> PeerAddr = SocketSubsystem->GetAddressFromString(PeerAddress);
	if (!PeerAddr.IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Invalid peer address: %s"), *PeerAddress);
		return false;
	}
	PeerAddr->SetPort(PeerPort);

	// Build TURN CreatePermission Request (RFC 5766 Section 9)
	TArray<uint8> Request;
	Request.SetNum(512);
	FMemory::Memzero(Request.GetData(), Request.Num());

	int32 Offset = 0;

	// Message Type: CreatePermission Request (0x0008)
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x08;

	// Message Length: Will be set later
	int32 LengthOffset = Offset;
	Offset += 2;

	// Magic Cookie
	Request[Offset++] = 0x21;
	Request[Offset++] = 0x12;
	Request[Offset++] = 0xA4;
	Request[Offset++] = 0x42;

	// Transaction ID: Copy from last allocation or generate new
	for (int32 i = 0; i < STUNConstants::TRANSACTION_ID_LENGTH; i++)
	{
		TURNTransactionID[i] = FMath::Rand() & 0xFF;
		Request[Offset++] = TURNTransactionID[i];
	}

	// Add XOR-PEER-ADDRESS attribute (0x0012)
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x12;
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x08; // Length = 8 for IPv4

	// Reserved + Family
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x01; // IPv4

	// XOR-ed Port
	uint16 XorPort = PeerPort ^ STUNConstants::MAGIC_COOKIE_HIGH;
	Request[Offset++] = (XorPort >> 8) & 0xFF;
	Request[Offset++] = XorPort & 0xFF;

	// XOR-ed IP address
	uint32 PeerIPValue = 0;
	// Parse IP address from string
	TArray<FString> IPParts;
	PeerAddress.ParseIntoArray(IPParts, TEXT("."));
	if (IPParts.Num() == 4)
	{
		PeerIPValue = (FCString::Atoi(*IPParts[0]) << 24) |
		              (FCString::Atoi(*IPParts[1]) << 16) |
		              (FCString::Atoi(*IPParts[2]) << 8) |
		              FCString::Atoi(*IPParts[3]);
	}
	uint32 XorIP = PeerIPValue ^ STUNConstants::MAGIC_COOKIE;
	Request[Offset++] = (XorIP >> 24) & 0xFF;
	Request[Offset++] = (XorIP >> 16) & 0xFF;
	Request[Offset++] = (XorIP >> 8) & 0xFF;
	Request[Offset++] = XorIP & 0xFF;

	// Add USERNAME attribute (required for authenticated requests)
	int32 UsernameLen = Config.TURNUsername.Len();
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x06;
	Request[Offset++] = (UsernameLen >> 8) & 0xFF;
	Request[Offset++] = UsernameLen & 0xFF;
	for (int32 i = 0; i < UsernameLen; i++)
	{
		Request[Offset++] = Config.TURNUsername[i];
	}
	while (Offset % 4 != 0)
	{
		Request[Offset++] = 0x00;
	}

	// Set message length
	int32 MessageLength = Offset - 20;
	Request[LengthOffset] = (MessageLength >> 8) & 0xFF;
	Request[LengthOffset + 1] = MessageLength & 0xFF;

	// Resize to actual size
	Request.SetNum(Offset);

	// Send request
	int32 BytesSent;
	if (!TURNSocket->SendTo(Request.GetData(), Request.Num(), BytesSent, *TURNServerAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to send TURN CreatePermission request"));
		return false;
	}

	// Wait for response
	if (!TURNSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0)))
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("TURN CreatePermission timeout"));
		return false;
	}

	// Receive response
	uint8 Response[1024];
	int32 BytesRead = 0;
	TSharedRef<FInternetAddr> FromAddr = SocketSubsystem->CreateInternetAddr();

	if (!TURNSocket->RecvFrom(Response, sizeof(Response), BytesRead, *FromAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to receive TURN CreatePermission response"));
		return false;
	}

	// Parse response
	if (BytesRead >= 20)
	{
		uint16 MessageType = (Response[0] << 8) | Response[1];
		
		if (MessageType == 0x0108) // CreatePermission Success Response
		{
			UE_LOG(LogOnlineICE, Log, TEXT("TURN permission created successfully"));
			return true;
		}
		else if (MessageType == 0x0118) // CreatePermission Error Response
		{
			UE_LOG(LogOnlineICE, Error, TEXT("TURN CreatePermission failed"));
			return false;
		}
	}

	return false;
}

bool FICEAgent::PerformTURNChannelBind(const FString& PeerAddress, int32 PeerPort, uint16 ChannelNumber)
{
	if (!TURNSocket || !bTURNAllocationActive || !TURNServerAddr.IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Cannot bind TURN channel: TURN not allocated"));
		return false;
	}

	UE_LOG(LogOnlineICE, Log, TEXT("Binding TURN channel 0x%04X for peer %s:%d"), 
		ChannelNumber, *PeerAddress, PeerPort);

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		return false;
	}

	// Build TURN ChannelBind Request (RFC 5766 Section 11)
	TArray<uint8> Request;
	Request.SetNum(512);
	FMemory::Memzero(Request.GetData(), Request.Num());

	int32 Offset = 0;

	// Message Type: ChannelBind Request (0x0009)
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x09;

	// Message Length: Will be set later
	int32 LengthOffset = Offset;
	Offset += 2;

	// Magic Cookie
	Request[Offset++] = 0x21;
	Request[Offset++] = 0x12;
	Request[Offset++] = 0xA4;
	Request[Offset++] = 0x42;

	// Transaction ID
	for (int32 i = 0; i < STUNConstants::TRANSACTION_ID_LENGTH; i++)
	{
		TURNTransactionID[i] = FMath::Rand() & 0xFF;
		Request[Offset++] = TURNTransactionID[i];
	}

	// Add CHANNEL-NUMBER attribute (0x000C)
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x0C;
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x04; // Length = 4

	Request[Offset++] = (ChannelNumber >> 8) & 0xFF;
	Request[Offset++] = ChannelNumber & 0xFF;
	Request[Offset++] = 0x00; // Reserved
	Request[Offset++] = 0x00; // Reserved

	// Add XOR-PEER-ADDRESS attribute (0x0012)
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x12;
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x08; // Length = 8 for IPv4

	Request[Offset++] = 0x00;
	Request[Offset++] = 0x01; // IPv4

	uint16 XorPort = PeerPort ^ STUNConstants::MAGIC_COOKIE_HIGH;
	Request[Offset++] = (XorPort >> 8) & 0xFF;
	Request[Offset++] = XorPort & 0xFF;

	// Parse and XOR IP address
	TArray<FString> IPParts;
	PeerAddress.ParseIntoArray(IPParts, TEXT("."));
	uint32 PeerIPValue = 0;
	if (IPParts.Num() == 4)
	{
		PeerIPValue = (FCString::Atoi(*IPParts[0]) << 24) |
		              (FCString::Atoi(*IPParts[1]) << 16) |
		              (FCString::Atoi(*IPParts[2]) << 8) |
		              FCString::Atoi(*IPParts[3]);
	}
	uint32 XorIP = PeerIPValue ^ STUNConstants::MAGIC_COOKIE;
	Request[Offset++] = (XorIP >> 24) & 0xFF;
	Request[Offset++] = (XorIP >> 16) & 0xFF;
	Request[Offset++] = (XorIP >> 8) & 0xFF;
	Request[Offset++] = XorIP & 0xFF;

	// Add USERNAME attribute
	int32 UsernameLen = Config.TURNUsername.Len();
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x06;
	Request[Offset++] = (UsernameLen >> 8) & 0xFF;
	Request[Offset++] = UsernameLen & 0xFF;
	for (int32 i = 0; i < UsernameLen; i++)
	{
		Request[Offset++] = Config.TURNUsername[i];
	}
	while (Offset % 4 != 0)
	{
		Request[Offset++] = 0x00;
	}

	// Set message length
	int32 MessageLength = Offset - 20;
	Request[LengthOffset] = (MessageLength >> 8) & 0xFF;
	Request[LengthOffset + 1] = MessageLength & 0xFF;

	Request.SetNum(Offset);

	// Send request
	int32 BytesSent;
	if (!TURNSocket->SendTo(Request.GetData(), Request.Num(), BytesSent, *TURNServerAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to send TURN ChannelBind request"));
		return false;
	}

	// Wait for response
	if (!TURNSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0)))
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("TURN ChannelBind timeout"));
		return false;
	}

	// Receive response
	uint8 Response[1024];
	int32 BytesRead = 0;
	TSharedRef<FInternetAddr> FromAddr = SocketSubsystem->CreateInternetAddr();

	if (!TURNSocket->RecvFrom(Response, sizeof(Response), BytesRead, *FromAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to receive TURN ChannelBind response"));
		return false;
	}

	// Parse response
	if (BytesRead >= 20)
	{
		uint16 MessageType = (Response[0] << 8) | Response[1];
		
		if (MessageType == 0x0109) // ChannelBind Success Response
		{
			UE_LOG(LogOnlineICE, Log, TEXT("TURN channel 0x%04X bound successfully"), ChannelNumber);
			TURNChannelNumber = ChannelNumber;
			return true;
		}
		else if (MessageType == 0x0119) // ChannelBind Error Response
		{
			UE_LOG(LogOnlineICE, Error, TEXT("TURN ChannelBind failed"));
			return false;
		}
	}

	return false;
}

bool FICEAgent::PerformTURNRefresh()
{
	if (!TURNSocket || !bTURNAllocationActive || !TURNServerAddr.IsValid())
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot refresh TURN: allocation not active"));
		return false;
	}

	UE_LOG(LogOnlineICE, Log, TEXT("Refreshing TURN allocation"));

	// Build TURN Refresh Request (RFC 5766 Section 7)
	TArray<uint8> Request;
	Request.SetNum(512);
	FMemory::Memzero(Request.GetData(), Request.Num());

	int32 Offset = 0;

	// Message Type: Refresh Request (0x0004)
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x04;

	// Message Length: Will be set later
	int32 LengthOffset = Offset;
	Offset += 2;

	// Magic Cookie
	Request[Offset++] = 0x21;
	Request[Offset++] = 0x12;
	Request[Offset++] = 0xA4;
	Request[Offset++] = 0x42;

	// Transaction ID
	for (int32 i = 0; i < STUNConstants::TRANSACTION_ID_LENGTH; i++)
	{
		TURNTransactionID[i] = FMath::Rand() & 0xFF;
		Request[Offset++] = TURNTransactionID[i];
	}

	// Add LIFETIME attribute (0x000D) - request same lifetime
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x0D;
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x04; // Length = 4

	Request[Offset++] = (TURNAllocationLifetime >> 24) & 0xFF;
	Request[Offset++] = (TURNAllocationLifetime >> 16) & 0xFF;
	Request[Offset++] = (TURNAllocationLifetime >> 8) & 0xFF;
	Request[Offset++] = TURNAllocationLifetime & 0xFF;

	// Add USERNAME attribute
	int32 UsernameLen = Config.TURNUsername.Len();
	Request[Offset++] = 0x00;
	Request[Offset++] = 0x06;
	Request[Offset++] = (UsernameLen >> 8) & 0xFF;
	Request[Offset++] = UsernameLen & 0xFF;
	for (int32 i = 0; i < UsernameLen; i++)
	{
		Request[Offset++] = Config.TURNUsername[i];
	}
	while (Offset % 4 != 0)
	{
		Request[Offset++] = 0x00;
	}

	// Set message length
	int32 MessageLength = Offset - 20;
	Request[LengthOffset] = (MessageLength >> 8) & 0xFF;
	Request[LengthOffset + 1] = MessageLength & 0xFF;

	Request.SetNum(Offset);

	// Send request
	int32 BytesSent;
	if (!TURNSocket->SendTo(Request.GetData(), Request.Num(), BytesSent, *TURNServerAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to send TURN Refresh request"));
		return false;
	}

	// Wait for response
	if (!TURNSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0)))
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("TURN Refresh timeout"));
		return false;
	}

	// Receive response
	uint8 Response[1024];
	int32 BytesRead = 0;
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		return false;
	}
	TSharedRef<FInternetAddr> FromAddr = SocketSubsystem->CreateInternetAddr();

	if (!TURNSocket->RecvFrom(Response, sizeof(Response), BytesRead, *FromAddr))
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to receive TURN Refresh response"));
		return false;
	}

	// Parse response
	if (BytesRead >= 20)
	{
		uint16 MessageType = (Response[0] << 8) | Response[1];
		
		if (MessageType == 0x0104) // Refresh Success Response
		{
			UE_LOG(LogOnlineICE, Log, TEXT("TURN allocation refreshed successfully"));
			TimeSinceTURNRefresh = 0.0f;
			
			// Update lifetime from response if present
			int32 AttrOffset = 20;
			while (AttrOffset < BytesRead)
			{
				if (AttrOffset + 4 > BytesRead) break;
				
				uint16 AttrType = (Response[AttrOffset] << 8) | Response[AttrOffset + 1];
				uint16 AttrLength = (Response[AttrOffset + 2] << 8) | Response[AttrOffset + 3];
				
				if (AttrType == 0x000D && AttrLength == 4) // LIFETIME
				{
					if (AttrOffset + 8 <= BytesRead)
					{
						TURNAllocationLifetime = (Response[AttrOffset + 4] << 24) |
						                         (Response[AttrOffset + 5] << 16) |
						                         (Response[AttrOffset + 6] << 8) |
						                         Response[AttrOffset + 7];
						UE_LOG(LogOnlineICE, Log, TEXT("Updated TURN allocation lifetime: %d seconds"), 
							TURNAllocationLifetime);
					}
					break;
				}
				
				AttrOffset += 4 + ((AttrLength + 3) & ~3);
			}
			
			return true;
		}
		else if (MessageType == 0x0114) // Refresh Error Response
		{
			UE_LOG(LogOnlineICE, Error, TEXT("TURN Refresh failed"));
			bTURNAllocationActive = false;
			return false;
		}
	}

	return false;
}

bool FICEAgent::SendDataThroughTURN(const uint8* Data, int32 Size, const FString& PeerAddress, int32 PeerPort)
{
	if (!TURNSocket || !bTURNAllocationActive || !TURNServerAddr.IsValid())
	{
		return false;
	}

	// Use ChannelData if channel is bound (more efficient)
	if (TURNChannelNumber >= STUNConstants::CHANNEL_NUMBER_MIN && TURNChannelNumber <= STUNConstants::CHANNEL_NUMBER_MAX)
	{
		// ChannelData format: Channel Number (2) | Length (2) | Application Data (variable)
		TArray<uint8> ChannelData;
		ChannelData.SetNum(4 + Size);
		
		// Channel number
		ChannelData[0] = (TURNChannelNumber >> 8) & 0xFF;
		ChannelData[1] = TURNChannelNumber & 0xFF;
		
		// Length
		ChannelData[2] = (Size >> 8) & 0xFF;
		ChannelData[3] = Size & 0xFF;
		
		// Application data
		FMemory::Memcpy(&ChannelData[4], Data, Size);
		
		int32 BytesSent;
		return TURNSocket->SendTo(ChannelData.GetData(), ChannelData.Num(), BytesSent, *TURNServerAddr);
	}
	else
	{
		// Use Send indication (RFC 5766 Section 10.1)
		// This is less efficient but works without ChannelBind
		UE_LOG(LogOnlineICE, Verbose, TEXT("Sending data through TURN using Send indication (channel not bound)"));
		
		// For now, we'll skip Send indication implementation and recommend using ChannelBind
		// In production, you should implement Send indication here
		return false;
	}
}

bool FICEAgent::ReceiveDataFromTURN(uint8* Data, int32 MaxSize, int32& OutSize)
{
	if (!TURNSocket)
	{
		OutSize = 0;
		return false;
	}

	uint8 ReceiveBuffer[2048];
	int32 BytesRead = 0;
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		OutSize = 0;
		return false;
	}
	TSharedRef<FInternetAddr> FromAddr = SocketSubsystem->CreateInternetAddr();

	if (!TURNSocket->RecvFrom(ReceiveBuffer, sizeof(ReceiveBuffer), BytesRead, *FromAddr))
	{
		OutSize = 0;
		return false;
	}

	if (BytesRead < 4)
	{
		OutSize = 0;
		return false;
	}

	// Check if this is ChannelData (first two bits are 01)
	if ((ReceiveBuffer[0] & STUNConstants::PACKET_TYPE_MASK) == STUNConstants::PACKET_TYPE_CHANNEL_DATA)
	{
		// ChannelData format
		uint16 ChannelNumber = (ReceiveBuffer[0] << 8) | ReceiveBuffer[1];
		uint16 DataLength = (ReceiveBuffer[2] << 8) | ReceiveBuffer[3];
		
		if (BytesRead >= 4 + DataLength && DataLength <= MaxSize)
		{
			FMemory::Memcpy(Data, &ReceiveBuffer[4], DataLength);
			OutSize = DataLength;
			return true;
		}
	}
	// Check if this is a STUN message (first two bits are 00)
	else if ((ReceiveBuffer[0] & STUNConstants::PACKET_TYPE_MASK) == STUNConstants::PACKET_TYPE_STUN)
	{
		// This could be a Data indication (0x0017)
		uint16 MessageType = (ReceiveBuffer[0] << 8) | ReceiveBuffer[1];
		if (MessageType == 0x0017) // Data indication
		{
			// Parse DATA attribute (0x0013)
			int32 AttrOffset = 20;
			while (AttrOffset < BytesRead)
			{
				if (AttrOffset + 4 > BytesRead) break;
				
				uint16 AttrType = (ReceiveBuffer[AttrOffset] << 8) | ReceiveBuffer[AttrOffset + 1];
				uint16 AttrLength = (ReceiveBuffer[AttrOffset + 2] << 8) | ReceiveBuffer[AttrOffset + 3];
				
				if (AttrType == 0x0013) // DATA
				{
					if (AttrOffset + 4 + AttrLength <= BytesRead && AttrLength <= MaxSize)
					{
						FMemory::Memcpy(Data, &ReceiveBuffer[AttrOffset + 4], AttrLength);
						OutSize = AttrLength;
						return true;
					}
				}
				
				AttrOffset += 4 + ((AttrLength + 3) & ~3);
			}
		}
	}

	OutSize = 0;
	return false;
}
