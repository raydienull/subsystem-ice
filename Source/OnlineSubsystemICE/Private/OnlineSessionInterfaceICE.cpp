// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSessionInterfaceICE.h"
#include "OnlineSubsystemICE.h"
#include "OnlineSubsystemUtils.h"
#include "ICEAgent.h"

FOnlineSessionICE::FOnlineSessionICE(FOnlineSubsystemICE* InSubsystem)
	: Subsystem(InSubsystem)
	, RemotePeerPort(0)
{
	// Create ICE agent with config from subsystem
	FICEAgentConfig Config;
	
	if (Subsystem)
	{
		// Add STUN server
		if (!Subsystem->GetSTUNServerAddress().IsEmpty())
		{
			Config.STUNServers.Add(Subsystem->GetSTUNServerAddress());
		}
		
		// Add TURN server
		if (!Subsystem->GetTURNServerAddress().IsEmpty())
		{
			Config.TURNServers.Add(Subsystem->GetTURNServerAddress());
			Config.TURNUsername = Subsystem->GetTURNUsername();
			Config.TURNCredential = Subsystem->GetTURNCredential();
		}
	}
	
	// Default STUN server if none configured
	if (Config.STUNServers.Num() == 0)
	{
		Config.STUNServers.Add(TEXT("stun.l.google.com:19302"));
	}
	
	ICEAgent = MakeShared<FICEAgent>(Config);
}

FOnlineSessionICE::~FOnlineSessionICE()
{
}

bool FOnlineSessionICE::CreateSession(int32 HostingPlayerNum, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	UE_LOG(LogOnlineICE, Log, TEXT("CreateSession: %s for player %d"), *SessionName.ToString(), HostingPlayerNum);

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot create session '%s': session already exists"), *SessionName.ToString());
		TriggerOnCreateSessionCompleteDelegates(SessionName, false);
		return false;
	}

	// Create a new session
	FNamedOnlineSession& NewSession = Sessions.Add(SessionName, FNamedOnlineSession(SessionName, NewSessionSettings));
	NewSession.HostingPlayerNum = HostingPlayerNum;
	NewSession.SessionState = EOnlineSessionState::Creating;

	// Mark session as pending (in a real implementation, we would gather ICE candidates here)
	Session = GetNamedSession(SessionName);
	if (Session)
	{
		Session->SessionState = EOnlineSessionState::Pending;
		TriggerOnCreateSessionCompleteDelegates(SessionName, true);
		return true;
	}

	return false;
}

bool FOnlineSessionICE::CreateSession(const FUniqueNetId& HostingPlayerId, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	return CreateSession(0, SessionName, NewSessionSettings);
}

bool FOnlineSessionICE::StartSession(FName SessionName)
{
	UE_LOG(LogOnlineICE, Log, TEXT("StartSession: %s"), *SessionName.ToString());

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot start session '%s': session not found"), *SessionName.ToString());
		TriggerOnStartSessionCompleteDelegates(SessionName, false);
		return false;
	}

	Session->SessionState = EOnlineSessionState::InProgress;
	TriggerOnStartSessionCompleteDelegates(SessionName, true);
	return true;
}

bool FOnlineSessionICE::UpdateSession(FName SessionName, FOnlineSessionSettings& UpdatedSessionSettings, bool bShouldRefreshOnlineData)
{
	UE_LOG(LogOnlineICE, Log, TEXT("UpdateSession: %s"), *SessionName.ToString());

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		return false;
	}

	Session->SessionSettings = UpdatedSessionSettings;
	TriggerOnUpdateSessionCompleteDelegates(SessionName, true);
	return true;
}

bool FOnlineSessionICE::EndSession(FName SessionName)
{
	UE_LOG(LogOnlineICE, Log, TEXT("EndSession: %s"), *SessionName.ToString());

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		TriggerOnEndSessionCompleteDelegates(SessionName, false);
		return false;
	}

	Session->SessionState = EOnlineSessionState::Ended;
	TriggerOnEndSessionCompleteDelegates(SessionName, true);
	return true;
}

