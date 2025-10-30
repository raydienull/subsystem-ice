# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-10-30

### Added
- Initial release of OnlineSubsystemICE
- ICE protocol implementation (RFC 8445)
- STUN support for NAT traversal
- Basic TURN support framework
- OnlineSession interface implementation
  - CreateSession, JoinSession, DestroySession
  - Session state management
  - Player registration/unregistration
- OnlineIdentity interface implementation
  - Login/Logout functionality
  - Unique player ID generation
  - Auto-login support
- ICE Agent with candidate gathering
  - Host candidate gathering
  - Server reflexive candidate gathering (STUN)
  - Priority calculation
  - Connectivity checks
- Configurable STUN/TURN servers
- Comprehensive documentation
  - README with feature overview
  - Integration guide with C++ examples
  - Configuration examples
- Multi-platform support (Windows, Linux, Mac)
- Logging and debugging support

### Known Limitations
- Signaling mechanism not implemented (requires manual candidate exchange)
- TURN relay implementation is basic
- No DTLS encryption yet
- Matchmaking service integration pending

## [Unreleased]

### Planned Features
- Signaling server implementation
- Full TURN relay support (RFC 5766)
- DTLS encryption for secure P2P
- Matchmaking service integration
- Lobby system
- Advanced NAT traversal techniques
- IPv6 full support
- Connection quality monitoring
- Automatic reconnection
