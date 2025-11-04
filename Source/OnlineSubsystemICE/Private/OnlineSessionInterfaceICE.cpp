// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSessionInterfaceICE.h"
#include "OnlineSubsystemICE.h"
#include "OnlineSubsystemUtils.h"
#include "ICEAgent.h"
#include "ICESignalingInterface.h"
#include "Misc/Paths.h"

namespace
{
	// Helper function to convert session state to string
	const TCHAR* GetSessionStateString(EOnlineSessionState::Type State)
	{
		switch (State)
		{
			case EOnlineSessionState::NoSession: return TEXT("NoSession");
			case EOnlineSessionState::Creating: return TEXT("Creating");
			case EOnlineSessionState::Pending: return TEXT("Pending");
			case EOnlineSessionState::Starting: return TEXT("Starting");
			case EOnlineSessionState::InProgress: return TEXT("InProgress");
			case EOnlineSessionState::Ending: return TEXT("Ending");
			case EOnlineSessionState::Ended: return TEXT("Ended");
			case EOnlineSessionState::Destroying: return TEXT("Destroying");
			default: return TEXT("Unknown");
		}
	}
}

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
	
	// Initialize local file signaling for testing
	// Use temp directory for signaling files
	FString SignalingDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ICESignaling"));
	SignalingInterface = MakeShared<FLocalFileSignaling>(SignalingDir);
	
	if (SignalingInterface->Initialize())
	{
		// Bind to signaling messages
		SignalingInterface->OnSignalReceived.AddRaw(this, &FOnlineSessionICE::OnSignalReceived);
		UE_LOG(LogOnlineICE, Log, TEXT("Local signaling initialized: PeerId=%s"), *SignalingInterface->GetLocalPeerId());
	}
	else
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Failed to initialize local signaling"));
	}
}

