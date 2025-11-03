// Copyright Epic Games, Inc. All Rights Reserved.

#include "ICEAgent.h"
#include "OnlineSubsystemICEPackage.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/SecureHash.h"

// STUN/TURN protocol constants (RFC 5389/5766)
namespace STUNConstants
{
	constexpr int32 TRANSACTION_ID_LENGTH = 12;
	constexpr int32 MESSAGE_INTEGRITY_ATTR_SIZE = 24; // Header(4) + HMAC-SHA1(20)
	constexpr int32 HMAC_SHA1_SIZE = 20;
	constexpr uint8 ERROR_CLASS_MASK = 0x07;
	constexpr int32 ERROR_CLASS_MULTIPLIER = 100;
	constexpr int32 SHA1_BLOCK_SIZE = 64;
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
	
	// Parse ICE candidate string (simplified parser)
	TArray<FString> Parts;
	CandidateString.ParseIntoArray(Parts, TEXT(" "));
	
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
	, bIsConnected(false)
{
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
	HostCandidate.Port = 0; // Will be assigned when socket is created
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
						OutPublicPort = XorPort ^ 0x2112;

						// XOR-ed IP (4 bytes at offset 8-11)
						uint32 XorIP = (STUNResponse[Offset + 8] << 24) | (STUNResponse[Offset + 9] << 16) |
						              (STUNResponse[Offset + 10] << 8) | STUNResponse[Offset + 11];
						uint32 PublicIPValue = XorIP ^ 0x2112A442;

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

	// Create socket for TURN
	FSocket* TURNSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("TURN"), TURNAddr->GetProtocolType());
	if (!TURNSocket)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to create TURN socket"));
		return false;
	}

	// First attempt: Send request without authentication to get realm and nonce
	// The server will respond with 401 Unauthorized if authentication is required
	bool bSuccess = PerformTURNAllocationRequest(TURNSocket, TURNAddr, Username, Credential, FString(), FString(), OutRelayIP, OutRelayPort, false);
	
	SocketSubsystem->DestroySocket(TURNSocket);
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
		// the MESSAGE-INTEGRITY attribute itself before calculating HMAC
		int32 MessageLengthForIntegrity = MessageIntegrityOffset - 20 + STUNConstants::MESSAGE_INTEGRITY_ATTR_SIZE;
		TURNRequest[LengthOffset] = (MessageLengthForIntegrity >> 8) & 0xFF;
		TURNRequest[LengthOffset + 1] = MessageLengthForIntegrity & 0xFF;

		// Key = MD5(username:realm:password) for long-term credentials
		FString KeyString = Username + TEXT(":") + Realm + TEXT(":") + Credential;
		uint8 KeyMD5[16];
		CalculateMD5(KeyString, KeyMD5);

		// Calculate HMAC-SHA1 over the message including header with adjusted length,
		// up to but not including the MESSAGE-INTEGRITY value itself
		uint8 HMAC[20];
		CalculateHMACSHA1(TURNRequest.GetData(), MessageIntegrityOffset + 4, KeyMD5, 16, HMAC);
		
		// Copy HMAC to MESSAGE-INTEGRITY attribute
		for (int32 i = 0; i < 20; i++)
		{
			TURNRequest[MessageIntegrityOffset + 4 + i] = HMAC[i];
		}
	}

	// Calculate final message length (everything after the header)
	int32 MessageLength = Offset - 20;
	TURNRequest[LengthOffset] = (MessageLength >> 8) & 0xFF;
	TURNRequest[LengthOffset + 1] = MessageLength & 0xFF;

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

			// Parse attributes to find XOR-RELAYED-ADDRESS (0x0016)
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
						OutRelayPort = XorPort ^ 0x2112;

						// XOR-ed IP (4 bytes at offset 8-11)
						uint32 XorIP = (TURNResponse[AttrOffset + 8] << 24) | (TURNResponse[AttrOffset + 9] << 16) |
						              (TURNResponse[AttrOffset + 10] << 8) | TURNResponse[AttrOffset + 11];
						uint32 RelayIPValue = XorIP ^ 0x2112A442;

						// Convert to string
						OutRelayIP = FString::Printf(TEXT("%d.%d.%d.%d"),
							(RelayIPValue >> 24) & 0xFF,
							(RelayIPValue >> 16) & 0xFF,
							(RelayIPValue >> 8) & 0xFF,
							RelayIPValue & 0xFF);

						UE_LOG(LogOnlineICE, Log, TEXT("TURN allocated relay address: %s:%d"), *OutRelayIP, OutRelayPort);
						return true;
					}
				}

				// Move to next attribute (pad to 4-byte boundary)
				AttrOffset += 4 + ((AttrLength + 3) & ~3);
			}
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

bool FICEAgent::StartConnectivityChecks()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Starting ICE connectivity checks"));

	if (LocalCandidates.Num() == 0 || RemoteCandidates.Num() == 0)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("No candidates available for connectivity checks"));
		return false;
	}

	// For a basic implementation, select the highest priority candidate pair
	// In a full implementation, we would perform STUN connectivity checks on all pairs

	SelectedLocalCandidate = LocalCandidates[0];
	SelectedRemoteCandidate = RemoteCandidates[0];

	for (const FICECandidate& LocalCand : LocalCandidates)
	{
		if (LocalCand.Priority > SelectedLocalCandidate.Priority)
		{
			SelectedLocalCandidate = LocalCand;
		}
	}

	for (const FICECandidate& RemoteCand : RemoteCandidates)
	{
		if (RemoteCand.Priority > SelectedRemoteCandidate.Priority)
		{
			SelectedRemoteCandidate = RemoteCand;
		}
	}

	UE_LOG(LogOnlineICE, Log, TEXT("Selected candidate pair - Local: %s, Remote: %s"),
		*SelectedLocalCandidate.ToString(), *SelectedRemoteCandidate.ToString());

	// Create socket for communication
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to get socket subsystem"));
		return false;
	}

	TSharedPtr<FInternetAddr> RemoteAddr = SocketSubsystem->GetAddressFromString(SelectedRemoteCandidate.Address);
	if (!RemoteAddr.IsValid())
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to parse remote address: %s"), *SelectedRemoteCandidate.Address);
		return false;
	}
	RemoteAddr->SetPort(SelectedRemoteCandidate.Port);

	Socket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("ICE"), RemoteAddr->GetProtocolType());
	if (!Socket)
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to create ICE socket"));
		return false;
	}

	bIsConnected = true;
	UE_LOG(LogOnlineICE, Log, TEXT("ICE connection established"));

	return true;
}

bool FICEAgent::IsConnected() const
{
	return bIsConnected;
}

bool FICEAgent::SendData(const uint8* Data, int32 Size)
{
	if (!bIsConnected || !Socket)
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
	if (!bIsConnected || !Socket)
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
	// Periodic processing for keepalives, timeouts, etc.
}

void FICEAgent::Close()
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

	bIsConnected = false;
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
