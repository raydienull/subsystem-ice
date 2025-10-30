// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemICE.h"
#include "OnlineSessionInterfaceICE.h"
#include "OnlineIdentityInterfaceICE.h"
#include "Misc/ConfigCacheIni.h"

FOnlineSubsystemICE::FOnlineSubsystemICE(FName InInstanceName)
	: FOnlineSubsystemImpl(TEXT("ICE"), InInstanceName)
	, SessionInterface(nullptr)
	, IdentityInterface(nullptr)
{
}

bool FOnlineSubsystemICE::Init()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Initializing OnlineSubsystemICE"));

	// Read configuration from DefaultEngine.ini
	GConfig->GetString(TEXT("OnlineSubsystemICE"), TEXT("STUNServer"), STUNServerAddress, GEngineIni);
	GConfig->GetString(TEXT("OnlineSubsystemICE"), TEXT("TURNServer"), TURNServerAddress, GEngineIni);
	GConfig->GetString(TEXT("OnlineSubsystemICE"), TEXT("TURNUsername"), TURNUsername, GEngineIni);
	GConfig->GetString(TEXT("OnlineSubsystemICE"), TEXT("TURNCredential"), TURNCredential, GEngineIni);

	// Set default values if not configured
	if (STUNServerAddress.IsEmpty())
	{
		STUNServerAddress = TEXT("stun.l.google.com:19302");
	}

	UE_LOG(LogOnlineICE, Log, TEXT("STUN Server: %s"), *STUNServerAddress);
	UE_LOG(LogOnlineICE, Log, TEXT("TURN Server: %s"), *TURNServerAddress);

	// Create interfaces
	SessionInterface = MakeShared<FOnlineSessionICE, ESPMode::ThreadSafe>(this);
	IdentityInterface = MakeShared<FOnlineIdentityICE, ESPMode::ThreadSafe>(this);

	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Initialized Successfully"));
	return true;
}

bool FOnlineSubsystemICE::Shutdown()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Shutting down OnlineSubsystemICE"));

	SessionInterface = nullptr;
	IdentityInterface = nullptr;

	return true;
}

FString FOnlineSubsystemICE::GetAppId() const
{
	return TEXT("ICE");
}

bool FOnlineSubsystemICE::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FOnlineSubsystemImpl::Exec(InWorld, Cmd, Ar))
	{
		return true;
	}

	// ICE specific console commands for testing and debugging
	if (FParse::Command(&Cmd, TEXT("ICE")))
	{
		// ice setremotepeer <ip> <port> - Manually set remote peer for testing
		if (FParse::Command(&Cmd, TEXT("SETREMOTEPEER")))
		{
			FString IPAddress = FParse::Token(Cmd, false);
			FString PortStr = FParse::Token(Cmd, false);
			
			if (!IPAddress.IsEmpty() && !PortStr.IsEmpty())
			{
				int32 Port = FCString::Atoi(*PortStr);
				
				if (SessionInterface.IsValid())
				{
					SessionInterface->SetRemotePeer(IPAddress, Port);
					Ar.Logf(TEXT("ICE: Remote peer set to %s:%d"), *IPAddress, Port);
					return true;
				}
				else
				{
					Ar.Logf(TEXT("ICE: Session interface not available"));
				}
			}
			else
			{
				Ar.Logf(TEXT("Usage: ICE SETREMOTEPEER <ip> <port>"));
			}
			return true;
		}
		// ice addcandidate <candidate_string> - Manually add remote candidate for testing
		else if (FParse::Command(&Cmd, TEXT("ADDCANDIDATE")))
		{
			FString CandidateStr = FString(Cmd).TrimStartAndEnd();
			
			if (!CandidateStr.IsEmpty())
			{
				if (SessionInterface.IsValid())
				{
					SessionInterface->AddRemoteICECandidate(CandidateStr);
					Ar.Logf(TEXT("ICE: Added remote candidate: %s"), *CandidateStr);
					return true;
				}
				else
				{
					Ar.Logf(TEXT("ICE: Session interface not available"));
				}
			}
			else
			{
				Ar.Logf(TEXT("Usage: ICE ADDCANDIDATE <candidate_string>"));
			}
			return true;
		}
		// ice listcandidates - List local ICE candidates
		else if (FParse::Command(&Cmd, TEXT("LISTCANDIDATES")))
		{
			if (SessionInterface.IsValid())
			{
				TArray<FString> Candidates = SessionInterface->GetLocalICECandidates();
				Ar.Logf(TEXT("ICE: Local candidates (%d):"), Candidates.Num());
				for (const FString& Candidate : Candidates)
				{
					Ar.Logf(TEXT("  %s"), *Candidate);
				}
				return true;
			}
			else
			{
				Ar.Logf(TEXT("ICE: Session interface not available"));
			}
			return true;
		}
		// ice startchecks - Start connectivity checks
		else if (FParse::Command(&Cmd, TEXT("STARTCHECKS")))
		{
			if (SessionInterface.IsValid())
			{
				bool bSuccess = SessionInterface->StartICEConnectivityChecks();
				Ar.Logf(TEXT("ICE: Connectivity checks %s"), bSuccess ? TEXT("started") : TEXT("failed"));
				return true;
			}
			else
			{
				Ar.Logf(TEXT("ICE: Session interface not available"));
			}
			return true;
		}
		// ice status - Show ICE connection status
		else if (FParse::Command(&Cmd, TEXT("STATUS")))
		{
			if (SessionInterface.IsValid())
			{
				SessionInterface->DumpICEStatus(Ar);
				return true;
			}
			else
			{
				Ar.Logf(TEXT("ICE: Session interface not available"));
			}
			return true;
		}
		// ice help - Show available commands
		else if (FParse::Command(&Cmd, TEXT("HELP")))
		{
			Ar.Logf(TEXT("Available ICE commands:"));
			Ar.Logf(TEXT("  ICE SETREMOTEPEER <ip> <port> - Set remote peer address"));
			Ar.Logf(TEXT("  ICE ADDCANDIDATE <candidate> - Add remote ICE candidate"));
			Ar.Logf(TEXT("  ICE LISTCANDIDATES - List local ICE candidates"));
			Ar.Logf(TEXT("  ICE STARTCHECKS - Start connectivity checks"));
			Ar.Logf(TEXT("  ICE STATUS - Show connection status"));
			Ar.Logf(TEXT("  ICE HELP - Show this help"));
			return true;
		}
		else
		{
			Ar.Logf(TEXT("Unknown ICE command. Type 'ICE HELP' for available commands."));
			return true;
		}
	}

	return false;
}

FText FOnlineSubsystemICE::GetOnlineServiceName() const
{
	return NSLOCTEXT("OnlineSubsystemICE", "OnlineServiceName", "ICE");
}

bool FOnlineSubsystemICE::IsEnabled() const
{
	return true;
}

bool FOnlineSubsystemICE::Tick(float DeltaTime)
{
	if (!FOnlineSubsystemImpl::Tick(DeltaTime))
	{
		return false;
	}

	if (SessionInterface.IsValid())
	{
		SessionInterface->Tick(DeltaTime);
	}

	return true;
}

IOnlineSessionPtr FOnlineSubsystemICE::GetSessionInterface() const
{
	return SessionInterface;
}

IOnlineIdentityPtr FOnlineSubsystemICE::GetIdentityInterface() const
{
	return IdentityInterface;
}