FOnlineSessionICE::~FOnlineSessionICE()
{
	if (SignalingInterface.IsValid())
	{
		SignalingInterface->OnSignalReceived.RemoveAll(this);
		SignalingInterface->Shutdown();
	}
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

	// Gather ICE candidates for this session
	if (ICEAgent.IsValid())
	{
		UE_LOG(LogOnlineICE, Log, TEXT("Gathering ICE candidates for session '%s'"), *SessionName.ToString());
		bool bGathered = ICEAgent->GatherCandidates();
		
		if (bGathered)
		{
			TArray<FICECandidate> LocalCandidates = ICEAgent->GetLocalCandidates();
			UE_LOG(LogOnlineICE, Log, TEXT("Gathered %d ICE candidates for session"), LocalCandidates.Num());
			
			// Log candidates for debugging/manual signaling
			for (const FICECandidate& Candidate : LocalCandidates)
			{
				UE_LOG(LogOnlineICE, Verbose, TEXT("  %s"), *Candidate.ToString());
			}
			
			// Automatically send candidates via signaling (broadcast offer)
			if (SignalingInterface.IsValid() && SignalingInterface->IsActive())
			{
				SendLocalCandidates(SessionName.ToString(), FString());
			}
		}
		else
		{
			UE_LOG(LogOnlineICE, Warning, TEXT("Failed to gather ICE candidates for session '%s'"), *SessionName.ToString());
		}
	}

	// Mark session as pending
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
	UE_LOG(LogOnlineICE, Log, TEXT("UpdateSession: %s (RefreshOnlineData: %s)"), 
		*SessionName.ToString(), bShouldRefreshOnlineData ? TEXT("true") : TEXT("false"));

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot update session '%s': session not found"), *SessionName.ToString());
		TriggerOnUpdateSessionCompleteDelegates(SessionName, false);
		return false;
	}

	// Validate session state for updates
	if (Session->SessionState == EOnlineSessionState::Destroying || Session->SessionState == EOnlineSessionState::Ended)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot update session '%s': session is in state %s"), 
			*SessionName.ToString(), GetSessionStateString(Session->SessionState));
		TriggerOnUpdateSessionCompleteDelegates(SessionName, false);
		return false;
	}

	// Update session settings
	Session->SessionSettings = UpdatedSessionSettings;
	
	UE_LOG(LogOnlineICE, Log, TEXT("Session '%s' updated successfully"), *SessionName.ToString());
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
	UE_LOG(LogOnlineICE, Log, TEXT("StartMatchmaking: %s with %d players"), *SessionName.ToString(), LocalPlayers.Num());

	// Validate inputs
	if (LocalPlayers.Num() == 0)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("StartMatchmaking: No local players provided"));
		TriggerOnMatchmakingCompleteDelegates(SessionName, false);
		return false;
	}

	// Check if session already exists
	FNamedOnlineSession* ExistingSession = GetNamedSession(SessionName);
	if (ExistingSession)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("StartMatchmaking: Session '%s' already exists"), *SessionName.ToString());
		TriggerOnMatchmakingCompleteDelegates(SessionName, false);
		return false;
	}

	// Store search settings for potential match finding
	CurrentSessionSearch = SearchSettings;
	SearchSettings->SearchState = EOnlineAsyncTaskState::InProgress;

	// Basic matchmaking: First try to find an existing session
	// In a real implementation, this would query a matchmaking service
	// For now, we'll simulate a simple "create or join" behavior
	
	// Simulate finding sessions (would query signaling server in production)
	SearchSettings->SearchResults.Empty();
	SearchSettings->SearchState = EOnlineAsyncTaskState::Done;

	// If no sessions found, create a new one
	if (SearchSettings->SearchResults.Num() == 0)
	{
		UE_LOG(LogOnlineICE, Log, TEXT("No existing sessions found, creating new session for matchmaking"));
		
		// Create a new session for matchmaking
		bool bCreated = CreateSession(*LocalPlayers[0], SessionName, NewSessionSettings);
		
		if (bCreated)
		{
			// Register all local players
			FNamedOnlineSession* NewSession = GetNamedSession(SessionName);
			if (NewSession)
			{
				for (const FUniqueNetIdRef& PlayerId : LocalPlayers)
				{
					NewSession->RegisteredPlayers.AddUnique(PlayerId);
				}
			}
			
			UE_LOG(LogOnlineICE, Log, TEXT("Matchmaking session created successfully"));
			TriggerOnMatchmakingCompleteDelegates(SessionName, true);
			return true;
		}
		else
		{
			UE_LOG(LogOnlineICE, Warning, TEXT("Failed to create session for matchmaking"));
			TriggerOnMatchmakingCompleteDelegates(SessionName, false);
			return false;
		}
	}
	else
	{
		// Join the first available session
		UE_LOG(LogOnlineICE, Log, TEXT("Found existing session, attempting to join"));
		bool bJoined = JoinSession(*LocalPlayers[0], SessionName, SearchSettings->SearchResults[0]);
		
		if (bJoined)
		{
			// Register all local players
			FNamedOnlineSession* JoinedSession = GetNamedSession(SessionName);
			if (JoinedSession)
			{
				for (const FUniqueNetIdRef& PlayerId : LocalPlayers)
				{
					JoinedSession->RegisteredPlayers.AddUnique(PlayerId);
				}
			}
		}
		
		TriggerOnMatchmakingCompleteDelegates(SessionName, bJoined);
		return bJoined;
	}
}

bool FOnlineSessionICE::CancelMatchmaking(int32 SearchingPlayerNum, FName SessionName)
{
	UE_LOG(LogOnlineICE, Log, TEXT("CancelMatchmaking: %s"), *SessionName.ToString());

	// Cancel any ongoing search
	if (CurrentSessionSearch.IsValid() && CurrentSessionSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		CurrentSessionSearch->SearchState = EOnlineAsyncTaskState::Failed;
		CurrentSessionSearch = nullptr;
	}

	// If a session was created during matchmaking but not started, destroy it
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session && (Session->SessionState == EOnlineSessionState::Creating || Session->SessionState == EOnlineSessionState::Pending))
	{
		UE_LOG(LogOnlineICE, Log, TEXT("Destroying pending matchmaking session"));
		DestroySession(SessionName);
	}

	TriggerOnCancelMatchmakingCompleteDelegates(SessionName, true);
	return true;
}