bool FOnlineSessionICE::DestroySession(FName SessionName, const FOnDestroySessionCompleteDelegate& CompletionDelegate)
{
	UE_LOG(LogOnlineICE, Log, TEXT("DestroySession: %s"), *SessionName.ToString());

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		CompletionDelegate.ExecuteIfBound(SessionName, false);
		TriggerOnDestroySessionCompleteDelegates(SessionName, false);
		return false;
	}

	Session->SessionState = EOnlineSessionState::Destroying;
	RemoveNamedSession(SessionName);

	CompletionDelegate.ExecuteIfBound(SessionName, true);
	TriggerOnDestroySessionCompleteDelegates(SessionName, true);
	return true;
}

bool FOnlineSessionICE::IsPlayerInSession(FName SessionName, const FUniqueNetId& UniqueId)
{
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		for (const FUniqueNetIdRef& PlayerId : Session->RegisteredPlayers)
		{
			if (*PlayerId == UniqueId)
			{
				return true;
			}
		}
	}
	return false;
}

bool FOnlineSessionICE::StartMatchmaking(const TArray<FUniqueNetIdRef>& LocalPlayers, FName SessionName, const FOnlineSessionSettings& NewSessionSettings, TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	UE_LOG(LogOnlineICE, Warning, TEXT("StartMatchmaking not implemented yet"));
	TriggerOnMatchmakingCompleteDelegates(SessionName, false);
	return false;
}

bool FOnlineSessionICE::CancelMatchmaking(int32 SearchingPlayerNum, FName SessionName)
{
	UE_LOG(LogOnlineICE, Warning, TEXT("CancelMatchmaking not implemented yet"));
	TriggerOnCancelMatchmakingCompleteDelegates(SessionName, false);
	return false;
}

bool FOnlineSessionICE::CancelMatchmaking(const FUniqueNetId& SearchingPlayerId, FName SessionName)
{
	return CancelMatchmaking(0, SessionName);
}

bool FOnlineSessionICE::FindSessions(int32 SearchingPlayerNum, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	UE_LOG(LogOnlineICE, Log, TEXT("FindSessions"));

	CurrentSessionSearch = SearchSettings;
	SearchSettings->SearchState = EOnlineAsyncTaskState::InProgress;

	// In a real implementation, we would query a signaling server here
	// For now, just mark as done with no results
	SearchSettings->SearchState = EOnlineAsyncTaskState::Done;
	TriggerOnFindSessionsCompleteDelegates(true);
	return true;
}

bool FOnlineSessionICE::FindSessions(const FUniqueNetId& SearchingPlayerId, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	return FindSessions(0, SearchSettings);
}

bool FOnlineSessionICE::FindSessionById(const FUniqueNetId& SearchingUserId, const FUniqueNetId& SessionId, const FUniqueNetId& FriendId, const FOnSingleSessionResultCompleteDelegate& CompletionDelegate)
{
	UE_LOG(LogOnlineICE, Warning, TEXT("FindSessionById not implemented yet"));
	CompletionDelegate.ExecuteIfBound(0, false, FOnlineSessionSearchResult());
	return false;
}

bool FOnlineSessionICE::CancelFindSessions()
{
	UE_LOG(LogOnlineICE, Log, TEXT("CancelFindSessions"));

	if (CurrentSessionSearch.IsValid())
	{
		CurrentSessionSearch->SearchState = EOnlineAsyncTaskState::Failed;
		CurrentSessionSearch = nullptr;
	}

	TriggerOnCancelFindSessionsCompleteDelegates(true);
	return true;
}

bool FOnlineSessionICE::PingSearchResults(const FOnlineSessionSearchResult& SearchResult)
{
	return false;
}

bool FOnlineSessionICE::JoinSession(int32 PlayerNum, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	UE_LOG(LogOnlineICE, Log, TEXT("JoinSession: %s for player %d"), *SessionName.ToString(), PlayerNum);

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot join session '%s': session already exists"), *SessionName.ToString());
		TriggerOnJoinSessionCompleteDelegates(SessionName, EOnJoinSessionCompleteResult::SessionDoesNotExist);
		return false;
	}

	// Create a new session based on the search result
	FNamedOnlineSession& NewSession = Sessions.Add(SessionName, FNamedOnlineSession(SessionName, DesiredSession.Session.SessionSettings));
	NewSession.HostingPlayerNum = PlayerNum;
	NewSession.SessionState = EOnlineSessionState::Pending;

	// In a real implementation, we would initiate ICE connection here
	TriggerOnJoinSessionCompleteDelegates(SessionName, EOnJoinSessionCompleteResult::Success);
	return true;
}

