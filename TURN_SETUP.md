# TURN Server Setup Guide

This guide explains how to set up a TURN relay server for use with OnlineSubsystemICE.

## Why TURN?

TURN (Traversal Using Relays around NAT) is needed when:
- Direct P2P connection fails due to symmetric NAT
- Both peers are behind restrictive firewalls
- STUN alone cannot establish connectivity

**Note:** TURN servers relay all traffic, which increases latency and bandwidth costs. Use them only as a fallback.

## Option 1: Using Public TURN Servers

### Free Public TURN Servers

Some organizations provide free TURN services for testing:

⚠️ **Warning:** Free public TURN servers are:
- Not suitable for production
- May have rate limits
- May have privacy concerns
- May be unreliable

For production use, always host your own TURN server.

### Commercial TURN Providers

- **Twilio**: Offers TURN service as part of their platform
- **Xirsys**: Dedicated TURN/STUN provider
- **AWS**: Can host custom TURN servers on EC2

## Option 2: Self-Hosted TURN Server (Recommended)

### Using Coturn (Recommended)

Coturn is the most popular open-source TURN server.

#### Ubuntu/Debian Installation

```bash
# Update package list
sudo apt-get update

# Install coturn
sudo apt-get install coturn -y

# Enable coturn service
sudo sed -i 's/#TURNSERVER_ENABLED=1/TURNSERVER_ENABLED=1/' /etc/default/coturn
```

#### Configuration

Edit `/etc/turnserver.conf`:

```conf
# Basic configuration
listening-port=3478
tls-listening-port=5349

# External IP (replace with your server's public IP)
external-ip=YOUR_SERVER_PUBLIC_IP

# Realm (your domain)
realm=turn.yourdomain.com

# Authentication
# Option 1: Long-term credentials (static users)
lt-cred-mech
user=username:password

# Option 2: Short-term credentials (time-limited tokens)
# use-auth-secret
# static-auth-secret=your-secret-key

# Relay configuration
relay-ip=YOUR_SERVER_PUBLIC_IP
min-port=49152
max-port=65535

# Security
fingerprint
no-multicast-peers
no-loopback-peers

# Logging
log-file=/var/log/turnserver.log
verbose

# Performance tuning
max-bps=1000000
user-quota=100
total-quota=1000

# SSL/TLS certificates (optional but recommended)
# cert=/etc/ssl/turn-cert.pem
# pkey=/etc/ssl/turn-key.pem
```

#### Firewall Configuration

```bash
# Allow TURN ports
sudo ufw allow 3478/tcp
sudo ufw allow 3478/udp
sudo ufw allow 5349/tcp
sudo ufw allow 5349/udp

# Allow relay port range
sudo ufw allow 49152:65535/tcp
sudo ufw allow 49152:65535/udp

# Reload firewall
sudo ufw reload
```

#### Start Coturn

```bash
# Start service
sudo systemctl start coturn

# Enable auto-start on boot
sudo systemctl enable coturn

# Check status
sudo systemctl status coturn

# View logs
sudo tail -f /var/log/turnserver.log
```

#### Test TURN Server

```bash
# Install turnutils-uclient for testing
sudo apt-get install coturn-utils -y

# Test TURN server
turnutils_uclient -v -u username -w password YOUR_SERVER_PUBLIC_IP
```

### Using Docker

Quick setup with Docker:

```bash
# Pull coturn image
docker pull coturn/coturn

# Run coturn container
docker run -d \
  --name=coturn \
  --network=host \
  -v /etc/coturn/turnserver.conf:/etc/coturn/turnserver.conf \
  coturn/coturn
```

Docker Compose example (`docker-compose.yml`):

```yaml
version: '3'
services:
  coturn:
    image: coturn/coturn
    network_mode: host
    volumes:
      - ./turnserver.conf:/etc/coturn/turnserver.conf
      - ./logs:/var/log
    restart: unless-stopped
```

## Option 3: Using Cloud Providers

