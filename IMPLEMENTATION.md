# OnlineSubsystemICE Implementation Details

This document provides detailed information about the implementation of OnlineSubsystemICE features.

## Overview

OnlineSubsystemICE provides a complete ICE-based P2P networking solution for Unreal Engine 5.6+, implementing the standard Online Subsystem interfaces with ICE protocol support for NAT traversal.

## Core Components

### 1. Session Management (`FOnlineSessionICE`)

The session interface handles all session-related operations including creation, joining, and matchmaking.

#### Features Implemented

##### Session Creation
- **Function**: `CreateSession()`
- **Description**: Creates a new game session with automatic ICE candidate gathering
- **ICE Integration**: Automatically gathers local ICE candidates (host, srflx, relay) upon session creation
- **Usage**:
  ```cpp
  IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
  FOnlineSessionSettings Settings;
  Settings.NumPublicConnections = 4;
  Settings.bShouldAdvertise = true;
  Sessions->CreateSession(0, FName("MySession"), Settings);
  ```

##### Session Joining
- **Function**: `JoinSession()`
- **Description**: Joins an existing session with ICE candidate preparation
- **ICE Integration**: Gathers local candidates for establishing connection with host
- **Usage**:
  ```cpp
  Sessions->JoinSession(0, FName("JoinedSession"), SearchResult);
  ```

##### Basic Matchmaking
- **Function**: `StartMatchmaking()`
- **Description**: Implements create-or-join matchmaking behavior
- **Behavior**: 
  - First searches for available sessions
  - If found, joins the first available session
  - If not found, creates a new session
- **Players**: Automatically registers all local players to the session
- **Usage**:
  ```cpp
  TArray<FUniqueNetIdRef> Players;
  Players.Add(LocalPlayerId);
  FOnlineSessionSettings Settings;
  TSharedRef<FOnlineSessionSearch> Search = MakeShared<FOnlineSessionSearch>();
  Sessions->StartMatchmaking(Players, FName("MatchSession"), Settings, Search);
  ```

##### Matchmaking Cancellation
- **Function**: `CancelMatchmaking()`
- **Description**: Cancels ongoing matchmaking and cleans up
- **Behavior**:
  - Stops any ongoing session search
  - Destroys pending/creating sessions
  - Triggers cancellation delegates

##### Session Discovery
- **Function**: `FindSessions()`
- **Description**: Searches for available sessions
- **Current Implementation**: Returns local advertised sessions
- **Production Note**: In production, this should query a signaling/matchmaking server
- **Usage**:
  ```cpp
  TSharedRef<FOnlineSessionSearch> Search = MakeShared<FOnlineSessionSearch>();
  Search->MaxSearchResults = 10;
  Sessions->FindSessions(0, Search);
  ```

##### Session Lookup by ID
- **Function**: `FindSessionById()`
- **Description**: Finds a specific session by its unique ID
- **Search Method**: 
  - Searches through local sessions
  - Compares against session owner ID
  - Compares against session name-derived ID
- **Usage**:
  ```cpp
  Sessions->FindSessionById(SearchingUserId, SessionId, FriendId, 
    FOnSingleSessionResultCompleteDelegate::CreateLambda([](int32 LocalUserNum, bool bSuccess, const FOnlineSessionSearchResult& Result) {
      // Handle result
    }));
  ```

##### Friend Session Discovery
- **Functions**: `FindFriendSession()` (3 overloads)
- **Description**: Locates sessions that friends are participating in
- **Search Criteria**:
  - Sessions where friend is the owner
  - Sessions where friend is a registered player
- **Multi-Friend Support**: Can search for sessions with any friend from a list
- **Usage**:
  ```cpp
  Sessions->FindFriendSession(0, FriendId);
  // Or with multiple friends
  TArray<FUniqueNetIdRef> Friends = { Friend1, Friend2, Friend3 };
  Sessions->FindFriendSession(LocalUserId, Friends);
  ```

