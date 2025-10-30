# OnlineSubsystemICE Troubleshooting Guide

This guide helps you diagnose and resolve common issues with OnlineSubsystemICE.

## Table of Contents
- [Connection Issues](#connection-issues)
- [STUN Server Problems](#stun-server-problems)
- [Compilation Errors](#compilation-errors)
- [Runtime Errors](#runtime-errors)
- [NAT Traversal Issues](#nat-traversal-issues)
- [Logging and Debugging](#logging-and-debugging)

## Connection Issues

### Problem: Cannot establish P2P connection

**Symptoms:**
- Sessions are created but players cannot connect
- Timeout errors during connection attempts
- No candidates are gathered

**Solutions:**

1. **Check STUN Server Configuration**
   ```ini
   [OnlineSubsystemICE]
   STUNServer=stun.l.google.com:19302
   ```
   - Verify the STUN server address is correct
   - Try alternative STUN servers (stun1.l.google.com, stun2.l.google.com, etc.)
   - Test STUN server connectivity from command line

2. **Verify Firewall Settings**
   - Allow UDP traffic on the required ports
   - Check both Windows Firewall and router firewall
   - Temporarily disable firewall for testing (if safe to do so)

3. **Check Network Configuration**
   - Ensure you have internet connectivity
   - Verify UDP packets are not blocked
   - Test with different network configurations

4. **Enable Verbose Logging**
   ```ini
   [Core.Log]
   LogOnlineICE=VeryVerbose
   LogNet=VeryVerbose
   ```

### Problem: Connection works locally but not over internet

**Symptoms:**
- LAN connections succeed
- WAN connections fail
- Timeout errors with remote peers

**Solutions:**

1. **Configure TURN Server**
   
   Some NAT types (especially symmetric NAT) require a TURN relay server:
   
   ```ini
   [OnlineSubsystemICE]
   TURNServer=turn.yourserver.com:3478
   TURNUsername=yourusername
   TURNCredential=yourpassword
   ```

2. **Check NAT Type**
   
   Use online tools or console commands to determine your NAT type:
   - Full Cone NAT: Should work with STUN only
   - Restricted Cone NAT: Should work with STUN only
   - Port Restricted Cone NAT: May need TURN
   - Symmetric NAT: Requires TURN relay

3. **Verify Public IP Discovery**
   
   Check logs to ensure public IP was discovered:
   ```
   LogOnlineICE: STUN discovered public address: X.X.X.X:YYYY
   ```

## STUN Server Problems

### Problem: STUN request timeout

**Symptoms:**
```
LogOnlineICE: STUN request timeout
LogOnlineICE: Failed to gather server reflexive candidates
```

**Solutions:**

1. **Test STUN Server Connectivity**
   ```bash
   # Using a STUN client tool
   stunclient stun.l.google.com 19302
   ```

2. **Try Different STUN Servers**
   ```ini
   [OnlineSubsystemICE]
   # Try these alternatives
   STUNServer=stun1.l.google.com:19302
   STUNServer=stun2.l.google.com:19302
   STUNServer=stun.ekiga.net:3478
   ```

3. **Check UDP Port Availability**
   - Ensure UDP port 19302 (or your configured port) is not blocked
   - Check if your ISP blocks STUN traffic

### Problem: No candidates gathered

**Symptoms:**
```
LogOnlineICE: Gathered 0 ICE candidates
```

**Solutions:**

1. **Verify Network Interfaces**
   - Check that network adapters are enabled
   - Verify IP addresses are assigned
   - Ensure not in airplane mode

2. **Check Socket Subsystem**
   ```
   LogOnlineICE: Failed to get socket subsystem
   ```
   If you see this error, the platform's socket subsystem may not be initialized properly.

## Compilation Errors

### Problem: Missing OnlineSubsystem headers

**Error:**
```
fatal error: OnlineSubsystem.h: No such file or directory
```

**Solution:**
Add required modules to your Build.cs file:
```csharp
PublicDependencyModuleNames.AddRange(new string[] { 
    "OnlineSubsystem",
    "OnlineSubsystemICE",
    "OnlineSubsystemUtils"
});
```

### Problem: Unresolved external symbols

**Error:**
```
unresolved external symbol "class IOnlineSubsystem * __cdecl IOnlineSubsystem::Get"
```

**Solution:**
1. Add module dependencies in Build.cs
2. Regenerate project files
3. Clean and rebuild solution

### Problem: Plugin not loading

**Symptoms:**
- Plugin appears in plugin list but shows as not loaded
- "Failed to load" errors in output log

**Solutions:**

1. **Check Plugin Descriptor**
   Verify OnlineSubsystemICE.uplugin has correct syntax
   
2. **Verify Module Name**
   Ensure module name in .uplugin matches folder structure

3. **Check Dependencies**
   Ensure OnlineSubsystem and OnlineSubsystemUtils are available

## Runtime Errors

### Problem: Null pointer when accessing interfaces

**Error:**
```cpp
check(SessionInterface.IsValid()); // Fails
```

**Solutions:**

1. **Verify Subsystem Initialization**
   ```cpp
   IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get("ICE");
   if (OnlineSubsystem == nullptr)
   {
       UE_LOG(LogTemp, Error, TEXT("OnlineSubsystem ICE not found"));
   }
   ```

2. **Check Configuration**
   Ensure DefaultEngine.ini has correct configuration:
   ```ini
   [OnlineSubsystem]
   DefaultPlatformService=ICE
   ```

3. **Initialization Timing**
   Don't access online subsystem in constructor; use BeginPlay or similar

### Problem: Session creation fails

**Symptoms:**
```
OnCreateSessionComplete called with Success = false
```

**Solutions:**

1. **Check if Session Already Exists**
   ```cpp
   // Destroy existing session first
   SessionInterface->DestroySession(SessionName);
   ```

2. **Verify Player is Logged In**
   ```cpp
   IOnlineIdentityPtr Identity = OnlineSubsystem->GetIdentityInterface();
   if (Identity->GetLoginStatus(0) != ELoginStatus::LoggedIn)
   {
       Identity->AutoLogin(0);
   }
   ```

## NAT Traversal Issues

### Understanding NAT Types

| NAT Type | STUN Works | TURN Required | Difficulty |
|----------|-----------|---------------|------------|
| Full Cone | ✅ Yes | ❌ No | Easy |
| Restricted Cone | ✅ Yes | ❌ No | Easy |
| Port Restricted | ✅ Yes | ❌ No | Medium |
| Symmetric | ❌ No | ✅ Yes | Hard |

### Problem: Symmetric NAT connection failure

**Symptoms:**
- Both peers have symmetric NAT
- Direct connection impossible
- Need relay server

**Solution:**
Configure and use TURN relay server (see TURN_SETUP.md)

## Logging and Debugging

### Enable Detailed Logging

Add to DefaultEngine.ini:
```ini
[Core.Log]
LogOnlineICE=VeryVerbose
LogNet=VeryVerbose
LogNetTraffic=VeryVerbose
LogSockets=VeryVerbose
```

Or use console commands at runtime:
```
log LogOnlineICE VeryVerbose
log LogNet VeryVerbose
```

### Important Log Messages

**Successful Initialization:**
```
LogOnlineICE: OnlineSubsystemICE Module Started
LogOnlineICE: Initializing OnlineSubsystemICE
LogOnlineICE: OnlineSubsystemICE Initialized Successfully
```

**Candidate Gathering:**
```
LogOnlineICE: Gathering ICE candidates
LogOnlineICE: Added host candidate: candidate:1 1 UDP 2130706431 192.168.1.100 0 typ host
LogOnlineICE: STUN discovered public address: X.X.X.X:YYYY
LogOnlineICE: Added server reflexive candidate: candidate:2 1 UDP 1694498815 X.X.X.X YYYY typ srflx
LogOnlineICE: Gathered 2 ICE candidates
```

**Connection Establishment:**
```
LogOnlineICE: Starting ICE connectivity checks
LogOnlineICE: Selected candidate pair - Local: ..., Remote: ...
LogOnlineICE: ICE connection established
```

### Debug Console Commands

```
# Dump current session state
DumpSessionState

# Check online subsystem status
net.DumpOnlineSubsystem

# Network statistics
net.stats
```

### Using Network Profiler

1. Enable network profiler in Project Settings
2. Use Session Frontend to capture network traffic
3. Analyze ICE candidate exchange and connectivity checks

## Common Pitfalls

### 1. Forgetting to Login
Always login before creating/joining sessions:
```cpp
IdentityInterface->AutoLogin(0);
```

### 2. Incorrect Subsystem Name
Use "ICE" not "OnlineSubsystemICE":
```cpp
IOnlineSubsystem::Get("ICE"); // Correct
IOnlineSubsystem::Get("OnlineSubsystemICE"); // Wrong
```

### 3. Not Handling Async Callbacks
Always bind delegates before calling async functions:
```cpp
SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(...);
SessionInterface->CreateSession(...);
```

### 4. Firewall Blocking UDP
Many firewalls block UDP by default. Test with firewall disabled first.

### 5. Missing Configuration
Ensure DefaultEngine.ini has complete configuration:
```ini
[OnlineSubsystem]
DefaultPlatformService=ICE

[OnlineSubsystemICE]
STUNServer=stun.l.google.com:19302
```

## Getting Help

If you're still experiencing issues:

1. **Enable VeryVerbose Logging** and capture the full log
2. **Check GitHub Issues** for similar problems
3. **Provide Details** when opening new issues:
   - Unreal Engine version
   - Platform (Windows/Linux/Mac)
   - Network configuration
   - Relevant log excerpts
   - Steps to reproduce

## Performance Considerations

### Optimal Configuration

```ini
[OnlineSubsystemICE]
# Use fastest responding STUN server
STUNServer=stun.l.google.com:19302

# Only enable IPv6 if needed
bEnableIPv6=false
```

### Connection Timeout Settings

Adjust timeouts based on your network conditions:
- STUN timeout: 5 seconds (hardcoded in current version)
- Connectivity check timeout: Configurable in future versions

### Reducing Latency

1. Use geographically close STUN/TURN servers
2. Prefer host candidates when possible (LAN)
3. Optimize candidate priority calculation
4. Use direct connections over relays when possible

## Advanced Debugging

### Wireshark Packet Capture

1. Install Wireshark
2. Capture on active network interface
3. Filter for STUN traffic: `stun`
4. Analyze STUN requests/responses
5. Verify candidate gathering process

### STUN Message Analysis

Look for:
- Binding Request (0x0001)
- Binding Response (0x0101)
- XOR-MAPPED-ADDRESS attribute (0x0020)

### Network Path Testing

```bash
# Test UDP connectivity
nc -u stun.l.google.com 19302

# Trace route to STUN server
traceroute stun.l.google.com

# Check if UDP port is open
nmap -sU -p 19302 stun.l.google.com
```