bool FOnlineSessionICE::JoinSession(const FUniqueNetId& PlayerId, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	return JoinSession(0, SessionName, DesiredSession);
}

bool FOnlineSessionICE::FindFriendSession(int32 LocalUserNum, const FUniqueNetId& Friend)
{
	UE_LOG(LogOnlineICE, Warning, TEXT("FindFriendSession not implemented yet"));
	TriggerOnFindFriendSessionCompleteDelegates(LocalUserNum, false, TArray<FOnlineSessionSearchResult>());
	return false;
}

bool FOnlineSessionICE::FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend)
{
	UE_LOG(LogOnlineICE, Warning, TEXT("FindFriendSession not implemented yet"));
	TriggerOnFindFriendSessionCompleteDelegates(0, false, TArray<FOnlineSessionSearchResult>());
	return false;
}

bool FOnlineSessionICE::FindFriendSession(const FUniqueNetId& LocalUserId, const TArray<FUniqueNetIdRef>& FriendList)
{
	UE_LOG(LogOnlineICE, Warning, TEXT("FindFriendSession not implemented yet"));
	TriggerOnFindFriendSessionCompleteDelegates(0, false, TArray<FOnlineSessionSearchResult>());
	return false;
}

bool FOnlineSessionICE::SendSessionInviteToFriend(int32 LocalUserNum, FName SessionName, const FUniqueNetId& Friend)
{
	UE_LOG(LogOnlineICE, Warning, TEXT("SendSessionInviteToFriend not implemented yet"));
	return false;
}

bool FOnlineSessionICE::SendSessionInviteToFriend(const FUniqueNetId& LocalUserId, FName SessionName, const FUniqueNetId& Friend)
{
	return SendSessionInviteToFriend(0, SessionName, Friend);
}

bool FOnlineSessionICE::SendSessionInviteToFriends(int32 LocalUserNum, FName SessionName, const TArray<FUniqueNetIdRef>& Friends)
{
	UE_LOG(LogOnlineICE, Warning, TEXT("SendSessionInviteToFriends not implemented yet"));
	return false;
}

bool FOnlineSessionICE::SendSessionInviteToFriends(const FUniqueNetId& LocalUserId, FName SessionName, const TArray<FUniqueNetIdRef>& Friends)
{
	return SendSessionInviteToFriends(0, SessionName, Friends);
}

bool FOnlineSessionICE::GetResolvedConnectString(FName SessionName, FString& ConnectInfo, FName PortType)
{
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		return false;
	}

	// In a real implementation, this would return the ICE connection string
	ConnectInfo = TEXT("ice://pending");
	return true;
}

bool FOnlineSessionICE::GetResolvedConnectString(const FOnlineSessionSearchResult& SearchResult, FName PortType, FString& ConnectInfo)
{
	ConnectInfo = TEXT("ice://pending");
	return true;
}

FOnlineSessionSettings* FOnlineSessionICE::GetSessionSettings(FName SessionName)
{
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		return &Session->SessionSettings;
	}
	return nullptr;
}

bool FOnlineSessionICE::RegisterPlayer(FName SessionName, const FUniqueNetId& PlayerId, bool bWasInvited)
{
	UE_LOG(LogOnlineICE, Log, TEXT("RegisterPlayer: %s"), *SessionName.ToString());

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		TArray<FUniqueNetIdRef> EmptyArray;
		TriggerOnRegisterPlayersCompleteDelegates(SessionName, EmptyArray, false);
		return false;
	}

	// Check if player already registered (to reuse existing shared pointer)
	for (const FUniqueNetIdRef& ExistingPlayer : Session->RegisteredPlayers)
	{
		if (*ExistingPlayer == PlayerId)
		{
			// Already registered, just trigger success delegate
			TArray<FUniqueNetIdRef> Players;
			Players.Add(ExistingPlayer);
			TriggerOnRegisterPlayersCompleteDelegates(SessionName, Players, true);
			return true;
		}
	}

	// Create a new copy for storage using string representation
	// Note: This involves a string conversion, but it's safe and avoids const_cast
	FUniqueNetIdPtr PlayerIdCopy = Subsystem->GetIdentityInterface()->CreateUniquePlayerId(PlayerId.ToString());
	
	if (PlayerIdCopy.IsValid())
	{
		Session->RegisteredPlayers.AddUnique(PlayerIdCopy.ToSharedRef());
		
		TArray<FUniqueNetIdRef> Players;
		Players.Add(PlayerIdCopy.ToSharedRef());
		TriggerOnRegisterPlayersCompleteDelegates(SessionName, Players, true);
		return true;
	}
	
	TArray<FUniqueNetIdRef> EmptyArray;
	TriggerOnRegisterPlayersCompleteDelegates(SessionName, EmptyArray, false);
	return false;
}

