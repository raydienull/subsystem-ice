// Copyright Epic Games, Inc. All Rights Reserved.

#include "ICEAgent.h"
#include "OnlineSubsystemICEPackage.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

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
	TSharedPtr<FInternetAddr> LocalAddr = SocketSubsystem->GetLocalHostAddr(*GLog, false);
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

	// TURN implementation would go here
	// This is a complex protocol that requires authentication and allocation
	// For now, we'll leave this as a placeholder

	UE_LOG(LogOnlineICE, Warning, TEXT("TURN relay candidate gathering not fully implemented"));
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
	if (!STUNAddr.IsValid())
	{
		auto ResolveInfo = SocketSubsystem->GetHostByName(TCHAR_TO_ANSI(*Host));
		if (ResolveInfo)
		{
			while (!ResolveInfo->IsComplete());
			
			if (ResolveInfo->GetErrorCode() == SE_NO_ERROR)
			{
				STUNAddr = SocketSubsystem->CreateInternetAddr();
				STUNAddr->SetIp(ResolveInfo->GetResolvedAddress().GetIp());
			}
		}
	}

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

	TSharedPtr<FInternetAddr> RemoteAddr = SocketSubsystem->CreateInternetAddr();
	RemoteAddr->SetIp(*SelectedRemoteCandidate.Address);
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

	TSharedPtr<FInternetAddr> RemoteAddr = SocketSubsystem->CreateInternetAddr();
	RemoteAddr->SetIp(*SelectedRemoteCandidate.Address);
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
