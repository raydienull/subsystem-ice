# Changelog

All notable changes to OnlineSubsystemICE will be documented in this file.

## [2.2.0] - 2025-11-04

### Changed

#### Simplified Signaling Architecture
- **Removed ICESignalingInterface**: Eliminated abstract signaling interface for simpler, more direct approach
  - Removed `IICESignaling` abstract interface
  - Removed `FLocalFileSignaling` implementation
  - Removed `FICESignalMessage` struct and JSON serialization
  - Removed file-based signaling system
  - Removed JSON and JsonUtilities dependencies from Build.cs

#### Direct Delegate-Based Communication
- **Added Multicast Delegates**: Replaced signaling interface with Unreal-style delegates
  - `FOnLocalCandidatesReady`: Broadcasts when local ICE candidates are ready
  - `FOnRemoteCandidateReceived`: Broadcasts when remote candidates are received
  - Applications bind to these delegates to implement their own signaling mechanism

#### Benefits
- **Simpler Architecture**: Direct delegate pattern aligns with Unreal Engine methodology
- **More Flexible**: Applications can implement any signaling mechanism they need
- **Reduced Dependencies**: No JSON processing or file I/O for signaling
- **Better Performance**: No file system overhead or JSON parsing
- **Cleaner Code**: Removed ~350 lines of signaling infrastructure code

### Migration Guide
Applications previously using the automatic file-based signaling should:
1. Bind to `OnLocalCandidatesReady` delegate to receive local candidates
2. Send candidates to remote peer using your preferred method (network, REST API, WebSocket, etc.)
3. Call `AddRemoteICECandidate()` when receiving candidates from remote peer

## [2.1.0] - 2025-11-04

### Added

#### Automatic Signaling System
- **File-Based Signaling**: Implemented `IICESignaling` interface with `FLocalFileSignaling`
  - Automatic candidate exchange without manual console commands
  - JSON-based message format for cross-platform compatibility
  - Shared directory signaling (ProjectSaved/ICESignaling)
  - Automatic message cleanup (5 minute timeout)
  - Peer ID generation for message routing
  
#### Enhanced Session Workflow
- **Automatic Offer Broadcast**: `CreateSession` now automatically broadcasts ICE candidates
- **Automatic Answer Response**: `JoinSession` automatically sends answer with candidates
- **Signal Processing**: Integrated signaling message processing in session Tick
- **Connection Handler**: Automatic ICE connectivity checks when receiving remote candidates

#### Developer Experience
- **Simplified Testing**: Create → Join → Connected workflow
- **New Console Command**: `ICE.SIGNALING` to show signaling status
- **Updated Help**: `ICE.HELP` now shows automatic signaling info
- **Backward Compatible**: Manual commands still work for advanced scenarios

#### Documentation
- **LOCAL_TESTING_GUIDE.md**: Comprehensive guide for local testing with automatic signaling
  - Two-instance testing workflows
  - Shared folder setup for multi-machine testing
  - Complete troubleshooting section
  - Code examples for C++ and Blueprint
- **Updated README.md**: Added automatic signaling features
- **Updated CHANGELOG.md**: Version 2.1 features documented

### Changed
- Session interface now initializes signaling on construction
- Tick function now processes signaling messages
- Console help updated to reflect automatic features

### Technical Details

#### Architecture
- Clean separation: Signaling interface is independent of ICE agent
- Extensible: Easy to add HTTP/WebSocket signaling implementations
- Thread-safe: Proper locking for concurrent access
- Performance: Minimal overhead, processes signals only when needed

#### File Format
```json
{
  "type": "offer|answer|candidate",
  "sessionId": "SessionName",
  "senderId": "PeerGUID",
  "receiverId": "TargetPeerGUID",
  "candidates": [...],
  "metadata": {},
  "timestamp": "ISO8601"
}
```

### Migration Notes

No breaking changes. Existing code continues to work without modifications.

**To use automatic signaling:**
1. No code changes needed - it works automatically
2. Sessions will exchange candidates on create/join
3. Manual commands still available if needed

**To disable automatic signaling:**
- Use manual commands exclusively
- System falls back gracefully if signaling unavailable

## [2.0.0] - 2025-11-03

### Added

#### Session Management Features
- **Basic Matchmaking**: Implemented `StartMatchmaking()` with create-or-join behavior
  - Automatically searches for available sessions
  - Creates new session if none found, or joins existing session
  - Registers all local players automatically
- **Matchmaking Cancellation**: Implemented `CancelMatchmaking()` with proper cleanup
  - Cancels ongoing session searches
  - Destroys pending/creating sessions
  - Triggers appropriate completion delegates