bool FOnlineSessionICE::RegisterPlayers(FName SessionName, const TArray<FUniqueNetIdRef>& Players, bool bWasInvited)
{
	UE_LOG(LogOnlineICE, Log, TEXT("RegisterPlayers: %s"), *SessionName.ToString());

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		TriggerOnRegisterPlayersCompleteDelegates(SessionName, Players, false);
		return false;
	}

	for (const FUniqueNetIdRef& PlayerId : Players)
	{
		Session->RegisteredPlayers.AddUnique(PlayerId);
	}

	TriggerOnRegisterPlayersCompleteDelegates(SessionName, Players, true);
	return true;
}

bool FOnlineSessionICE::UnregisterPlayer(FName SessionName, const FUniqueNetId& PlayerId)
{
	UE_LOG(LogOnlineICE, Log, TEXT("UnregisterPlayer: %s"), *SessionName.ToString());

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		TArray<FUniqueNetIdRef> EmptyArray;
		TriggerOnUnregisterPlayersCompleteDelegates(SessionName, EmptyArray, false);
		return false;
	}

	// Find and remove the matching player
	TArray<FUniqueNetIdRef> RemovedPlayers;
	for (int32 i = Session->RegisteredPlayers.Num() - 1; i >= 0; --i)
	{
		if (*Session->RegisteredPlayers[i] == PlayerId)
		{
			RemovedPlayers.Add(Session->RegisteredPlayers[i]);
			Session->RegisteredPlayers.RemoveAt(i);
			break;
		}
	}

	// Trigger delegate once after the operation
	bool bSuccess = RemovedPlayers.Num() > 0;
	TriggerOnUnregisterPlayersCompleteDelegates(SessionName, RemovedPlayers, bSuccess);
	return bSuccess;
}

bool FOnlineSessionICE::UnregisterPlayers(FName SessionName, const TArray<FUniqueNetIdRef>& Players)
{
	UE_LOG(LogOnlineICE, Log, TEXT("UnregisterPlayers: %s"), *SessionName.ToString());

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		TriggerOnUnregisterPlayersCompleteDelegates(SessionName, Players, false);
		return false;
	}

	for (const FUniqueNetIdRef& PlayerId : Players)
	{
		Session->RegisteredPlayers.Remove(PlayerId);
	}

	TriggerOnUnregisterPlayersCompleteDelegates(SessionName, Players, true);
	return true;
}

void FOnlineSessionICE::RegisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnRegisterLocalPlayerCompleteDelegate& Delegate)
{
	Delegate.ExecuteIfBound(PlayerId, EOnJoinSessionCompleteResult::Success);
}

void FOnlineSessionICE::UnregisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnUnregisterLocalPlayerCompleteDelegate& Delegate)
{
	Delegate.ExecuteIfBound(PlayerId, true);
}

int32 FOnlineSessionICE::GetNumSessions()
{
	return Sessions.Num();
}

void FOnlineSessionICE::DumpSessionState()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Dumping Session State:"));
	for (const auto& SessionPair : Sessions)
	{
		UE_LOG(LogOnlineICE, Log, TEXT("  Session: %s, State: %s"), 
			*SessionPair.Key.ToString(), 
			EOnlineSessionState::ToString(SessionPair.Value.SessionState));
	}
}

FNamedOnlineSession* FOnlineSessionICE::GetNamedSession(FName SessionName)
{
	return Sessions.Find(SessionName);
}

void FOnlineSessionICE::RemoveNamedSession(FName SessionName)
{
	Sessions.Remove(SessionName);
}

