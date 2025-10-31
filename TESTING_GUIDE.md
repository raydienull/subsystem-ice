# OnlineSubsystemICE Testing Guide

This guide explains how to test P2P connectivity using OnlineSubsystemICE with console commands for manual signaling.

## Overview

For local testing without a signaling server, OnlineSubsystemICE provides console commands that allow you to manually exchange ICE candidates and connection information between peers.

## Console Commands

### Available Commands

```
ICE.HELP                          - Show available commands
ICE.SETREMOTEPEER <ip> <port>     - Set remote peer address
ICE.ADDCANDIDATE <candidate>      - Add remote ICE candidate
ICE.LISTCANDIDATES                - List local ICE candidates
ICE.STARTCHECKS                   - Start connectivity checks
ICE.STATUS                        - Show connection status
```

## Testing Workflow

### Setup: Two Local Instances

1. **Launch Two Game Instances**
   - Package your game or use PIE (Play In Editor) with "Run Dedicated Server" disabled
   - Instance A: Host/Peer 1
   - Instance B: Client/Peer 2

### Step 1: Gather Local Candidates

On both instances, open the console (`~` key) and type:

```
ICE.LISTCANDIDATES
```

**Expected Output:**
```
ICE: Local candidates (2):
  candidate:1 1 UDP 2130706431 192.168.1.100 5000 typ host
  candidate:2 1 UDP 1694498815 203.0.113.45 5001 typ srflx
```

Copy the candidates from each instance.

### Step 2: Exchange Candidates

#### On Instance A (Host):
```
ICE.SETREMOTEPEER 192.168.1.101 5000
```
or add the full candidate:
```
ICE.ADDCANDIDATE candidate:1 1 UDP 2130706431 192.168.1.101 5000 typ host
```

#### On Instance B (Client):
```
ICE.SETREMOTEPEER 192.168.1.100 5000
```
or add the full candidate:
```
ICE.ADDCANDIDATE candidate:1 1 UDP 2130706431 192.168.1.100 5000 typ host
```

### Step 3: Start Connectivity Checks

On both instances:
```
ICE.STARTCHECKS
```

**Expected Output:**
```
ICE: Connectivity checks started
```

### Step 4: Verify Connection

Check the connection status on both instances:
```
ICE.STATUS
```

**Expected Output:**
```
=== ICE Connection Status ===
Connected: Yes
Local Candidates: 2
  candidate:1 1 UDP 2130706431 192.168.1.100 5000 typ host
  candidate:2 1 UDP 1694498815 203.0.113.45 5001 typ srflx
Remote Peer: 192.168.1.101:5000
=============================
```

## Testing Scenarios

### Scenario 1: LAN Testing (Same Network)

**Setup:**
- Two computers on the same local network
- Or two instances on the same computer using localhost

**Commands:**
```
# Instance A
ICE.LISTCANDIDATES
# Copy the host candidate (typ host)
# Note the IP and port

# Instance B
ICE.SETREMOTEPEER <Instance_A_IP> <Instance_A_Port>
ICE.STARTCHECKS

# Instance A
ICE.SETREMOTEPEER <Instance_B_IP> <Instance_B_Port>
ICE.STARTCHECKS
```

### Scenario 2: WAN Testing (Different Networks)

**Setup:**
- Two computers on different networks
- Both need to perform STUN to get public IP

**Commands:**
```
# Both instances
ICE.LISTCANDIDATES
# Note the server reflexive candidate (typ srflx) - this is your public IP

# Exchange the srflx candidates
# Instance A
ICE.ADDCANDIDATE candidate:2 1 UDP 1694498815 <Instance_B_Public_IP> <Port> typ srflx

# Instance B
ICE.ADDCANDIDATE candidate:2 1 UDP 1694498815 <Instance_A_Public_IP> <Port> typ srflx

# Both instances
ICE.STARTCHECKS
```

### Scenario 3: TURN Relay Testing

**Prerequisites:**
- TURN server configured in DefaultEngine.ini

**Configuration (DefaultEngine.ini):**
```ini
[OnlineSubsystemICE]
STUNServer=stun.l.google.com:19302
TURNServer=turn.example.com:3478
TURNUsername=testuser
TURNCredential=testpass
```

**Commands:**
```
# Both instances
ICE.LISTCANDIDATES
# You should see relay candidates (typ relay) if TURN is working

# Exchange relay candidates
# Instance A
ICE.ADDCANDIDATE candidate:3 1 UDP 16777215 <Relay_IP> <Relay_Port> typ relay

# Instance B  
ICE.ADDCANDIDATE candidate:3 1 UDP 16777215 <Relay_IP> <Relay_Port> typ relay

# Both instances
ICE.STARTCHECKS
```