bool FOnlineSessionICE::CancelMatchmaking(const FUniqueNetId& SearchingPlayerId, FName SessionName)
{
	return CancelMatchmaking(0, SessionName);
}

bool FOnlineSessionICE::FindSessions(int32 SearchingPlayerNum, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	UE_LOG(LogOnlineICE, Log, TEXT("FindSessions for player %d"), SearchingPlayerNum);

	CurrentSessionSearch = SearchSettings;
	SearchSettings->SearchState = EOnlineAsyncTaskState::InProgress;

	// Clear previous results
	SearchSettings->SearchResults.Empty();

	// In a production environment, this would query a signaling/matchmaking server
	// For local/testing purposes, we can return local sessions that are advertised
	// This allows for basic testing of the session system
	
	int32 ResultsFound = 0;
	
	// Check all local sessions that are advertised and in the right state
	for (const auto& SessionPair : Sessions)
	{
		const FNamedOnlineSession& Session = SessionPair.Value;
		
		// Only include sessions that should be advertised and are in progress or pending
		if (Session.SessionSettings.bShouldAdvertise && 
		    (Session.SessionState == EOnlineSessionState::InProgress || Session.SessionState == EOnlineSessionState::Pending))
		{
			// Check if session meets search criteria
			bool bMatchesCriteria = true;
			
			// Apply max search results limit
			if (SearchSettings->MaxSearchResults > 0 && ResultsFound >= SearchSettings->MaxSearchResults)
			{
				break;
			}
			
			// Create a search result for this session
			FOnlineSessionSearchResult SearchResult;
			SearchResult.Session = Session;
			SearchResult.PingInMs = 0; // Local session, no ping
			
			SearchSettings->SearchResults.Add(SearchResult);
			ResultsFound++;
			
			UE_LOG(LogOnlineICE, Log, TEXT("Found session: %s"), *SessionPair.Key.ToString());
		}
	}

	// Mark search as complete
	SearchSettings->SearchState = EOnlineAsyncTaskState::Done;
	
	UE_LOG(LogOnlineICE, Log, TEXT("FindSessions completed: %d results found"), ResultsFound);
	TriggerOnFindSessionsCompleteDelegates(true);
	return true;
}

bool FOnlineSessionICE::FindSessions(const FUniqueNetId& SearchingPlayerId, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	return FindSessions(0, SearchSettings);
}

