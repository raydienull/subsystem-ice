# TURN Implementation Details

## Overview

OnlineSubsystemICE now includes a complete implementation of TURN (Traversal Using Relays around NAT) protocol as specified in RFC 5766. This document describes the implementation details and how TURN relay functionality works.

## TURN Protocol Basics

TURN is used when direct peer-to-peer connections fail due to restrictive NATs or firewalls. A TURN server acts as a relay, forwarding data between peers who cannot connect directly.

### Key Concepts

1. **Allocation**: A TURN client requests the server to allocate a relay address
2. **Permission**: The client creates permissions to allow traffic from specific peers
3. **ChannelBind**: Associates a channel number with a peer for efficient data transfer
4. **Refresh**: Periodically refreshes the allocation before it expires
5. **ChannelData**: Optimized packet format for sending data through the relay

## Architecture

### Class Structure

The TURN implementation is integrated into `FICEAgent`:

```cpp
class FICEAgent
{
private:
    // TURN-specific members
    FSocket* TURNSocket;                    // Persistent socket for TURN
    TSharedPtr<FInternetAddr> TURNServerAddr;  // TURN server address
    TSharedPtr<FInternetAddr> TURNRelayAddr;   // Allocated relay address
    int32 TURNAllocationLifetime;            // Allocation lifetime (seconds)
    float TimeSinceTURNRefresh;              // Time tracking for refresh
    uint16 TURNChannelNumber;                // Channel number (0x4000-0x7FFF)
    bool bTURNAllocationActive;              // Allocation state
    uint8 TURNTransactionID[12];             // Current transaction ID
    
    // TURN protocol methods
    bool PerformTURNCreatePermission(const FString& PeerAddress, int32 PeerPort);
    bool PerformTURNChannelBind(const FString& PeerAddress, int32 PeerPort, uint16 ChannelNumber);
    bool PerformTURNRefresh();
    bool SendDataThroughTURN(const uint8* Data, int32 Size, const FString& PeerAddress, int32 PeerPort);
    bool ReceiveDataFromTURN(uint8* Data, int32 MaxSize, int32& OutSize);
};
```

## TURN Flow

### 1. Allocation

When gathering relay candidates (`GatherRelayedCandidates()`):

```
Client -> TURN Server: Allocate Request (0x0003)
  - REQUESTED-TRANSPORT (UDP = 17)
  - USERNAME
  - [REALM, NONCE, MESSAGE-INTEGRITY if authenticated]

TURN Server -> Client: Allocate Success Response (0x0103)
  - XOR-RELAYED-ADDRESS: The allocated relay address
  - LIFETIME: How long the allocation is valid (typically 600 seconds)
  - [Or 401 Unauthorized with REALM and NONCE for authentication]
```

**Implementation Notes**:
- The TURN socket is kept open (not destroyed after allocation)
- Server address and relay address are cached for later use
- Lifetime is extracted and stored for refresh scheduling
- The allocation is marked as active

### 2. CreatePermission

When establishing connection with relay candidate (`StartConnectivityChecks()`):

```
Client -> TURN Server: CreatePermission Request (0x0008)
  - XOR-PEER-ADDRESS: Address of the peer to communicate with
  - USERNAME

TURN Server -> Client: CreatePermission Success Response (0x0108)
```

**Purpose**: Tells the TURN server to accept traffic from a specific peer address. Without permission, the server will drop packets from that peer.

### 3. ChannelBind

Immediately after CreatePermission:

```
Client -> TURN Server: ChannelBind Request (0x0009)
  - CHANNEL-NUMBER: 0x4000-0x7FFF
  - XOR-PEER-ADDRESS: Peer address
  - USERNAME

TURN Server -> Client: ChannelBind Success Response (0x0109)
```

**Purpose**: Associates a 16-bit channel number with a peer address. This enables the more efficient ChannelData format instead of Send/Data indications.

### 4. Data Transfer

#### Sending Data

