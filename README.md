# OnlineSubsystemICE

A P2P connectivity plugin for Unreal Engine using ICE protocol (STUN/TURN) for NAT traversal.

## Overview

OnlineSubsystemICE provides an alternative to proprietary online subsystems like Steam for peer-to-peer connectivity. It uses standard protocols (ICE, STUN, TURN) to establish direct connections between players across different networks.

## Features

- ✅ **ICE Protocol Support**: RFC 8445-compliant Interactive Connectivity Establishment
- ✅ **STUN Support**: Automatic public IP discovery through STUN servers
- 🚧 **TURN Support**: Relay fallback for restrictive NAT (basic implementation)
- ✅ **OnlineSubsystem Integration**: Compatible with Unreal Engine's IOnlineSession and IOnlineIdentity interfaces
- ✅ **Multi-platform**: Windows, Linux, and Mac support
- ✅ **Configurable**: Easy-to-configure STUN/TURN servers

## Installation

### Method 1: Plugin Installation

1. Copy the `OnlineSubsystemICE` folder to your project's `Plugins` directory
2. Regenerate project files
3. Build your project

### Method 2: Engine Plugin

1. Copy the `OnlineSubsystemICE` folder to your engine's `Engine/Plugins` directory
2. Regenerate project files for your engine
3. Rebuild the engine

## Configuration

Add the following to your project's `DefaultEngine.ini`:

```ini
[OnlineSubsystem]
DefaultPlatformService=ICE

[OnlineSubsystemICE]
; STUN server for NAT traversal
STUNServer=stun.l.google.com:19302

; Optional: TURN server for relay
; TURNServer=turn.example.com:3478
; TURNUsername=username
; TURNCredential=password

; Enable IPv6 (optional)
bEnableIPv6=false
```

## Usage

### Basic Session Creation

```cpp
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"

// Get the Online Subsystem
IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get("ICE");
if (OnlineSubsystem)
{
    IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
    
    if (SessionInterface.IsValid())
    {
        // Create session settings
        FOnlineSessionSettings SessionSettings;
        SessionSettings.NumPublicConnections = 4;
        SessionSettings.bShouldAdvertise = true;
        SessionSettings.bAllowJoinInProgress = true;
        SessionSettings.bIsLANMatch = false;
        SessionSettings.bUsesPresence = true;
        SessionSettings.bAllowInvites = true;
        
        // Create session
        SessionInterface->CreateSession(0, FName("MySession"), SessionSettings);
    }
}
```

### Login (Identity)

```cpp
// Get identity interface
IOnlineIdentityPtr IdentityInterface = OnlineSubsystem->GetIdentityInterface();

if (IdentityInterface.IsValid())
{
    // Auto-login with generated ID
    IdentityInterface->AutoLogin(0);
    
    // Or login with custom credentials
    FOnlineAccountCredentials Credentials;
    Credentials.Type = "ICE";
    Credentials.Id = "MyPlayerID";
    Credentials.Token = "MyNickname";
    
    IdentityInterface->Login(0, Credentials);
}
```

### Join Session

```cpp
// Find sessions
TSharedRef<FOnlineSessionSearch> SearchSettings = MakeShared<FOnlineSessionSearch>();
SearchSettings->MaxSearchResults = 10;

SessionInterface->FindSessions(0, SearchSettings);

// In the OnFindSessionsComplete delegate:
void OnFindSessionsComplete(bool bWasSuccessful)
{
    if (bWasSuccessful && SearchSettings->SearchResults.Num() > 0)
    {
        // Join first found session
        SessionInterface->JoinSession(0, FName("MyJoinedSession"), SearchSettings->SearchResults[0]);
    }
}
```

## Architecture

### Core Components

1. **FOnlineSubsystemICE**: Main subsystem implementation
2. **FOnlineSessionICE**: Session management (create, join, destroy)
3. **FOnlineIdentityICE**: Player authentication and unique ID generation
4. **FICEAgent**: ICE protocol implementation with candidate gathering and connectivity checks

### ICE Protocol Flow

1. **Candidate Gathering**: 
   - Host candidates (local network interfaces)
   - Server reflexive candidates (via STUN)
   - Relayed candidates (via TURN, if configured)

2. **Candidate Exchange**: 
   - Candidates are exchanged through a signaling mechanism (to be implemented)

3. **Connectivity Checks**:
   - STUN binding requests between candidate pairs
   - Best path selection based on priority

4. **Connection Establishment**:
   - Once a candidate pair succeeds, data can be transmitted

## STUN Servers

The plugin comes pre-configured with Google's public STUN servers:

- stun.l.google.com:19302
- stun1.l.google.com:19302
- stun2.l.google.com:19302
- stun3.l.google.com:19302
- stun4.l.google.com:19302

You can also use other public STUN servers or host your own.

## TURN Servers

For networks with symmetric NAT or strict firewalls, TURN relay servers are needed. You'll need to:

1. Set up a TURN server (e.g., coturn)
2. Configure the server address and credentials in `DefaultEngine.ini`

Example:
```ini
TURNServer=turn.myserver.com:3478
TURNUsername=myuser
TURNCredential=mypassword
```

## Limitations & Future Work

- ⚠️ **Signaling**: Currently requires manual candidate exchange. A signaling server implementation is needed for production use
- ⚠️ **TURN**: Basic TURN implementation - full RFC 5766 compliance is in progress
- ⚠️ **Security**: Consider implementing DTLS for encrypted P2P communication
- ⚠️ **Matchmaking**: Basic implementation - full matchmaking service integration pending

## Debugging

Enable verbose logging in `DefaultEngine.ini`:

```ini
[Core.Log]
LogOnlineICE=VeryVerbose
```

You can also use the console command:
```
log LogOnlineICE VeryVerbose
```

## Dependencies

- Unreal Engine 4.27+ or Unreal Engine 5.x
- OnlineSubsystem module
- OnlineSubsystemUtils module
- Sockets module

## License

Copyright Epic Games, Inc. All Rights Reserved.

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues.

## References

- [RFC 8445 - Interactive Connectivity Establishment (ICE)](https://tools.ietf.org/html/rfc8445)
- [RFC 5389 - Session Traversal Utilities for NAT (STUN)](https://tools.ietf.org/html/rfc5389)
- [RFC 5766 - Traversal Using Relays around NAT (TURN)](https://tools.ietf.org/html/rfc5766)
- [Unreal Engine Online Subsystem Documentation](https://docs.unrealengine.com/en-US/ProgrammingAndScripting/Online/index.html)

## Support

For issues and questions, please use the GitHub issue tracker.