bool FOnlineSessionICE::FindSessionById(const FUniqueNetId& SearchingUserId, const FUniqueNetId& SessionId, const FUniqueNetId& FriendId, const FOnSingleSessionResultCompleteDelegate& CompletionDelegate)
{
	UE_LOG(LogOnlineICE, Log, TEXT("FindSessionById: %s"), *SessionId.ToString());

	// In a production environment, this would query a signaling/matchmaking server by session ID
	// For local testing, we'll search through our local sessions
	
	FOnlineSessionSearchResult SearchResult;
	bool bFound = false;

	// Search through local sessions for a matching session ID
	for (const auto& SessionPair : Sessions)
	{
		const FNamedOnlineSession& Session = SessionPair.Value;
		
		// Check if this session's owner ID matches the session ID we're looking for
		if (Session.OwningUserId.IsValid() && *Session.OwningUserId == SessionId)
		{
			SearchResult.Session = Session;
			SearchResult.PingInMs = 0; // Local session
			bFound = true;
			UE_LOG(LogOnlineICE, Log, TEXT("Found session by ID: %s"), *SessionPair.Key.ToString());
			break;
		}
		
		// Also check if the session name converted to a unique ID matches
		FUniqueNetIdPtr SessionIdFromName = CreateSessionIdFromString(SessionPair.Key.ToString());
		if (SessionIdFromName.IsValid() && *SessionIdFromName == SessionId)
		{
			SearchResult.Session = Session;
			SearchResult.PingInMs = 0;
			bFound = true;
			UE_LOG(LogOnlineICE, Log, TEXT("Found session by name-derived ID: %s"), *SessionPair.Key.ToString());
			break;
		}
	}

	if (!bFound)
	{
		UE_LOG(LogOnlineICE, Log, TEXT("Session not found by ID: %s"), *SessionId.ToString());
	}

	// Execute completion delegate
	CompletionDelegate.ExecuteIfBound(0, bFound, SearchResult);
	return true;
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

	// Gather local ICE candidates for connection
	if (ICEAgent.IsValid())
	{
		UE_LOG(LogOnlineICE, Log, TEXT("Gathering ICE candidates for joining session '%s'"), *SessionName.ToString());
		bool bGathered = ICEAgent->GatherCandidates();
		
		if (bGathered)
		{
			TArray<FICECandidate> LocalCandidates = ICEAgent->GetLocalCandidates();
			UE_LOG(LogOnlineICE, Log, TEXT("Gathered %d ICE candidates for joining"), LocalCandidates.Num());
			
			// Log candidates for debugging/manual signaling
			for (const FICECandidate& Candidate : LocalCandidates)
			{
				UE_LOG(LogOnlineICE, Verbose, TEXT("  %s"), *Candidate.ToString());
			}
			
			// Automatically send answer candidates via signaling to host
			if (SignalingInterface.IsValid() && SignalingInterface->IsActive())
			{
				// Get host peer ID from session info if available
				FString HostPeerId;
				if (DesiredSession.Session.OwningUserId.IsValid())
				{
					HostPeerId = DesiredSession.Session.OwningUserId->ToString();
				}
				
				SendLocalCandidates(SessionName.ToString(), HostPeerId);
			}
		}
		else
		{
			UE_LOG(LogOnlineICE, Warning, TEXT("Failed to gather ICE candidates for joining session '%s'"), *SessionName.ToString());
		}
	}

	TriggerOnJoinSessionCompleteDelegates(SessionName, EOnJoinSessionCompleteResult::Success);
	return true;
}

bool FOnlineSessionICE::JoinSession(const FUniqueNetId& PlayerId, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	return JoinSession(0, SessionName, DesiredSession);
}

bool FOnlineSessionICE::FindFriendSession(int32 LocalUserNum, const FUniqueNetId& Friend)
{
	UE_LOG(LogOnlineICE, Log, TEXT("FindFriendSession: Looking for sessions with friend %s"), *Friend.ToString());

	TArray<FOnlineSessionSearchResult> Results;

	// In a production environment, this would query a signaling server for friend's sessions
	// For local testing, search through local sessions to find ones the friend is in
	
	for (const auto& SessionPair : Sessions)
	{
		const FNamedOnlineSession& Session = SessionPair.Value;
		
		// Check if the friend is the owner of this session
		if (Session.OwningUserId.IsValid() && *Session.OwningUserId == Friend)
		{
			FOnlineSessionSearchResult SearchResult;
			SearchResult.Session = Session;
			SearchResult.PingInMs = 0;
			Results.Add(SearchResult);
			UE_LOG(LogOnlineICE, Log, TEXT("Found friend's session (owner): %s"), *SessionPair.Key.ToString());
			continue;
		}
		
		// Check if the friend is a registered player in this session
		for (const FUniqueNetIdRef& PlayerId : Session.RegisteredPlayers)
		{
			if (*PlayerId == Friend)
			{
				FOnlineSessionSearchResult SearchResult;
				SearchResult.Session = Session;
				SearchResult.PingInMs = 0;
				Results.Add(SearchResult);
				UE_LOG(LogOnlineICE, Log, TEXT("Found friend's session (player): %s"), *SessionPair.Key.ToString());
				break;
			}
		}
	}

	bool bSuccess = Results.Num() > 0;
	UE_LOG(LogOnlineICE, Log, TEXT("FindFriendSession completed: %d sessions found"), Results.Num());
	
	TriggerOnFindFriendSessionCompleteDelegates(LocalUserNum, bSuccess, Results);
	return true;
}

