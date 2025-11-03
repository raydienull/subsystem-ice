# Changelog

All notable changes to OnlineSubsystemICE will be documented in this file.

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