### AWS EC2 TURN Server

1. **Launch EC2 Instance**
   - Choose Ubuntu 20.04 LTS
   - Instance type: t3.small (minimum)
   - Open required ports in security group

2. **Security Group Configuration**
   - TCP: 3478, 5349
   - UDP: 3478, 5349
   - UDP: 49152-65535 (relay range)

3. **Install and Configure**
   Follow Ubuntu installation steps above

4. **Elastic IP**
   Assign an Elastic IP for consistent addressing

### Google Cloud Platform

1. **Create VM Instance**
   - Ubuntu 20.04
   - Machine type: e2-small
   - Allow HTTP/HTTPS traffic

2. **Firewall Rules**
   ```bash
   gcloud compute firewall-rules create turn-server \
     --allow tcp:3478,tcp:5349,udp:3478,udp:5349,udp:49152-65535
   ```

3. **Setup**
   Follow Ubuntu installation steps

### Azure

1. **Create Virtual Machine**
   - Ubuntu Server 20.04
   - Standard B1s or higher

2. **Network Security Group**
   Add inbound rules for TURN ports

3. **Setup**
   Follow Ubuntu installation steps

## Configuration in OnlineSubsystemICE

### DefaultEngine.ini

```ini
[OnlineSubsystemICE]
# Your TURN server
TURNServer=turn.yourdomain.com:3478
TURNUsername=username
TURNCredential=password

# STUN server (can use same server)
STUNServer=turn.yourdomain.com:3478
```

### Dynamic Credentials

For production, use time-limited credentials:

```cpp
// Generate time-limited TURN credentials
FString Username = FString::Printf(TEXT("%d:%s"), 
    FMath::FloorToInt(FDateTime::UtcNow().ToUnixTimestamp()) + 86400,
    *UserId);

// Calculate HMAC-SHA1 credential
FString Credential = GenerateHMACSHA1(Username, SharedSecret);

// Update configuration
GConfig->SetString(TEXT("OnlineSubsystemICE"), TEXT("TURNUsername"), *Username, GEngineIni);
GConfig->SetString(TEXT("OnlineSubsystemICE"), TEXT("TURNCredential"), *Credential, GEngineIni);
```

## Testing Your TURN Server

### Using Trickle ICE

1. Visit: https://webrtc.github.io/samples/src/content/peerconnection/trickle-ice/
2. Add your TURN server:
   ```
   turn:turn.yourdomain.com:3478
   username: username
   credential: password
   ```
3. Click "Gather candidates"
4. You should see relay candidates

### Command Line Test

```bash
# Test with turnutils
turnutils_uclient -v \
  -u username \
  -w password \
  turn.yourdomain.com
```

### From Unreal Engine

Enable verbose logging:
```ini
[Core.Log]
LogOnlineICE=VeryVerbose
```

Look for log entries:
```
LogOnlineICE: Gathering relayed candidates via TURN
LogOnlineICE: Added relay candidate: candidate:3 1 UDP 16777215 X.X.X.X YYYY typ relay
```

## Security Best Practices

### 1. Use TLS/DTLS

```conf
# turnserver.conf
tls-listening-port=5349
cert=/etc/ssl/turn-cert.pem
pkey=/etc/ssl/turn-key.pem
```

```ini
# DefaultEngine.ini
[OnlineSubsystemICE]
TURNServer=turns:turn.yourdomain.com:5349
```

### 2. Implement Rate Limiting

```conf
max-bps=1000000
user-quota=100
total-quota=1000
```

### 3. Use Short-Term Credentials

```conf
use-auth-secret
static-auth-secret=your-very-long-random-secret
```

### 4. Restrict Access

```conf
# Only allow specific IP ranges
allowed-peer-ip=0.0.0.0-255.255.255.255
denied-peer-ip=127.0.0.1-127.255.255.255
```

### 5. Monitor Usage

```bash
# Check active allocations
turnutils_peer -v -L YOUR_SERVER_IP

# Monitor logs
tail -f /var/log/turnserver.log
```