bool FOnlineSessionICE::FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend)
{
	// Delegate to the int32 version with default user num
	return FindFriendSession(0, Friend);
}

bool FOnlineSessionICE::FindFriendSession(const FUniqueNetId& LocalUserId, const TArray<FUniqueNetIdRef>& FriendList)
{
	UE_LOG(LogOnlineICE, Log, TEXT("FindFriendSession: Looking for sessions with %d friends"), FriendList.Num());

	TArray<FOnlineSessionSearchResult> Results;
	TSet<FName> AddedSessionNames; // Track added sessions to avoid duplicates

	// Search for sessions containing any of the friends in the list
	for (const FUniqueNetIdRef& Friend : FriendList)
	{
		for (const auto& SessionPair : Sessions)
		{
			const FNamedOnlineSession& Session = SessionPair.Value;
			
			// Skip if we already added this session
			if (AddedSessionNames.Contains(SessionPair.Key))
			{
				continue;
			}
			
			// Check if this friend is the owner
			if (Session.OwningUserId.IsValid() && *Session.OwningUserId == *Friend)
			{
				FOnlineSessionSearchResult SearchResult;
				SearchResult.Session = Session;
				SearchResult.PingInMs = 0;
				Results.Add(SearchResult);
				AddedSessionNames.Add(SessionPair.Key);
				UE_LOG(LogOnlineICE, Log, TEXT("Found friend's session (owner): %s"), *SessionPair.Key.ToString());
				continue;
			}
			
			// Check if this friend is a registered player
			for (const FUniqueNetIdRef& PlayerId : Session.RegisteredPlayers)
			{
				if (*PlayerId == *Friend)
				{
					FOnlineSessionSearchResult SearchResult;
					SearchResult.Session = Session;
					SearchResult.PingInMs = 0;
					Results.Add(SearchResult);
					AddedSessionNames.Add(SessionPair.Key);
					UE_LOG(LogOnlineICE, Log, TEXT("Found friend's session (player): %s"), *SessionPair.Key.ToString());
					break;
				}
			}
		}
	}

	bool bSuccess = Results.Num() > 0;
	UE_LOG(LogOnlineICE, Log, TEXT("FindFriendSession completed: %d sessions found"), Results.Num());
	
	TriggerOnFindFriendSessionCompleteDelegates(0, bSuccess, Results);
	return true;
}

bool FOnlineSessionICE::SendSessionInviteToFriend(int32 LocalUserNum, FName SessionName, const FUniqueNetId& Friend)
{
	UE_LOG(LogOnlineICE, Log, TEXT("SendSessionInviteToFriend: Session '%s' to friend %s"), *SessionName.ToString(), *Friend.ToString());

	// Verify session exists
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot send invite: Session '%s' not found"), *SessionName.ToString());
		return false;
	}

	// In a production environment, this would:
	// 1. Send the invite through a signaling/messaging service
	// 2. The friend would receive a notification
	// 3. The friend could then join using the session information
	
	// For local/testing purposes, we'll just log the invite
	// A real implementation would integrate with a messaging system or signaling server
	UE_LOG(LogOnlineICE, Log, TEXT("Session invite sent (local simulation) - Session: %s, Friend: %s"), 
		*SessionName.ToString(), *Friend.ToString());
	
	// In a real system, you would:
	// - Generate session connection info (ICE candidates, session ID)
	// - Send this info to the friend through your signaling mechanism
	// - The friend would use this to join the session
	
	return true;
}