## Troubleshooting

### No Candidates Generated

**Problem:**
```
ICE: Local candidates (0):
```

**Solution:**
- Check network connectivity
- Verify STUN server is reachable
- Check firewall settings
- Enable verbose logging: `log LogOnlineICE VeryVerbose`

### Connection Fails

**Problem:**
```
Connected: No
```

**Solutions:**

1. **Check Firewall:**
   - Ensure UDP ports are open
   - Temporarily disable firewall for testing

2. **Verify Candidates:**
   - Make sure you exchanged the correct IP addresses
   - Use host candidates for LAN testing
   - Use srflx candidates for WAN testing

3. **Enable Logging:**
   ```
   log LogOnlineICE VeryVerbose
   log LogNet VeryVerbose
   ```

4. **Check NAT Type:**
   - Symmetric NAT requires TURN relay
   - Use TURN server for difficult NAT scenarios

### STUN Request Timeout

**Problem:**
```
LogOnlineICE: STUN request timeout
```

**Solutions:**
- Check internet connectivity
- Try different STUN server
- Verify UDP port 19302 is not blocked

### TURN Allocation Failed

**Problem:**
```
LogOnlineICE: TURN Allocate failed - error response received
```

**Solutions:**
- Verify TURN server is running and accessible
- Check TURN username and credential
- Ensure TURN server supports UDP
- Check TURN server logs for authentication errors

## Example Testing Session

### Complete Example: Two Instances on Same Computer

**Instance A (Port 7777):**
```
PIE Session 1 (Console)
> ICE.LISTCANDIDATES
ICE: Local candidates (1):
  candidate:1 1 UDP 2130706431 127.0.0.1 7777 typ host

> ICE.SETREMOTEPEER 127.0.0.1 7778
ICE: Remote peer set to 127.0.0.1:7778

> ICE.STARTCHECKS
ICE: Connectivity checks started

> ICE.STATUS
=== ICE Connection Status ===
Connected: Yes
Local Candidates: 1
  candidate:1 1 UDP 2130706431 127.0.0.1 7777 typ host
Remote Peer: 127.0.0.1:7778
=============================
```

**Instance B (Port 7778):**
```
PIE Session 2 (Console)
> ICE.LISTCANDIDATES
ICE: Local candidates (1):
  candidate:1 1 UDP 2130706431 127.0.0.1 7778 typ host

> ICE.SETREMOTEPEER 127.0.0.1 7777
ICE: Remote peer set to 127.0.0.1:7777

> ICE.STARTCHECKS
ICE: Connectivity checks started

> ICE.STATUS
=== ICE Connection Status ===
Connected: Yes
Local Candidates: 1
  candidate:1 1 UDP 2130706431 127.0.0.1 7778 typ host
Remote Peer: 127.0.0.1:7777
=============================
```

## Automation Scripts

You can create Blueprint or C++ functions to automate the signaling process for testing:

### Blueprint Example

```cpp
// Create a Blueprint function library
UFUNCTION(BlueprintCallable, Category = "ICE Testing")
static void SetupTestConnection(FString RemoteIP, int32 RemotePort)
{
    if (GEngine && GEngine->GameViewport)
    {
        GEngine->Exec(GEngine->GameViewport->GetWorld(), 
            *FString::Printf(TEXT("ICE.SETREMOTEPEER %s %d"), *RemoteIP, RemotePort));
        GEngine->Exec(GEngine->GameViewport->GetWorld(), TEXT("ICE.STARTCHECKS"));
    }
}
```

## Performance Testing

### Latency Testing

Once connected, you can test latency using network profiling tools:

```
stat net
net pktlag=50
net pktloss=5
```

### Throughput Testing

Monitor data transfer rates:
```
stat netgraph
```

## Next Steps

Once you've validated local connectivity:
1. Implement a signaling server for automatic candidate exchange
2. Test with different NAT configurations
3. Add DTLS encryption for production use
4. Implement reconnection logic

## Additional Resources

- [ICE Protocol RFC 8445](https://tools.ietf.org/html/rfc8445)
- [STUN Protocol RFC 5389](https://tools.ietf.org/html/rfc5389)
- [TURN Protocol RFC 5766](https://tools.ietf.org/html/rfc5766)
- See TROUBLESHOOTING.md for common issues
- See TURN_SETUP.md for TURN server configuration