##### Session Invitations
- **Functions**: `SendSessionInviteToFriend()`, `SendSessionInviteToFriends()`
- **Description**: Send session invitations to friends
- **Current Implementation**: Framework with logging (signaling server integration required)
- **Production Note**: Requires signaling/messaging service integration
- **Usage**:
  ```cpp
  Sessions->SendSessionInviteToFriend(0, SessionName, FriendId);
  // Or invite multiple friends
  TArray<FUniqueNetIdRef> Friends = { Friend1, Friend2, Friend3 };
  Sessions->SendSessionInviteToFriends(0, SessionName, Friends);
  ```

##### Connection Information
- **Function**: `GetResolvedConnectString()`
- **Description**: Retrieves connection information for a session
- **Returns**: 
  - ICE connection URI with address and port when connected
  - Pending URI with session name when not yet connected
- **Format**: `ice://address:port` or `ice://pending/sessionname`

##### Session State Management
- **Function**: `UpdateSession()`
- **Enhanced**: Added session state validation
- **Validation**: Prevents updates to destroying/ended sessions
- **Error Handling**: Comprehensive logging for invalid states

### 2. ICE Protocol (`FICEAgent`)

Handles low-level ICE operations including candidate gathering and connectivity checks.

#### Candidate Types Supported

1. **Host Candidates**: Local network interfaces
2. **Server Reflexive Candidates (srflx)**: Public addresses via STUN
3. **Relayed Candidates (relay)**: Relay addresses via TURN

#### Integration Points

- **Session Creation**: Automatic candidate gathering
- **Session Joining**: Candidate preparation for connection
- **Console Commands**: Manual candidate exchange for testing

### 3. Identity Management (`FOnlineIdentityICE`)

Provides player identification and authentication.

#### Features
- Auto-generated unique player IDs
- Custom credential support
- Privilege management

## Session Lifecycle with ICE

### Creating a Session (Host)

1. **Create Session**
   ```cpp
   Sessions->CreateSession(0, FName("MySession"), Settings);
   ```

2. **ICE Candidate Gathering**
   - Host candidates gathered from local interfaces
   - STUN requests sent to discover public IP
   - TURN allocation if configured
   - Candidates logged for manual exchange

3. **Start Session**
   ```cpp
   Sessions->StartSession(FName("MySession"));
   ```

4. **Exchange Candidates**
   - Host sends candidates to signaling server (or manually via console)
   - Clients receive host candidates

### Joining a Session (Client)

1. **Find Sessions**
   ```cpp
   Sessions->FindSessions(0, SearchSettings);
   ```

2. **Join Session**
   ```cpp
   Sessions->JoinSession(0, FName("JoinedSession"), SearchResult);
   ```

3. **ICE Candidate Gathering**
   - Client gathers local candidates
   - Candidates logged for manual exchange

4. **Exchange Candidates**
   - Client sends candidates to host via signaling server
   - Host receives client candidates

5. **Establish Connection**
   ```cpp
   Sessions->StartICEConnectivityChecks();
   ```

## Matchmaking Flow

### Create-or-Join Behavior

```cpp
// 1. Start matchmaking
Sessions->StartMatchmaking(Players, SessionName, Settings, SearchSettings);

// 2. System searches for available sessions
//    - If sessions found: Joins first available
//    - If no sessions: Creates new session

// 3. All local players registered automatically

// 4. Matchmaking complete delegate triggered
```

### Canceling Matchmaking

```cpp
Sessions->CancelMatchmaking(0, SessionName);
// - Cancels ongoing search
// - Destroys pending session
// - Cleans up resources
```

## Console Commands for Testing

The system provides console commands for manual ICE signaling:

```
ICE.HELP                          - Show all commands
ICE.LISTCANDIDATES                - Display local ICE candidates
ICE.SETREMOTEPEER <ip> <port>     - Set remote peer address
ICE.ADDCANDIDATE <candidate>      - Add remote ICE candidate
ICE.STARTCHECKS                   - Begin connectivity checks
ICE.STATUS                        - Show connection status
```