bool FOnlineSessionICE::SendSessionInviteToFriend(const FUniqueNetId& LocalUserId, FName SessionName, const FUniqueNetId& Friend)
{
	return SendSessionInviteToFriend(0, SessionName, Friend);
}

bool FOnlineSessionICE::SendSessionInviteToFriends(int32 LocalUserNum, FName SessionName, const TArray<FUniqueNetIdRef>& Friends)
{
	UE_LOG(LogOnlineICE, Log, TEXT("SendSessionInviteToFriends: Session '%s' to %d friends"), *SessionName.ToString(), Friends.Num());

	// Verify session exists
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (!Session)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot send invites: Session '%s' not found"), *SessionName.ToString());
		return false;
	}

	if (Friends.Num() == 0)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("No friends specified for invite"));
		return false;
	}

	// Send invite to each friend
	bool bAllSucceeded = true;
	for (const FUniqueNetIdRef& Friend : Friends)
	{
		bool bSent = SendSessionInviteToFriend(LocalUserNum, SessionName, *Friend);
		if (!bSent)
		{
			bAllSucceeded = false;
			UE_LOG(LogOnlineICE, Warning, TEXT("Failed to send invite to friend: %s"), *Friend->ToString());
		}
	}

	UE_LOG(LogOnlineICE, Log, TEXT("Session invites sent to %d friends"), Friends.Num());
	return bAllSucceeded;
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

	// Build connection string with ICE information
	if (ICEAgent.IsValid() && ICEAgent->IsConnected())
	{
		// If ICE is connected, provide connection details
		TArray<FICECandidate> LocalCandidates = ICEAgent->GetLocalCandidates();
		if (LocalCandidates.Num() > 0)
		{
			// Use the first candidate for the connection string
			const FICECandidate& Candidate = LocalCandidates[0];
			ConnectInfo = FString::Printf(TEXT("ice://%s:%d"), *Candidate.Address, Candidate.Port);
			UE_LOG(LogOnlineICE, Verbose, TEXT("Connect string for session '%s': %s"), *SessionName.ToString(), *ConnectInfo);
			return true;
		}
	}

	// If ICE not connected or no candidates, provide a pending status
	ConnectInfo = FString::Printf(TEXT("ice://pending/%s"), *SessionName.ToString());
	return true;
}

bool FOnlineSessionICE::GetResolvedConnectString(const FOnlineSessionSearchResult& SearchResult, FName PortType, FString& ConnectInfo)
{
	// Try to extract connection information from the search result
	if (SearchResult.Session.SessionInfo.IsValid())
	{
		// In a production environment, session info would contain ICE candidates and connection details
		// For now, construct a basic connection string
		const FUniqueNetId& SessionId = SearchResult.Session.SessionInfo->GetSessionId();
		if (SessionId.IsValid())
		{
			ConnectInfo = FString::Printf(TEXT("ice://session/%s"), *SessionId.ToString());
			return true;
		}
	}

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
			GetSessionStateString(SessionPair.Value.SessionState));
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

FNamedOnlineSession* FOnlineSessionICE::AddNamedSession(FName SessionName, const FOnlineSessionSettings& SessionSettings)
{
	UE_LOG(LogOnlineICE, Log, TEXT("AddNamedSession: %s"), *SessionName.ToString());
	
	// Check if session already exists
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Session '%s' already exists"), *SessionName.ToString());
		return Session;
	}
	
	// Create and add new session
	FNamedOnlineSession& NewSession = Sessions.Add(SessionName, FNamedOnlineSession(SessionName, SessionSettings));
	return &NewSession;
}