#### Session Discovery
- **Enhanced FindSessions**: Returns local advertised sessions for testing
  - Filters by advertised flag and session state
  - Supports MaxSearchResults limit
  - Ready for signaling server integration
- **Session Lookup by ID**: Implemented `FindSessionById()`
  - Searches by session owner ID
  - Searches by session name-derived ID
  - Proper async delegate handling
- **Friend Session Discovery**: Implemented `FindFriendSession()` variants
  - Single friend search
  - Multiple friend search with deduplication
  - Searches both session owners and registered players

#### Session Invitations
- **Friend Invitations**: Implemented `SendSessionInviteToFriend()`
  - Framework for single friend invites
  - Validation of session existence
  - Ready for signaling server integration
- **Batch Invitations**: Implemented `SendSessionInviteToFriends()`
  - Send invites to multiple friends at once
  - Tracks success/failure per friend
  - Comprehensive error reporting

#### ICE Integration
- **Automatic Candidate Gathering**: 
  - CreateSession now gathers ICE candidates automatically
  - JoinSession prepares candidates for connection
  - Candidates logged for manual exchange or signaling
- **Enhanced Connection Strings**: Improved `GetResolvedConnectString()`
  - Returns meaningful ICE URIs with address and port
  - Provides pending status with session identification
  - Two variants for named sessions and search results

#### Error Handling & Validation
- **Session State Validation**: Enhanced `UpdateSession()`
  - Validates session state before updates
  - Prevents updates to destroying/ended sessions
  - Clear error messages for invalid operations
- **Comprehensive Input Validation**:
  - Parameter validation in all functions
  - Null checks for optional parameters
  - Array bounds checking
- **Improved Logging**:
  - Detailed logs at Log, Warning, and Error levels
  - Verbose logging for debugging
  - Success/failure tracking

#### Documentation
- **IMPLEMENTATION.md**: Complete feature documentation
  - Detailed API documentation
  - Usage examples for all features
  - Production deployment guidelines
  - Best practices and limitations
- **Updated README.md**: Version 2.0 feature highlights
- **Updated QUICK_START.md**: Reference to new documentation

### Fixed
- **Session State String Conversion**: Added `GetSessionStateString()` helper
  - Replaced non-existent `EOnlineSessionState::ToString()` calls
  - Safe conversion for all session states
- **Null Pointer Safety**: Enhanced `GetResolvedConnectString()`
  - Added null check for SessionInfo->GetSessionId()
  - Proper validation before dereferencing
- **Performance Optimization**: Improved `FindFriendSession()`
  - Use TSet for duplicate session tracking
  - Reduced complexity from O(N*M*P) to O(N*M)
  - More efficient session deduplication

### Technical Details

#### Code Quality
- All implementations follow Unreal Engine patterns
- Proper use of shared pointers and references
- Consistent error handling and delegate triggering
- Memory-safe operations throughout

#### Testing Support
- Console commands for manual testing
- Detailed logging for debugging
- Local session discovery for testing
- Framework ready for signaling server

#### Production Readiness
- Clear integration points for signaling server
- Documented production requirements
- STUN/TURN server configuration support
- Scalable architecture

## [1.0.0] - Initial Release

### Added
- Basic ICE protocol implementation
- STUN support for NAT traversal
- TURN support for relay
- Session creation and joining
- Identity management
- Console commands for testing
- Comprehensive documentation

### Supported Platforms
- Windows (Win64)
- Linux
- macOS

### Requirements
- Unreal Engine 5.6 or later
- OnlineSubsystem module
- OnlineSubsystemUtils module
- Sockets module

---

## Migration Guide

### From 1.0 to 2.0

No breaking changes. All new features are additions to existing functionality.

#### New Features Available
1. Use `StartMatchmaking()` instead of manual CreateSession/FindSessions workflow
2. Use `FindSessionById()` for targeted session searches
3. Use `FindFriendSession()` to locate friend sessions
4. Session creation now automatically gathers ICE candidates
5. Connection strings now provide meaningful ICE information

#### Recommended Updates
- Enable VeryVerbose logging to see new ICE candidate information
- Use new matchmaking APIs for simpler session management
- Implement signaling server for production deployment

## Future Roadmap

### Version 2.1 (Planned)
- Signaling server reference implementation
- WebSocket-based candidate exchange
- Automatic session discovery

### Version 3.0 (Planned)
- DTLS support for encrypted P2P
- Advanced matchmaking with skill-based matching
- IPv6 full support
- Connection quality metrics
- Session persistence

---

For detailed information about specific features, see [IMPLEMENTATION.md](IMPLEMENTATION.md).
