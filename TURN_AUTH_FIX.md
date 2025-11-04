# TURN Authentication Fix

## Problem Description

The TURN server was returning a 401 Unauthorized error even after the client retried with proper credentials. The logs showed:

```
LogOnlineICE: Performing TURN allocation to: 51.68.44.244:3478
LogOnlineICE: Warning: TURN Error 401: Unauthorized
LogOnlineICE: TURN requires authentication, retrying with credentials
LogOnlineICE: Realm: nullptr.com, Nonce: d15ca5067d55e519
LogOnlineICE: Warning: TURN Error 401: Unauthorized
LogOnlineICE: Error: TURN Allocate failed - error 401 received
```

The issue was that the MESSAGE-INTEGRITY calculation was incorrect, causing the TURN server to reject the authentication.

## Root Cause

The bug was in the HMAC-SHA1 calculation for the MESSAGE-INTEGRITY attribute in `ICEAgent.cpp`, line 576 (before the fix).

### Incorrect Code (Before)
```cpp
CalculateHMACSHA1(TURNRequest.GetData(), MessageIntegrityOffset + 4, KeyMD5, 16, HMAC);
```

This calculated the HMAC over:
- STUN header (20 bytes)
- All attributes before MESSAGE-INTEGRITY
- MESSAGE-INTEGRITY attribute header (4 bytes) ❌ **This should NOT be included**

### Correct Code (After)
```cpp
CalculateHMACSHA1(TURNRequest.GetData(), MessageIntegrityOffset, KeyMD5, 16, HMAC);
```

This calculates the HMAC over:
- STUN header (20 bytes)
- All attributes before MESSAGE-INTEGRITY
- **Excludes MESSAGE-INTEGRITY attribute entirely** ✓

## RFC 5389 Specification

According to [RFC 5389 Section 15.4](https://tools.ietf.org/html/rfc5389#section-15.4):

> "For a request or indication message, the MESSAGE-INTEGRITY is computed over the message from the STUN header **up to, and including, the attribute preceding MESSAGE-INTEGRITY**."

The key phrase is "up to, and including, the attribute preceding MESSAGE-INTEGRITY", which clearly means:
- Include everything UP TO MESSAGE-INTEGRITY
- Do NOT include MESSAGE-INTEGRITY itself (neither header nor value)

## Technical Details

### Message Structure

A TURN Allocate request with authentication has this structure:

```
[STUN Header - 20 bytes]
  - Message Type (2 bytes): 0x0003 (Allocate Request)
  - Message Length (2 bytes): Total length of all attributes
  - Magic Cookie (4 bytes): 0x2112A442
  - Transaction ID (12 bytes): Random

[REQUESTED-TRANSPORT Attribute - 8 bytes]
  - Type (2 bytes): 0x0019
  - Length (2 bytes): 0x0004
  - Value (4 bytes): 17 (UDP)

[USERNAME Attribute - Variable]
  - Type (2 bytes): 0x0006
  - Length (2 bytes): Length of username
  - Value: Username string
  - Padding: To 4-byte boundary

[REALM Attribute - Variable]
  - Type (2 bytes): 0x0014
  - Length (2 bytes): Length of realm
  - Value: Realm string
  - Padding: To 4-byte boundary

[NONCE Attribute - Variable]
  - Type (2 bytes): 0x0015
  - Length (2 bytes): Length of nonce
  - Value: Nonce string
  - Padding: To 4-byte boundary

[MESSAGE-INTEGRITY Attribute - 24 bytes]
  - Type (2 bytes): 0x0008
  - Length (2 bytes): 0x0014 (20 bytes)
  - Value (20 bytes): HMAC-SHA1
```

### HMAC-SHA1 Calculation Process

1. **Build the message** with all attributes including MESSAGE-INTEGRITY header with placeholder value
2. **Set message length** in header to point to end of MESSAGE-INTEGRITY (for HMAC calculation)
3. **Calculate key**: `MD5(username:realm:password)`
4. **Calculate HMAC-SHA1** over message from start UP TO (but not including) MESSAGE-INTEGRITY attribute
5. **Write HMAC** into MESSAGE-INTEGRITY value field
6. **Send message** with the correct length field value

### Why This Matters

The TURN server validates the MESSAGE-INTEGRITY by:
1. Receiving the message
2. Extracting the HMAC from MESSAGE-INTEGRITY attribute
3. Recalculating HMAC using the same process (over everything EXCEPT MESSAGE-INTEGRITY)
4. Comparing the received HMAC with the calculated HMAC

If the client includes the MESSAGE-INTEGRITY header in the HMAC calculation, the HMACs won't match, and the server will reject the request with a 401 Unauthorized error.

## Testing

To test this fix, you need:

1. **TURN Server** configured with credentials
   - Example using coturn:
     ```bash
     turnserver -v -a -u user1:test1 -r nullptr.com
     ```

2. **Configure the plugin** in `DefaultEngine.ini`:
   ```ini
   [OnlineSubsystemICE]
   TURNServer=51.68.44.244:3478
   TURNUsername=user1
   TURNCredential=test1
   ```

3. **Run the game** and observe logs:
   - Before fix: "TURN Error 401: Unauthorized" (twice)
   - After fix: "TURN Allocate successful" and "TURN allocated relay address: X.X.X.X:XXXX"

## References

- [RFC 5389 - Session Traversal Utilities for NAT (STUN)](https://tools.ietf.org/html/rfc5389)
- [RFC 5766 - Traversal Using Relays around NAT (TURN)](https://tools.ietf.org/html/rfc5766)
- [RFC 2104 - HMAC: Keyed-Hashing for Message Authentication](https://tools.ietf.org/html/rfc2104)

## Related Files

- `Source/OnlineSubsystemICE/Private/ICEAgent.cpp` - TURN authentication implementation
- `Source/OnlineSubsystemICE/Public/ICEAgent.h` - ICE agent interface
- `Config/DefaultOnlineSubsystemICE.ini` - Configuration file
