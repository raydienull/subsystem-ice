// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSessionInterfaceICE.h"
#include "OnlineSubsystemICE.h"
#include "OnlineSubsystemUtils.h"

FOnlineSessionICE::FOnlineSessionICE(FOnlineSubsystemICE* InSubsystem)
	: Subsystem(InSubsystem)
{
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
	FNamedOnlineSession NewSession;
	NewSession.SessionName = SessionName;
	NewSession.SessionSettings = NewSessionSettings;
	NewSession.HostingPlayerNum = HostingPlayerNum;
	NewSession.SessionState = EOnlineSessionState::Creating;

	Sessions.Add(SessionName, NewSession);

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
	FNamedOnlineSession NewSession;
	NewSession.SessionName = SessionName;
	NewSession.SessionSettings = DesiredSession.Session.SessionSettings;
	NewSession.HostingPlayerNum = PlayerNum;
	NewSession.SessionState = EOnlineSessionState::Pending;

	Sessions.Add(SessionName, NewSession);

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
	TArray<FUniqueNetIdRef> Friends;
	Friends.Add(Friend.AsShared());
	return FindFriendSession(*GetIdentityInterface()->GetUniquePlayerId(LocalUserNum), Friends);
}

bool FOnlineSessionICE::FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend)
{
	TArray<FUniqueNetIdRef> Friends;
	Friends.Add(Friend.AsShared());
	return FindFriendSession(LocalUserId, Friends);
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
	TArray<FUniqueNetIdRef> Players;
	Players.Add(PlayerId.AsShared());
	return RegisterPlayers(SessionName, Players, bWasInvited);
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
	TArray<FUniqueNetIdRef> Players;
	Players.Add(PlayerId.AsShared());
	return UnregisterPlayers(SessionName, Players);
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
}