## Cost Estimation

### Bandwidth Costs

TURN relays all traffic between peers:
- 1 hour of voice chat (~64 kbps): ~28 MB
- 1 hour of game session (~128 kbps): ~56 MB
- 1 hour of video (~1 Mbps): ~450 MB

### Server Costs (Monthly Estimates)

**AWS EC2 (t3.small):**
- Instance: ~$15/month
- Bandwidth: $0.09/GB (first 10 TB)
- Elastic IP: Free (when attached)

**Google Cloud (e2-small):**
- Instance: ~$13/month
- Egress: $0.12/GB

**Azure (B1s):**
- Instance: ~$7.50/month
- Bandwidth: $0.087/GB

**Recommendation:** Start with t3.small or equivalent, scale based on usage.

## Scaling Considerations

### Single Server Limits

- ~500 concurrent connections
- ~50 Mbps sustained throughput

### Load Balancing

For high-scale deployments:

1. **Multiple TURN Servers**
   ```ini
   TURNServer=turn1.yourdomain.com:3478
   TURNServer=turn2.yourdomain.com:3478
   TURNServer=turn3.yourdomain.com:3478
   ```

2. **Geographic Distribution**
   - Deploy servers in multiple regions
   - Use DNS-based load balancing
   - Direct clients to nearest server

3. **Health Monitoring**
   ```bash
   # Setup monitoring with Prometheus/Grafana
   # Track: active allocations, bandwidth, CPU, memory
   ```

## Troubleshooting

### TURN Server Not Responding

```bash
# Check if service is running
sudo systemctl status coturn

# Check if ports are open
sudo netstat -tuln | grep 3478

# Test from external network
telnet turn.yourdomain.com 3478
```

### Authentication Failures

```
LogOnlineICE: TURN authentication failed
```

- Verify username/password in configuration
- Check turnserver.conf credentials
- Ensure no typos in DefaultEngine.ini

### No Relay Candidates

- Verify TURN server is reachable
- Check firewall rules
- Ensure UDP ports 49152-65535 are open
- Check coturn logs for errors

### High Bandwidth Usage

- Implement rate limiting
- Monitor for abuse
- Consider per-user quotas
- Use bandwidth alerts

## Advanced Configuration

### Prometheus Metrics

```conf
# Enable Prometheus exporter
prometheus
prometheus-port=9641
```

### Geographic Routing

Use GeoDNS to route clients to nearest TURN server:
```
turn-us-east.yourdomain.com
turn-us-west.yourdomain.com
turn-eu-west.yourdomain.com
turn-ap-southeast.yourdomain.com
```

### Redis for Shared State

For multi-server deployments:
```conf
redis-userdb="ip=127.0.0.1 dbname=0 password=turn connect_timeout=30"
```

## Monitoring and Maintenance

### Log Rotation

```bash
# /etc/logrotate.d/turnserver
/var/log/turnserver.log {
    daily
    rotate 7
    compress
    missingok
    notifempty
    postrotate
        /usr/bin/killall -HUP turnserver
    endscript
}
```

### Health Checks

```bash
#!/bin/bash
# health-check.sh
curl -f http://localhost:9641/metrics > /dev/null 2>&1
if [ $? -ne 0 ]; then
    systemctl restart coturn
    echo "TURN server restarted" | mail -s "TURN Alert" admin@yourdomain.com
fi
```

### Automated Backups

```bash
# Backup configuration
tar -czf /backup/turnserver-$(date +%Y%m%d).tar.gz /etc/turnserver.conf
```

## Additional Resources

- [Coturn Documentation](https://github.com/coturn/coturn)
- [RFC 5766 - TURN](https://tools.ietf.org/html/rfc5766)
- [WebRTC TURN Server Setup](https://webrtc.org/getting-started/turn-server)
- [ICE, STUN, TURN Explained](https://bloggeek.me/webrtc-turn/)