When `SendData()` is called with a relayed candidate:

```cpp
// Format: [Channel# (2 bytes)] [Length (2 bytes)] [Application Data]
ChannelData packet -> TURN Server -> Peer
```

**ChannelData Format**:
- Bytes 0-1: Channel number (big-endian, 0x4000-0x7FFF)
- Bytes 2-3: Data length (big-endian)
- Bytes 4+: Application data

**Advantages over Send Indication**:
- Only 4 bytes overhead vs ~36 bytes for Send indication
- No STUN message parsing required
- Better performance for high-frequency data transfer

#### Receiving Data

When `ReceiveData()` is called:

```cpp
// Detect format by first two bits
if ((byte[0] & 0xC0) == 0x40) {
    // ChannelData format (bits 01)
    channel = (byte[0] << 8) | byte[1];
    length = (byte[2] << 8) | byte[3];
    data = byte[4..4+length];
}
```

### 5. Refresh

Managed automatically in `Tick()`:

```
// Refresh at 80% of lifetime
if (TimeSinceTURNRefresh >= TURNAllocationLifetime * 0.8) {
    Client -> TURN Server: Refresh Request (0x0004)
      - LIFETIME: Requested lifetime
      - USERNAME
    
    TURN Server -> Client: Refresh Success Response (0x0104)
      - LIFETIME: New lifetime granted
}
```

**Timing Strategy**:
- Refresh is triggered at 80% of allocation lifetime
- Example: 600s lifetime → refresh at 480s
- If refresh fails, retry after 30 seconds
- On success, timer resets to 0

**Why 80%?**
- Provides 20% buffer before expiration
- Allows time for retries if first attempt fails
- Balances between too-frequent refreshes and expiration risk

## Error Handling

### Allocation Errors

- **401 Unauthorized**: Request credentials, retry with MESSAGE-INTEGRITY
- **437 Allocation Mismatch**: Allocation expired or never existed
- **486 Allocation Quota Reached**: Server resource limit
- **508 Insufficient Capacity**: Server overloaded

### Permission/ChannelBind Errors

- **403 Forbidden**: Permission denied (bad peer address)
- **437 Allocation Mismatch**: No allocation exists
- **500 Server Error**: Internal server error

### Refresh Errors

- **437 Allocation Mismatch**: Allocation already expired
- Response with LIFETIME=0: Server wants to terminate allocation

## Performance Considerations

### Socket Management

- **One persistent socket**: Same socket for allocation, permissions, channels, refresh, and data
- **No socket recreation**: Avoids port changes and NAT binding issues
- **Proper cleanup**: Socket only destroyed when ICE agent is closed or allocation fails

### Data Transfer Efficiency

| Method | Overhead | Use Case |
|--------|----------|----------|
| ChannelData | 4 bytes | High-frequency data (preferred) |
| Send Indication | ~36 bytes | Single messages, not implemented yet |
| Data Indication | ~36 bytes | Received from peers not using channels |

### Refresh Strategy

- **Proactive refresh**: At 80% of lifetime
- **Background operation**: Non-blocking, happens in Tick()
- **Retry mechanism**: Retries on failure without disrupting connection
- **Lifetime adaptation**: Uses server-provided lifetime value

## Configuration

### DefaultEngine.ini

```ini
[OnlineSubsystemICE]
; TURN server configuration
TURNServer=turn.myserver.com:3478
TURNUsername=myuser
TURNCredential=mypassword
```

### Setting Up a TURN Server (coturn)

1. Install coturn:
   ```bash
   sudo apt-get install coturn
   ```

2. Configure `/etc/turnserver.conf`:
   ```ini
   listening-port=3478
   realm=myrealm.com
   user=myuser:mypassword
   lt-cred-mech
   no-tcp-relay
   ```

3. Start the server:
   ```bash
   sudo systemctl start coturn
   ```

4. Test with STUN/TURN test tools

## Debugging

### Enable Verbose Logging

