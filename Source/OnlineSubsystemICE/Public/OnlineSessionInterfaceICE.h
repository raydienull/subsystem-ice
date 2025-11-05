// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSubsystemICEPackage.h"

class FOnlineSubsystemICE;
enum class EICEConnectionState : uint8;

/**
 * Delegate for local ICE candidates ready notification
 * Params: SessionName, Candidates
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLocalCandidatesReady, FName, const TArray<struct FICECandidate>&);

/**
 * Delegate for remote ICE candidate received notification
 * This allows external systems to be notified when candidates are received
 * Params: SessionName, Candidate
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRemoteCandidateReceived, FName, const struct FICECandidate&);

/**
 * Session interface implementation for ICE
 * Handles session creation, joining, and P2P connection management
 */
class FOnlineSessionICE : public IOnlineSession
{
public:
	FOnlineSessionICE(FOnlineSubsystemICE* InSubsystem);
	virtual ~FOnlineSessionICE();

	// IOnlineSession Interface
	virtual bool CreateSession(int32 HostingPlayerNum, FName SessionName, const FOnlineSessionSettings& NewSessionSettings) override;
	virtual bool CreateSession(const FUniqueNetId& HostingPlayerId, FName SessionName, const FOnlineSessionSettings& NewSessionSettings) override;
	virtual bool StartSession(FName SessionName) override;
	virtual bool UpdateSession(FName SessionName, FOnlineSessionSettings& UpdatedSessionSettings, bool bShouldRefreshOnlineData = true) override;
	virtual bool EndSession(FName SessionName) override;
	virtual bool DestroySession(FName SessionName, const FOnDestroySessionCompleteDelegate& CompletionDelegate = FOnDestroySessionCompleteDelegate()) override;
	virtual bool IsPlayerInSession(FName SessionName, const FUniqueNetId& UniqueId) override;
	virtual bool StartMatchmaking(const TArray<FUniqueNetIdRef>& LocalPlayers, FName SessionName, const FOnlineSessionSettings& NewSessionSettings, TSharedRef<FOnlineSessionSearch>& SearchSettings) override;
	virtual bool CancelMatchmaking(int32 SearchingPlayerNum, FName SessionName) override;
	virtual bool CancelMatchmaking(const FUniqueNetId& SearchingPlayerId, FName SessionName) override;
	virtual bool FindSessions(int32 SearchingPlayerNum, const TSharedRef<FOnlineSessionSearch>& SearchSettings) override;
	virtual bool FindSessions(const FUniqueNetId& SearchingPlayerId, const TSharedRef<FOnlineSessionSearch>& SearchSettings) override;
	virtual bool FindSessionById(const FUniqueNetId& SearchingUserId, const FUniqueNetId& SessionId, const FUniqueNetId& FriendId, const FOnSingleSessionResultCompleteDelegate& CompletionDelegate) override;
	virtual bool CancelFindSessions() override;
	virtual bool PingSearchResults(const FOnlineSessionSearchResult& SearchResult) override;
	virtual bool JoinSession(int32 PlayerNum, FName SessionName, const FOnlineSessionSearchResult& DesiredSession) override;
	virtual bool JoinSession(const FUniqueNetId& PlayerId, FName SessionName, const FOnlineSessionSearchResult& DesiredSession) override;
	virtual bool FindFriendSession(int32 LocalUserNum, const FUniqueNetId& Friend) override;
	virtual bool FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend) override;
	virtual bool FindFriendSession(const FUniqueNetId& LocalUserId, const TArray<FUniqueNetIdRef>& FriendList) override;
	virtual bool SendSessionInviteToFriend(int32 LocalUserNum, FName SessionName, const FUniqueNetId& Friend) override;
	virtual bool SendSessionInviteToFriend(const FUniqueNetId& LocalUserId, FName SessionName, const FUniqueNetId& Friend) override;
	virtual bool SendSessionInviteToFriends(int32 LocalUserNum, FName SessionName, const TArray<FUniqueNetIdRef>& Friends) override;
	virtual bool SendSessionInviteToFriends(const FUniqueNetId& LocalUserId, FName SessionName, const TArray<FUniqueNetIdRef>& Friends) override;
	virtual bool GetResolvedConnectString(FName SessionName, FString& ConnectInfo, FName PortType) override;
	virtual bool GetResolvedConnectString(const FOnlineSessionSearchResult& SearchResult, FName PortType, FString& ConnectInfo) override;
	virtual FOnlineSessionSettings* GetSessionSettings(FName SessionName) override;
	virtual bool RegisterPlayer(FName SessionName, const FUniqueNetId& PlayerId, bool bWasInvited) override;
	virtual bool RegisterPlayers(FName SessionName, const TArray<FUniqueNetIdRef>& Players, bool bWasInvited = false) override;
	virtual bool UnregisterPlayer(FName SessionName, const FUniqueNetId& PlayerId) override;
	virtual bool UnregisterPlayers(FName SessionName, const TArray<FUniqueNetIdRef>& Players) override;
	virtual void RegisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnRegisterLocalPlayerCompleteDelegate& Delegate) override;
	virtual void UnregisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnUnregisterLocalPlayerCompleteDelegate& Delegate) override;
	virtual int32 GetNumSessions() override;
	virtual void DumpSessionState() override;
	virtual FNamedOnlineSession* GetNamedSession(FName SessionName) override;
	virtual void RemoveNamedSession(FName SessionName) override;
	virtual EOnlineSessionState::Type GetSessionState(FName SessionName) const override;
	virtual bool HasPresenceSession() override;
	virtual FNamedOnlineSession* AddNamedSession(FName SessionName, const FOnlineSessionSettings& SessionSettings) override;
	virtual FNamedOnlineSession* AddNamedSession(FName SessionName, const FOnlineSession& Session) override;
	virtual FUniqueNetIdPtr CreateSessionIdFromString(const FString& SessionIdStr) override;

	/**
	 * Tick function for handling periodic tasks
	 */
	void Tick(float DeltaTime);

	/**
	 * Set remote peer address manually (for testing)
	 */
	void SetRemotePeer(const FString& IPAddress, int32 Port);

	/**
	 * Add remote ICE candidate manually (for testing)
	 */
	void AddRemoteICECandidate(const FString& CandidateString);

	/**
	 * Get local ICE candidates
	 */
	TArray<FString> GetLocalICECandidates();

	/**
	 * Start ICE connectivity checks
	 */
	bool StartICEConnectivityChecks();

	/**
	 * Dump ICE connection status
	 */
	void DumpICEStatus(FOutputDevice& Ar);

	/**
	 * Delegate called when local ICE candidates are ready
	 * Applications can bind to this to send candidates to remote peers
	 */
	FOnLocalCandidatesReady OnLocalCandidatesReady;

	/**
	 * Delegate called when a remote candidate is received
	 * Applications can bind to this to monitor candidate reception
	 */
	FOnRemoteCandidateReceived OnRemoteCandidateReceived;

	/**
	 * Delegate called when ICE connection state changes for a session
	 * Params: SessionName, NewState
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnICEConnectionStateChanged, FName, EICEConnectionState);
	FOnICEConnectionStateChanged OnICEConnectionStateChanged;

private:
	/** Reference to the main subsystem */
	FOnlineSubsystemICE* Subsystem;

	/** Current session settings */
	TMap<FName, FNamedOnlineSession> Sessions;

	/** Current search object */
	TSharedPtr<FOnlineSessionSearch> CurrentSessionSearch;

	/** ICE agent for P2P connectivity */
	TSharedPtr<class FICEAgent> ICEAgent;

	/** Remote peer address for manual signaling */
	FString RemotePeerIP;
	int32 RemotePeerPort;
};