FNamedOnlineSession* FOnlineSessionICE::AddNamedSession(FName SessionName, const FOnlineSession& Session)
{
	UE_LOG(LogOnlineICE, Log, TEXT("AddNamedSession from FOnlineSession: %s"), *SessionName.ToString());
	
	// Check if session already exists
	FNamedOnlineSession* ExistingSession = GetNamedSession(SessionName);
	if (ExistingSession)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Session '%s' already exists"), *SessionName.ToString());
		return ExistingSession;
	}
	
	// Create and add new session from FOnlineSession
	FNamedOnlineSession& NewSession = Sessions.Add(SessionName, FNamedOnlineSession(SessionName, Session));
	return &NewSession;
}

FUniqueNetIdPtr FOnlineSessionICE::CreateSessionIdFromString(const FString& SessionIdStr)
{
	// Use the identity interface to create a unique ID from the string
	if (Subsystem && Subsystem->GetIdentityInterface().IsValid())
	{
		return Subsystem->GetIdentityInterface()->CreateUniquePlayerId(SessionIdStr);
	}
	return nullptr;
}

void FOnlineSessionICE::Tick(float DeltaTime)
{
	// Periodic processing for sessions
	// In a real implementation, this would handle ICE keepalives, timeouts, etc.
	if (ICEAgent.IsValid())
	{
		ICEAgent->Tick(DeltaTime);
	}
	
	// Process signaling messages
	if (SignalingInterface.IsValid())
	{
		SignalingInterface->ProcessSignals();
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

void FOnlineSessionICE::OnSignalReceived(const FICESignalMessage& Message)
{
	UE_LOG(LogOnlineICE, Log, TEXT("Received signal from %s: Type=%d, Candidates=%d"), 
		*Message.SenderId, (int32)Message.Type, Message.Candidates.Num());
	
	if (!ICEAgent.IsValid())
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot process signal: ICE agent not initialized"));
		return;
	}
	
	// Add remote candidates to ICE agent
	for (const FICECandidate& Candidate : Message.Candidates)
	{
		ICEAgent->AddRemoteCandidate(Candidate);
		UE_LOG(LogOnlineICE, Verbose, TEXT("Added remote candidate: %s"), *Candidate.ToString());
	}
	
	// If we have candidates and this is an offer/answer, start connectivity checks
	if (Message.Type == EICESignalType::Offer || Message.Type == EICESignalType::Answer)
	{
		TArray<FICECandidate> LocalCandidates = ICEAgent->GetLocalCandidates();
		if (LocalCandidates.Num() > 0 && Message.Candidates.Num() > 0)
		{
			UE_LOG(LogOnlineICE, Log, TEXT("Starting ICE connectivity checks"));
			ICEAgent->StartConnectivityChecks();
		}
	}
}

void FOnlineSessionICE::SendLocalCandidates(const FString& SessionId, const FString& ReceiverId)
{
	if (!SignalingInterface.IsValid() || !SignalingInterface->IsActive())
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot send candidates: signaling not active"));
		return;
	}
	
	if (!ICEAgent.IsValid())
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("Cannot send candidates: ICE agent not initialized"));
		return;
	}
	
	// Get local candidates
	TArray<FICECandidate> LocalCandidates = ICEAgent->GetLocalCandidates();
	if (LocalCandidates.Num() == 0)
	{
		UE_LOG(LogOnlineICE, Warning, TEXT("No local candidates to send"));
		return;
	}
	
	// Create signaling message
	FICESignalMessage Message;
	Message.Type = ReceiverId.IsEmpty() ? EICESignalType::Offer : EICESignalType::Answer;
	Message.SessionId = SessionId;
	Message.SenderId = SignalingInterface->GetLocalPeerId();
	Message.ReceiverId = ReceiverId;
	Message.Candidates = LocalCandidates;
	
	// Send via signaling
	if (SignalingInterface->SendSignal(Message))
	{
		UE_LOG(LogOnlineICE, Log, TEXT("Sent %d candidates for session %s"), LocalCandidates.Num(), *SessionId);
	}
	else
	{
		UE_LOG(LogOnlineICE, Error, TEXT("Failed to send candidates for session %s"), *SessionId);
	}
}