```ini
[Core.Log]
LogOnlineICE=VeryVerbose
```

### What to Look For

**Successful TURN Flow**:
```
LogOnlineICE: Performing TURN allocation to: turn.example.com:3478
LogOnlineICE: TURN Allocate successful
LogOnlineICE: TURN allocated relay address: 203.0.113.1:50000
LogOnlineICE: TURN allocation lifetime: 600 seconds
LogOnlineICE: Added relay candidate: candidate:3 1 UDP 16777215 203.0.113.1 50000 typ relay
LogOnlineICE: Setting up TURN relay for communication
LogOnlineICE: Creating TURN permission for peer 198.51.100.1:54321
LogOnlineICE: TURN permission created for peer
LogOnlineICE: Binding TURN channel 0x4000 for peer 198.51.100.1:54321
LogOnlineICE: TURN channel 0x4000 bound successfully
```

**Refresh Cycle**:
```
LogOnlineICE: TURN allocation needs refresh (480.1 seconds elapsed, lifetime: 600)
LogOnlineICE: Refreshing TURN allocation
LogOnlineICE: TURN allocation refreshed successfully
LogOnlineICE: Updated TURN allocation lifetime: 600 seconds
```

**Data Transfer**:
```
LogOnlineICE: Sending data through TURN using ChannelData
[Data flows through relay]
```

### Common Issues

1. **"TURN username or credential not configured"**
   - Solution: Set TURNUsername and TURNCredential in config

2. **"TURN Allocate request timeout"**
   - Check TURN server is running and accessible
   - Verify firewall allows UDP port 3478
   - Test connectivity with `telnet turn.server.com 3478`

3. **"TURN allocation needs refresh" followed by failure**
   - TURN server may be overloaded or crashed
   - Check server logs for errors
   - Verify credentials are still valid

4. **"TURN permission creation failed"**
   - Peer address may be invalid
   - Allocation may have expired
   - Check TURN server supports permissions

## Testing TURN

### Local Testing with Symmetric NAT

1. Set up coturn server on a public IP
2. Configure two clients behind different NATs
3. Use restrictive firewall rules to force TURN usage:
   ```bash
   # Block direct P2P but allow TURN server
   iptables -A OUTPUT -p udp ! --dport 3478 -j DROP
   ```

### Monitoring TURN Server

```bash
# Watch coturn logs
tail -f /var/log/turn/turnserver.log

# Check active allocations
turnutils_uclient -v turn.server.com
```

### Network Analysis

Use Wireshark to analyze TURN traffic:
- Filter: `stun || turn`
- Look for:
  - Allocate Request/Response (0x0003/0x0103)
  - CreatePermission (0x0008/0x0108)
  - ChannelBind (0x0009/0x0109)
  - Refresh (0x0004/0x0104)
  - ChannelData (starts with 0x40-0x7F)

## Compliance

This implementation follows:
- **RFC 5766**: Traversal Using Relays around NAT (TURN)
- **RFC 5389**: Session Traversal Utilities for NAT (STUN)
- **RFC 2104**: HMAC-SHA1 for MESSAGE-INTEGRITY

## Future Enhancements

### Planned
- [ ] Send/Data indications as fallback (currently ChannelBind is required)
- [ ] TCP transport support
- [ ] TLS/DTLS for secure relay connections
- [ ] Multiple simultaneous peers (multiple permissions/channels)
- [ ] Bandwidth management and quotas

### Possible
- [ ] TURN REST API for credential generation
- [ ] Automatic TURN server discovery
- [ ] Connection quality metrics
- [ ] Relay selection based on latency

## References

- [RFC 5766 - TURN](https://tools.ietf.org/html/rfc5766)
- [RFC 5389 - STUN](https://tools.ietf.org/html/rfc5389)
- [RFC 8445 - ICE](https://tools.ietf.org/html/rfc8445)
- [coturn TURN server](https://github.com/coturn/coturn)