EOnlineSessionState::Type FOnlineSessionICE::GetSessionState(FName SessionName) const
{
	const FNamedOnlineSession* Session = Sessions.Find(SessionName);
	if (Session)
	{
		return Session->SessionState;
	}
	return EOnlineSessionState::NoSession;
}

bool FOnlineSessionICE::HasPresenceSession()
{
	for (const auto& SessionPair : Sessions)
	{
		if (SessionPair.Value.SessionSettings.bUsesPresence)
		{
			return true;
		}
	}
	return false;
}

void FOnlineSessionICE::Tick(float DeltaTime)
{
	// Periodic processing for sessions
	// In a real implementation, this would handle ICE keepalives, timeouts, etc.
	if (ICEAgent.IsValid())
	{
		ICEAgent->Tick(DeltaTime);
	}
}

void FOnlineSessionICE::SetRemotePeer(const FString& IPAddress, int32 Port)
{
	UE_LOG(LogOnlineICE, Log, TEXT("Setting remote peer: %s:%d"), *IPAddress, Port);
	RemotePeerIP = IPAddress;
	RemotePeerPort = Port;

	// Create a remote candidate from the IP/Port
	if (ICEAgent.IsValid())
	{
		FICECandidate RemoteCandidate;
		RemoteCandidate.Foundation = TEXT("remote");
		RemoteCandidate.ComponentId = 1;
		RemoteCandidate.Transport = TEXT("UDP");
		RemoteCandidate.Priority = 1000;
		RemoteCandidate.Address = IPAddress;
		RemoteCandidate.Port = Port;
		RemoteCandidate.Type = EICECandidateType::Host;

		ICEAgent->AddRemoteCandidate(RemoteCandidate);
		UE_LOG(LogOnlineICE, Log, TEXT("Added remote candidate for peer"));
	}
}

void FOnlineSessionICE::AddRemoteICECandidate(const FString& CandidateString)
{
	UE_LOG(LogOnlineICE, Log, TEXT("Adding remote ICE candidate: %s"), *CandidateString);

	if (ICEAgent.IsValid())
	{
		FICECandidate Candidate = FICECandidate::FromString(CandidateString);
		if (!Candidate.Address.IsEmpty())
		{
			ICEAgent->AddRemoteCandidate(Candidate);
			UE_LOG(LogOnlineICE, Log, TEXT("Remote candidate added successfully"));
		}
		else
		{
			UE_LOG(LogOnlineICE, Warning, TEXT("Failed to parse candidate string"));
		}
	}
}

TArray<FString> FOnlineSessionICE::GetLocalICECandidates()
{
	TArray<FString> CandidateStrings;

	if (ICEAgent.IsValid())
	{
		// Gather candidates if not already done
		ICEAgent->GatherCandidates();

		TArray<FICECandidate> Candidates = ICEAgent->GetLocalCandidates();
		for (const FICECandidate& Candidate : Candidates)
		{
			CandidateStrings.Add(Candidate.ToString());
		}
	}

	return CandidateStrings;
}

bool FOnlineSessionICE::StartICEConnectivityChecks()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Starting ICE connectivity checks"));

	if (ICEAgent.IsValid())
	{
		return ICEAgent->StartConnectivityChecks();
	}

	return false;
}

void FOnlineSessionICE::DumpICEStatus(FOutputDevice& Ar)
{
	Ar.Logf(TEXT("=== ICE Connection Status ==="));

	if (ICEAgent.IsValid())
	{
		Ar.Logf(TEXT("Connected: %s"), ICEAgent->IsConnected() ? TEXT("Yes") : TEXT("No"));

		TArray<FICECandidate> LocalCandidates = ICEAgent->GetLocalCandidates();
		Ar.Logf(TEXT("Local Candidates: %d"), LocalCandidates.Num());
		for (const FICECandidate& Candidate : LocalCandidates)
		{
			Ar.Logf(TEXT("  %s"), *Candidate.ToString());
		}

		if (!RemotePeerIP.IsEmpty())
		{
			Ar.Logf(TEXT("Remote Peer: %s:%d"), *RemotePeerIP, RemotePeerPort);
		}
		else
		{
			Ar.Logf(TEXT("Remote Peer: Not set"));
		}
	}
	else
	{
		Ar.Logf(TEXT("ICE Agent not initialized"));
	}

	Ar.Logf(TEXT("============================="));
}