### Example Testing Workflow

**Host:**
```
ICE.LISTCANDIDATES
> candidate:1 1 UDP 2130706431 192.168.1.100 5000 typ host

ICE.SETREMOTEPEER 192.168.1.101 5001
ICE.STARTCHECKS
```

**Client:**
```
ICE.LISTCANDIDATES
> candidate:1 1 UDP 2130706431 192.168.1.101 5001 typ host

ICE.SETREMOTEPEER 192.168.1.100 5000
ICE.STARTCHECKS
```

## Production Deployment

### Required Components

1. **Signaling Server**: 
   - Exchange ICE candidates between peers
   - Manage session discovery
   - Handle matchmaking requests

2. **STUN Server**: 
   - Default: `stun.l.google.com:19302`
   - Or self-hosted coturn server

3. **TURN Server** (optional, for restrictive NATs):
   - Required for symmetric NAT
   - Requires credentials
   - Example: coturn server

### Configuration

```ini
[OnlineSubsystemICE]
STUNServer=stun.l.google.com:19302
TURNServer=turn.example.com:3478
TURNUsername=username
TURNCredential=password
```

### Signaling Server Integration Points

Replace manual candidate exchange with automated signaling:

1. **CreateSession**: Send local candidates to signaling server
2. **JoinSession**: Request host candidates from signaling server
3. **FindSessions**: Query signaling server for available sessions
4. **Matchmaking**: Coordinate through signaling server

## Error Handling

All functions include comprehensive error handling:

- **Validation**: Input validation for all parameters
- **State Checking**: Session state validation
- **Logging**: Detailed logging at Log, Warning, and Error levels
- **Delegates**: Proper success/failure delegate triggering
- **Resource Cleanup**: Automatic cleanup on failures

## Logging

Enable detailed logging:

```ini
[Core.Log]
LogOnlineICE=VeryVerbose
```

Or via console:
```
log LogOnlineICE VeryVerbose
```

## Best Practices

1. **Always check delegate results**: Session operations are asynchronous
2. **Handle network failures gracefully**: ICE may fail on restrictive networks
3. **Use TURN for production**: Ensures connectivity for all NAT types
4. **Implement signaling server**: Essential for automatic peer discovery
5. **Test with different network configurations**: Verify NAT traversal
6. **Monitor session state**: Track session lifecycle properly
7. **Clean up resources**: Destroy sessions when done

## Known Limitations

1. **Manual Signaling**: Currently requires manual candidate exchange or signaling server integration
2. **Local Session Discovery**: FindSessions only returns local sessions without signaling server
3. **Basic Matchmaking**: Simple create-or-join behavior; more sophisticated logic requires signaling server
4. **No DTLS**: Consider adding DTLS for encrypted P2P communication
5. **Single ICE Agent**: All sessions share one ICE agent instance

## Future Enhancements

1. **Signaling Server Reference Implementation**: WebSocket-based signaling server
2. **DTLS Support**: Encrypted P2P data channels
3. **Advanced Matchmaking**: Skill-based matching, region preferences
4. **Session Persistence**: Save/restore session state
5. **Multiple ICE Agents**: Per-session ICE agent instances
6. **IPv6 Support**: Full IPv6 candidate support
7. **Connection Quality Metrics**: Latency, packet loss, jitter monitoring

## References

- [RFC 8445 - ICE Protocol](https://tools.ietf.org/html/rfc8445)
- [RFC 5389 - STUN Protocol](https://tools.ietf.org/html/rfc5389)
- [RFC 5766 - TURN Protocol](https://tools.ietf.org/html/rfc5766)
- [Unreal Engine Online Subsystem](https://docs.unrealengine.com/en-US/ProgrammingAndScripting/Online/)

## Support

For issues and questions:
- GitHub: https://github.com/raydienull/subsystem-ice
- Documentation: See README.md, TESTING_GUIDE.md, QUICK_START.md
