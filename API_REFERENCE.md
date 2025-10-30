# OnlineSubsystemICE API Reference

Complete API documentation for OnlineSubsystemICE.

## Table of Contents

- [Core Classes](#core-classes)
- [Session Management](#session-management)
- [Identity Management](#identity-management)
- [ICE Protocol](#ice-protocol)
- [Configuration](#configuration)
- [Delegates](#delegates)

## Core Classes

### FOnlineSubsystemICE

Main subsystem class that manages all online functionality.

#### Methods

##### `GetSessionInterface()`
```cpp
IOnlineSessionPtr GetSessionInterface() const;
```
Returns the session management interface.

**Returns:** Shared pointer to IOnlineSession implementation

**Example:**
```cpp
IOnlineSubsystem* OSS = IOnlineSubsystem::Get("ICE");
IOnlineSessionPtr Sessions = OSS->GetSessionInterface();
```

##### `GetIdentityInterface()`
```cpp
IOnlineIdentityPtr GetIdentityInterface() const;
```
Returns the identity management interface.

**Returns:** Shared pointer to IOnlineIdentity implementation

##### `Init()`
```cpp
bool Init();
```
Initializes the subsystem. Called automatically by the module.

**Returns:** `true` if initialization succeeded

##### `Shutdown()`
```cpp
bool Shutdown();
```
Shuts down the subsystem. Called automatically by the module.

**Returns:** `true` if shutdown succeeded

## Session Management

### IOnlineSession (FOnlineSessionICE)

Interface for managing multiplayer sessions.

#### Session Creation

##### `CreateSession()`
```cpp
bool CreateSession(
    int32 HostingPlayerNum,
    FName SessionName,
    const FOnlineSessionSettings& NewSessionSettings
);

bool CreateSession(
    const FUniqueNetId& HostingPlayerId,
    FName SessionName,
    const FOnlineSessionSettings& NewSessionSettings
);
```

Creates a new multiplayer session.

**Parameters:**
- `HostingPlayerNum` - Local player index hosting the session
- `HostingPlayerId` - Unique ID of hosting player
- `SessionName` - Name to identify this session
- `NewSessionSettings` - Settings for the new session

**Returns:** `true` if creation started successfully

**Delegates:** `OnCreateSessionComplete`

**Example:**
```cpp
FOnlineSessionSettings Settings;
Settings.NumPublicConnections = 4;
Settings.bShouldAdvertise = true;
Settings.bAllowJoinInProgress = true;
Settings.bIsLANMatch = false;
Settings.bUsesPresence = true;

SessionInterface->CreateSession(0, FName("MySession"), Settings);
```

##### `StartSession()`
```cpp
bool StartSession(FName SessionName);
```

Starts a previously created session, allowing gameplay to begin.

**Parameters:**
- `SessionName` - Name of the session to start

**Returns:** `true` if start succeeded

**Delegates:** `OnStartSessionComplete`

**Example:**
```cpp
SessionInterface->StartSession(FName("MySession"));
```

##### `UpdateSession()`
```cpp
bool UpdateSession(
    FName SessionName,
    FOnlineSessionSettings& UpdatedSessionSettings,
    bool bShouldRefreshOnlineData = true
);
```

Updates session settings.

**Parameters:**
- `SessionName` - Session to update
- `UpdatedSessionSettings` - New settings
- `bShouldRefreshOnlineData` - Whether to refresh online data

**Returns:** `true` if update started

**Delegates:** `OnUpdateSessionComplete`

#### Session Destruction

##### `EndSession()`
```cpp
bool EndSession(FName SessionName);
```

Ends an active session, preventing new joins but maintaining connection.

**Parameters:**
- `SessionName` - Session to end

**Returns:** `true` if end succeeded

**Delegates:** `OnEndSessionComplete`

##### `DestroySession()`
```cpp
bool DestroySession(
    FName SessionName,
    const FOnDestroySessionCompleteDelegate& CompletionDelegate
);
```

Completely destroys a session and disconnects all players.

**Parameters:**
- `SessionName` - Session to destroy
- `CompletionDelegate` - Optional delegate for completion

**Returns:** `true` if destruction started

**Delegates:** `OnDestroySessionComplete`

**Example:**
```cpp
SessionInterface->DestroySession(
    FName("MySession"),
    FOnDestroySessionCompleteDelegate::CreateLambda(
        [](FName SessionName, bool bWasSuccessful)
        {
            UE_LOG(LogTemp, Log, TEXT("Session destroyed: %s"), 
                bWasSuccessful ? TEXT("Success") : TEXT("Failed"));
        }
    )
);
```

#### Session Search

##### `FindSessions()`
```cpp
bool FindSessions(
    int32 SearchingPlayerNum,
    const TSharedRef<FOnlineSessionSearch>& SearchSettings
);

bool FindSessions(
    const FUniqueNetId& SearchingPlayerId,
    const TSharedRef<FOnlineSessionSearch>& SearchSettings
);
```

Searches for available sessions.

**Parameters:**
- `SearchingPlayerNum` - Local player performing search
- `SearchingPlayerId` - Unique ID of searching player
- `SearchSettings` - Search parameters and results container

**Returns:** `true` if search started

**Delegates:** `OnFindSessionsComplete`

**Example:**
```cpp
TSharedRef<FOnlineSessionSearch> SearchSettings = 
    MakeShared<FOnlineSessionSearch>();
SearchSettings->MaxSearchResults = 20;
SearchSettings->bIsLanQuery = false;

SessionInterface->FindSessions(0, SearchSettings);
```

##### `CancelFindSessions()`
```cpp
bool CancelFindSessions();
```

Cancels an ongoing session search.

**Returns:** `true` if cancellation succeeded

**Delegates:** `OnCancelFindSessionsComplete`

#### Session Joining

##### `JoinSession()`
```cpp
bool JoinSession(
    int32 PlayerNum,
    FName SessionName,
    const FOnlineSessionSearchResult& DesiredSession
);

bool JoinSession(
    const FUniqueNetId& PlayerId,
    FName SessionName,
    const FOnlineSessionSearchResult& DesiredSession
);
```

Joins an existing session.

**Parameters:**
- `PlayerNum` - Local player joining
- `PlayerId` - Unique ID of joining player
- `SessionName` - Local name for the joined session
- `DesiredSession` - Search result to join

**Returns:** `true` if join started

**Delegates:** `OnJoinSessionComplete`

**Example:**
```cpp
// After FindSessions completes
if (SearchSettings->SearchResults.Num() > 0)
{
    SessionInterface->JoinSession(
        0,
        FName("JoinedSession"),
        SearchSettings->SearchResults[0]
    );
}
```

#### Player Management

##### `RegisterPlayer()`
```cpp
bool RegisterPlayer(
    FName SessionName,
    const FUniqueNetId& PlayerId,
    bool bWasInvited
);
```

Registers a player with a session.

**Parameters:**
- `SessionName` - Session to register with
- `PlayerId` - Player to register
- `bWasInvited` - Whether player was invited

**Returns:** `true` if registration succeeded

**Delegates:** `OnRegisterPlayersComplete`

##### `UnregisterPlayer()`
```cpp
bool UnregisterPlayer(
    FName SessionName,
    const FUniqueNetId& PlayerId
);
```

Unregisters a player from a session.

**Parameters:**
- `SessionName` - Session to unregister from
- `PlayerId` - Player to unregister

**Returns:** `true` if unregistration succeeded

**Delegates:** `OnUnregisterPlayersComplete`

#### Session Information

##### `GetSessionSettings()`
```cpp
FOnlineSessionSettings* GetSessionSettings(FName SessionName);
```

Gets the settings for a session.

**Parameters:**
- `SessionName` - Session to query

**Returns:** Pointer to session settings, or `nullptr` if not found

##### `GetNamedSession()`
```cpp
FNamedOnlineSession* GetNamedSession(FName SessionName);
```

Gets a named session.

**Parameters:**
- `SessionName` - Session to retrieve

**Returns:** Pointer to session, or `nullptr` if not found

##### `GetSessionState()`
```cpp
EOnlineSessionState::Type GetSessionState(FName SessionName) const;
```

Gets the current state of a session.

**Parameters:**
- `SessionName` - Session to query

**Returns:** Session state (Creating, Pending, InProgress, Ended, etc.)

**Example:**
```cpp
EOnlineSessionState::Type State = 
    SessionInterface->GetSessionState(FName("MySession"));

switch (State)
{
    case EOnlineSessionState::InProgress:
        UE_LOG(LogTemp, Log, TEXT("Session is active"));
        break;
    case EOnlineSessionState::NoSession:
        UE_LOG(LogTemp, Log, TEXT("Session not found"));
        break;
}
```

## Identity Management

### IOnlineIdentity (FOnlineIdentityICE)

Interface for player authentication and identification.

#### Authentication

##### `Login()`
```cpp
bool Login(
    int32 LocalUserNum,
    const FOnlineAccountCredentials& AccountCredentials
);
```

Logs in a player.

**Parameters:**
- `LocalUserNum` - Local player index
- `AccountCredentials` - Login credentials

**Returns:** `true` if login started

**Delegates:** `OnLoginComplete`, `OnLoginStatusChanged`

**Example:**
```cpp
FOnlineAccountCredentials Credentials;
Credentials.Type = TEXT("ICE");
Credentials.Id = TEXT("MyPlayerID");
Credentials.Token = TEXT("MyNickname");

IdentityInterface->Login(0, Credentials);
```

##### `AutoLogin()`
```cpp
bool AutoLogin(int32 LocalUserNum);
```

Automatically logs in a player with generated credentials.

**Parameters:**
- `LocalUserNum` - Local player index

**Returns:** `true` if auto-login started

**Example:**
```cpp
IdentityInterface->AutoLogin(0);
```

##### `Logout()`
```cpp
bool Logout(int32 LocalUserNum);
```

Logs out a player.

**Parameters:**
- `LocalUserNum` - Local player to logout

**Returns:** `true` if logout succeeded

**Delegates:** `OnLogoutComplete`, `OnLoginStatusChanged`

#### Player Information

##### `GetUniquePlayerId()`
```cpp
FUniqueNetIdPtr GetUniquePlayerId(int32 LocalUserNum) const;
```

Gets the unique ID for a local player.

**Parameters:**
- `LocalUserNum` - Local player index

**Returns:** Unique player ID, or `nullptr` if not logged in

**Example:**
```cpp
FUniqueNetIdPtr PlayerId = IdentityInterface->GetUniquePlayerId(0);
if (PlayerId.IsValid())
{
    UE_LOG(LogTemp, Log, TEXT("Player ID: %s"), *PlayerId->ToString());
}
```

##### `GetPlayerNickname()`
```cpp
FString GetPlayerNickname(int32 LocalUserNum) const;
FString GetPlayerNickname(const FUniqueNetId& UserId) const;
```

Gets a player's nickname.

**Parameters:**
- `LocalUserNum` - Local player index
- `UserId` - Player's unique ID

**Returns:** Player nickname

##### `GetLoginStatus()`
```cpp
ELoginStatus::Type GetLoginStatus(int32 LocalUserNum) const;
ELoginStatus::Type GetLoginStatus(const FUniqueNetId& UserId) const;
```

Gets the login status of a player.

**Returns:** Login status (NotLoggedIn, UsingLocalProfile, LoggedIn)

**Example:**
```cpp
ELoginStatus::Type Status = IdentityInterface->GetLoginStatus(0);
if (Status == ELoginStatus::LoggedIn)
{
    // Player is logged in
}
```

## ICE Protocol

### FICEAgent

Low-level ICE protocol implementation.

#### Candidate Management

##### `GatherCandidates()`
```cpp
bool GatherCandidates();
```

Gathers ICE candidates (host, server reflexive, relay).

**Returns:** `true` if gathering succeeded

##### `GetLocalCandidates()`
```cpp
TArray<FICECandidate> GetLocalCandidates() const;
```

Gets all gathered local candidates.

**Returns:** Array of ICE candidates

##### `AddRemoteCandidate()`
```cpp
void AddRemoteCandidate(const FICECandidate& Candidate);
```

Adds a remote candidate received from peer.

**Parameters:**
- `Candidate` - Remote candidate to add

#### Connection

##### `StartConnectivityChecks()`
```cpp
bool StartConnectivityChecks();
```

Starts ICE connectivity checks with remote candidates.

**Returns:** `true` if checks started successfully

##### `IsConnected()`
```cpp
bool IsConnected() const;
```

Checks if ICE connection is established.

**Returns:** `true` if connected

#### Data Transfer

##### `SendData()`
```cpp
bool SendData(const uint8* Data, int32 Size);
```

Sends data through the ICE connection.

**Parameters:**
- `Data` - Data buffer to send
- `Size` - Size of data in bytes

**Returns:** `true` if send succeeded

##### `ReceiveData()`
```cpp
bool ReceiveData(uint8* Data, int32 MaxSize, int32& OutSize);
```

Receives data from the ICE connection.

**Parameters:**
- `Data` - Buffer to receive data
- `MaxSize` - Maximum buffer size
- `OutSize` - Actual bytes received

**Returns:** `true` if data was received

### FICECandidate

Represents an ICE candidate (potential connection path).

#### Fields

```cpp
FString Foundation;           // Candidate foundation
int32 ComponentId;            // Component ID (1 for RTP, 2 for RTCP)
FString Transport;            // Transport protocol (UDP/TCP)
int32 Priority;               // Candidate priority
FString Address;              // IP address
int32 Port;                   // Port number
EICECandidateType Type;       // Host, ServerReflexive, or Relayed
FString RelatedAddress;       // Related address for reflexive/relay
int32 RelatedPort;            // Related port
```

#### Methods

##### `ToString()`
```cpp
FString ToString() const;
```

Converts candidate to SDP string format.

##### `FromString()`
```cpp
static FICECandidate FromString(const FString& CandidateString);
```

Parses candidate from SDP string.

## Configuration

### DefaultEngine.ini Settings

```ini
[OnlineSubsystem]
; Set ICE as the default platform service
DefaultPlatformService=ICE

[OnlineSubsystemICE]
; STUN server for NAT traversal
STUNServer=stun.l.google.com:19302

; TURN server for relay (optional)
; TURNServer=turn.example.com:3478
; TURNUsername=username
; TURNCredential=password

; Enable IPv6 support
bEnableIPv6=false
```

## Delegates

### Session Delegates

```cpp
// Session creation
FOnCreateSessionComplete
void Delegate(FName SessionName, bool bWasSuccessful)

// Session start
FOnStartSessionComplete
void Delegate(FName SessionName, bool bWasSuccessful)

// Session end
FOnEndSessionComplete
void Delegate(FName SessionName, bool bWasSuccessful)

// Session destroy
FOnDestroySessionComplete
void Delegate(FName SessionName, bool bWasSuccessful)

// Session search
FOnFindSessionsComplete
void Delegate(bool bWasSuccessful)

// Session join
FOnJoinSessionComplete
void Delegate(FName SessionName, EOnJoinSessionCompleteResult::Type Result)

// Player registration
FOnRegisterPlayersComplete
void Delegate(FName SessionName, const TArray<FUniqueNetIdRef>& Players, bool bWasSuccessful)

// Player unregistration
FOnUnregisterPlayersComplete
void Delegate(FName SessionName, const TArray<FUniqueNetIdRef>& Players, bool bWasSuccessful)
```

### Identity Delegates

```cpp
// Login complete
FOnLoginComplete
void Delegate(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)

// Logout complete
FOnLogoutComplete
void Delegate(int32 LocalUserNum, bool bWasSuccessful)

// Login status changed
FOnLoginStatusChanged
void Delegate(int32 LocalUserNum, ELoginStatus::Type OldStatus, ELoginStatus::Type NewStatus, const FUniqueNetId& NewId)
```

## Error Codes

### Session Join Results

```cpp
enum EOnJoinSessionCompleteResult::Type
{
    Success,                  // Successfully joined
    SessionIsFull,            // Session is at capacity
    SessionDoesNotExist,      // Session not found
    CouldNotRetrieveAddress,  // Failed to get connection info
    AlreadyInSession,         // Already in a session
    UnknownError              // Other error
};
```

### Login Status

```cpp
enum ELoginStatus::Type
{
    NotLoggedIn,              // Not logged in
    UsingLocalProfile,        // Using local profile
    LoggedIn                  // Logged in to online service
};
```

## Usage Patterns

### Complete Session Creation Flow

```cpp
// 1. Login
IdentityInterface->AutoLogin(0);

// 2. Wait for login completion
void OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, 
                     const FUniqueNetId& UserId, const FString& Error)
{
    if (bWasSuccessful)
    {
        // 3. Create session
        FOnlineSessionSettings Settings;
        Settings.NumPublicConnections = 4;
        SessionInterface->CreateSession(0, FName("MySession"), Settings);
    }
}

// 4. Wait for session creation
void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        // 5. Start session
        SessionInterface->StartSession(SessionName);
    }
}

// 6. Session is now active
void OnStartSessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        // Session ready for gameplay
    }
}
```

### Complete Session Join Flow

```cpp
// 1. Find sessions
TSharedRef<FOnlineSessionSearch> SearchSettings = 
    MakeShared<FOnlineSessionSearch>();
SessionInterface->FindSessions(0, SearchSettings);

// 2. Wait for search completion
void OnFindSessionsComplete(bool bWasSuccessful)
{
    if (bWasSuccessful && SearchSettings->SearchResults.Num() > 0)
    {
        // 3. Join first session
        SessionInterface->JoinSession(0, FName("JoinedSession"), 
                                     SearchSettings->SearchResults[0]);
    }
}

// 4. Wait for join completion
void OnJoinSessionComplete(FName SessionName, 
                          EOnJoinSessionCompleteResult::Type Result)
{
    if (Result == EOnJoinSessionCompleteResult::Success)
    {
        // 5. Get connect string
        FString ConnectInfo;
        SessionInterface->GetResolvedConnectString(SessionName, ConnectInfo);
        
        // 6. Travel to session
        // Note: Uncomment and use the appropriate player controller
        // This will trigger client-side travel to the session
        // APlayerController* PC = GetWorld()->GetFirstPlayerController();
        // if (PC)
        // {
        //     PC->ClientTravel(ConnectInfo, TRAVEL_Absolute);
        // }
    }
}
```

## See Also

- [Integration Guide](INTEGRATION_GUIDE.md) - Detailed integration steps
- [Quick Start](QUICK_START.md) - Get started quickly
- [Troubleshooting](TROUBLESHOOTING.md) - Common issues and solutions
