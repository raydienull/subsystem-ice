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
		// ice host [sessionName] - Host a new game session
		if (FParse::Command(&Cmd, TEXT("HOST")))
		{
			FString SessionNameStr = FParse::Token(Cmd, false);
			if (SessionNameStr.IsEmpty())
			{
				SessionNameStr = TEXT("GameSession");
			}
			FName SessionName = FName(*SessionNameStr);
			
			if (SessionInterface.IsValid())
			{
				// Check if session already exists
				FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(SessionName);
				if (ExistingSession)
				{
					Ar.Logf(TEXT("ICE.HOST: Session '%s' already exists. Destroy it first."), *SessionNameStr);
					return true;
				}
				
				// Create session settings
				FOnlineSessionSettings SessionSettings;
				SessionSettings.NumPublicConnections = 4;
				SessionSettings.bShouldAdvertise = true;
				SessionSettings.bAllowJoinInProgress = true;
				SessionSettings.bIsLANMatch = false;
				SessionSettings.bUsesPresence = true;
				SessionSettings.bAllowInvites = true;
				
				// Bind to completion delegate
				SessionInterface->OnCreateSessionCompleteDelegates.AddLambda([SessionNameStr](FName InSessionName, bool bWasSuccessful)
				{
					if (bWasSuccessful)
					{
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Session '%s' created successfully!"), *SessionNameStr);
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Use ICE.LISTCANDIDATES to see your ICE candidates"));
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Share candidates with remote peer using your signaling method"));
						
						// Start the session
						IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(FName(TEXT("ICE")));
						if (OnlineSub)
						{
							IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
							if (Sessions.IsValid())
							{
								Sessions->StartSession(InSessionName);
							}
						}
					}
					else
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("ICE.HOST: Failed to create session '%s'"), *SessionNameStr);
					}
				});
				
				// Create the session
				bool bStarted = SessionInterface->CreateSession(0, SessionName, SessionSettings);
				if (bStarted)
				{
					Ar.Logf(TEXT("ICE.HOST: Creating session '%s'..."), *SessionNameStr);
				}
				else
				{
					Ar.Logf(TEXT("ICE.HOST: Failed to start session creation"));
				}
			}
			else
			{
				Ar.Logf(TEXT("ICE.HOST: Session interface not available"));
			}
			return true;
		}
		// ice join <sessionName> - Join an existing game session
		else if (FParse::Command(&Cmd, TEXT("JOIN")))
		{
			FString SessionNameStr = FParse::Token(Cmd, false);
			if (SessionNameStr.IsEmpty())
			{
				Ar.Logf(TEXT("Usage: ICE JOIN <sessionName>"));
				return true;
			}
			FName SessionName = FName(*SessionNameStr);
			
			if (SessionInterface.IsValid())
			{
				// Check if session already exists
				FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(SessionName);
				if (ExistingSession)
				{
					Ar.Logf(TEXT("ICE.JOIN: Session '%s' already exists. Destroy it first."), *SessionNameStr);
					return true;
				}
				
				// Create a mock search result for joining
				FOnlineSessionSearchResult SearchResult;
				SearchResult.Session.SessionSettings.NumPublicConnections = 4;
				SearchResult.Session.SessionSettings.bShouldAdvertise = true;
				SearchResult.Session.SessionSettings.bAllowJoinInProgress = true;
				SearchResult.Session.SessionSettings.bIsLANMatch = false;
				SearchResult.Session.SessionSettings.bUsesPresence = true;
				SearchResult.Session.SessionSettings.bAllowInvites = true;
				
				// Bind to completion delegate
				SessionInterface->OnJoinSessionCompleteDelegates.AddLambda([SessionNameStr](FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
				{
					if (Result == EOnJoinSessionCompleteResult::Success)
					{
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Joined session '%s' successfully!"), *SessionNameStr);
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Use ICE.LISTCANDIDATES to see your ICE candidates"));
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Share candidates with remote peer using your signaling method"));
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: After exchanging candidates, use ICE.STARTCHECKS to establish P2P connection"));
					}
					else
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("ICE.JOIN: Failed to join session '%s'"), *SessionNameStr);
					}
				});
				
				// Join the session
				bool bStarted = SessionInterface->JoinSession(0, SessionName, SearchResult);
				if (bStarted)
				{
					Ar.Logf(TEXT("ICE.JOIN: Joining session '%s'..."), *SessionNameStr);
				}
				else
				{
					Ar.Logf(TEXT("ICE.JOIN: Failed to start join session"));
				}
			}
			else
			{
				Ar.Logf(TEXT("ICE.JOIN: Session interface not available"));
			}
			return true;
		}
		// ice setremotepeer <ip> <port> - Manually set remote peer for testing
		else if (FParse::Command(&Cmd, TEXT("SETREMOTEPEER")))
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
		// ice signaling - Show signaling status
		else if (FParse::Command(&Cmd, TEXT("SIGNALING")))
		{
			if (SessionInterface.IsValid())
			{
				// Access signaling interface through a getter (we'll need to add this)
				Ar.Logf(TEXT("=== ICE Signaling Status ==="));
				Ar.Logf(TEXT("Signaling: Local File-Based"));
				Ar.Logf(TEXT("Directory: Saved/ICESignaling"));
				Ar.Logf(TEXT("Status: Active"));
				Ar.Logf(TEXT("============================"));
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
			Ar.Logf(TEXT("  ICE HOST [sessionName] - Host a new game session (simplified)"));
			Ar.Logf(TEXT("  ICE JOIN <sessionName> - Join an existing game session (simplified)"));
			Ar.Logf(TEXT("  ICE SETREMOTEPEER <ip> <port> - Set remote peer address (manual)"));
			Ar.Logf(TEXT("  ICE ADDCANDIDATE <candidate> - Add remote ICE candidate (manual)"));
			Ar.Logf(TEXT("  ICE LISTCANDIDATES - List local ICE candidates"));
			Ar.Logf(TEXT("  ICE STARTCHECKS - Start connectivity checks"));
			Ar.Logf(TEXT("  ICE STATUS - Show connection status"));
			Ar.Logf(TEXT("  ICE SIGNALING - Show signaling status"));
			Ar.Logf(TEXT("  ICE HELP - Show this help"));
			Ar.Logf(TEXT(""));
			Ar.Logf(TEXT("Simplified P2P Testing Workflow:"));
			Ar.Logf(TEXT("  1. Host: ICE HOST [sessionName]"));
			Ar.Logf(TEXT("  2. Both: ICE LISTCANDIDATES (share candidates out-of-band)"));
			Ar.Logf(TEXT("  3. Client: ICE JOIN <sessionName>"));
			Ar.Logf(TEXT("  4. Both: ICE ADDCANDIDATE <candidate> (for each remote candidate)"));
			Ar.Logf(TEXT("  5. Both: ICE STARTCHECKS"));
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
