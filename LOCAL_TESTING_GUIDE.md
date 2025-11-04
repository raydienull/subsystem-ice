# Guía de Testing Local con Señalización Automática

Esta guía explica cómo probar la conectividad P2P entre múltiples clientes en la misma máquina o red local usando el nuevo sistema de señalización automática.

## Resumen

OnlineSubsystemICE ahora incluye un sistema de señalización basado en archivos que permite el intercambio automático de candidatos ICE entre clientes sin necesidad de un servidor de señalización dedicado.

### ¿Qué ha cambiado?

**Antes (Manual):**
- Crear sesión en Cliente A
- Ejecutar `ICE.LISTCANDIDATES` en Cliente A
- Copiar candidatos manualmente
- Ejecutar `ICE.ADDCANDIDATE` en Cliente B
- Repetir en dirección inversa

**Ahora (Automático):**
- Crear sesión en Cliente A
- Unirse a sesión en Cliente B
- ✅ **Los candidatos se intercambian automáticamente**

## Requisitos

- Dos o más instancias del juego ejecutándose
- Acceso a un directorio compartido (por defecto: `ProjectSaved/ICESignaling`)
- Configuración básica de OnlineSubsystemICE

## Configuración Inicial

### 1. Configurar DefaultEngine.ini

```ini
[OnlineSubsystem]
DefaultPlatformService=ICE

[OnlineSubsystemICE]
STUNServer=stun.l.google.com:19302
; TURN opcional para NAT difíciles
; TURNServer=turn.example.com:3478
; TURNUsername=username
; TURNCredential=password
```

### 2. Verificar Directorio de Señalización

El sistema usa automáticamente:
```
<Tu Proyecto>/Saved/ICESignaling/
```

Este directorio se crea automáticamente cuando se inicia el sistema.

## Workflow de Testing Básico

### Escenario 1: Host y Cliente en la Misma Máquina

#### Paso 1: Iniciar Host (Instance A)

```cpp
// En Blueprint o C++
IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();

FOnlineSessionSettings Settings;
Settings.NumPublicConnections = 4;
Settings.bShouldAdvertise = true;
Settings.bIsLANMatch = false;
Settings.bUsesPresence = true;

Sessions->CreateSession(0, FName("GameSession"), Settings);
```

**Log esperado:**
```
LogOnlineICE: Gathering ICE candidates for session 'GameSession'
LogOnlineICE: Gathered 3 ICE candidates for session
LogOnlineICE: Sent 3 candidates for session GameSession
```

#### Paso 2: Iniciar Cliente (Instance B)

```cpp
// Buscar sesiones
TSharedRef<FOnlineSessionSearch> SearchSettings = MakeShared<FOnlineSessionSearch>();
SearchSettings->MaxSearchResults = 10;

Sessions->FindSessions(0, SearchSettings);

// En el delegate OnFindSessionsComplete:
if (SearchSettings->SearchResults.Num() > 0)
{
    Sessions->JoinSession(0, FName("JoinedSession"), SearchSettings->SearchResults[0]);
}
```

**Log esperado:**
```
LogOnlineICE: Gathering ICE candidates for joining session 'JoinedSession'
LogOnlineICE: Sent 3 candidates for session JoinedSession
LogOnlineICE: Received signal from <HostPeerId>: Type=0, Candidates=3
LogOnlineICE: Starting ICE connectivity checks
LogOnlineICE: ICE connection state changed: ConnectingDirect
```

#### Paso 3: Verificar Conexión

En ambas instancias, ejecutar en consola:
```
ICE.STATUS
```

**Resultado esperado:**
```
=== ICE Connection Status ===
Connected: Yes
Local Candidates: 3
  candidate:1 1 UDP 2130706431 192.168.1.100 5000 typ host
  candidate:2 1 UDP 1694498815 203.0.113.45 5001 typ srflx
Remote Peer: 192.168.1.101:5000
=============================
```

### Escenario 2: Dos Máquinas en la Misma Red

#### Configuración de Red Compartida

Opción A: **Carpeta Compartida Windows**
```
1. En Host: Compartir carpeta Saved/ICESignaling
2. En Cliente: Mapear unidad de red o usar UNC path
3. Configurar ambos para usar la misma ruta
```

Opción B: **Sincronización en Tiempo Real**
```
1. Usar Dropbox/Google Drive/OneDrive
2. Ambas máquinas sincronizando la misma carpeta
3. Puede tener latencia de 1-2 segundos
```

#### Ejecutar el Test

1. **Host (Máquina A):**
   - Crear sesión como en Escenario 1
   - Verificar que archivos .json aparecen en ICESignaling/

2. **Cliente (Máquina B):**
   - Buscar y unirse a sesión
   - Verificar que puede leer archivos del host
   - Verificar que escribe sus propios archivos

3. **Verificar Conexión:**
   - Usar `ICE.STATUS` en ambas máquinas
   - Verificar logs para mensajes "Received signal"

## Comandos de Consola Útiles

### Ver Estado de Señalización
```
ICE.SIGNALING
```
Muestra:
- Tipo de señalización activa
- Directorio usado
- Estado de conexión

### Ver Candidatos Locales
```
ICE.LISTCANDIDATES
```
Muestra todos los candidatos ICE locales (host, srflx, relay).

### Ver Estado de Conexión
```
ICE.STATUS
```
Muestra:
- Estado de conexión ICE
- Candidatos locales
- Peer remoto

### Forzar Checks de Conectividad (si es necesario)
```
ICE.STARTCHECKS
```

### Ayuda
```
ICE.HELP
```

## Troubleshooting

### Problema: "No candidates gathered"

**Síntomas:**
```
LogOnlineICE: Gathered 0 ICE candidates for session
```

**Soluciones:**
1. Verificar conexión a internet (necesaria para STUN)
2. Verificar que puerto UDP 19302 no está bloqueado
3. Probar servidor STUN alternativo en config
4. Verificar firewall/antivirus

### Problema: "Signal not received"

**Síntomas:**
```
LogOnlineICE: Sent 3 candidates for session GameSession
(No mensaje "Received signal" en el otro cliente)
```

**Soluciones:**
1. Verificar que ambos clientes usan el mismo directorio
2. Verificar permisos de lectura/escritura en directorio
3. Verificar sincronización si usas carpeta compartida
4. Esperar unos segundos (sincronización puede tardar)
5. Verificar logs para errores de archivo

### Problema: "Connectivity checks not starting"

**Síntomas:**
```
LogOnlineICE: Received signal from <PeerId>: Type=0, Candidates=3
(No se inician checks automáticamente)
```

**Soluciones:**
1. Ejecutar manualmente: `ICE.STARTCHECKS`
2. Verificar que ambos lados tienen candidatos remotos
3. Revisar logs para errores de ICE agent

### Problema: "Connection failed"

**Síntomas:**
```
LogOnlineICE: ICE connection state changed: Failed
```

**Soluciones:**
1. Verificar que no hay firewall bloqueando UDP
2. Si estás en NAT simétrico, configurar TURN
3. Verificar que IPs/puertos son correctos
4. Probar con candidatos host primero (LAN)

## Logging Detallado

Para debugging avanzado, habilitar logs verbosos:

### En DefaultEngine.ini
```ini
[Core.Log]
LogOnlineICE=VeryVerbose
LogNet=Verbose
```

### En Consola
```
log LogOnlineICE VeryVerbose
log LogNet Verbose
```

## Arquitectura del Sistema

### Flujo de Señalización

```
[Host Crea Sesión]
    ↓
[Recopila Candidatos ICE]
    ↓
[Escribe signal_*.json] ← Directorio Compartido
    ↓                                    ↓
[Cliente Lee Archivo] ← [Tick Procesa Señales]
    ↓
[Agrega Candidatos Remotos]
    ↓
[Envía Respuesta (Answer)]
    ↓
[Host Recibe Answer]
    ↓
[Ambos Inician Connectivity Checks]
    ↓
[Conexión P2P Establecida] ✅
```

### Formato de Mensajes

Los archivos de señalización usan formato JSON:

```json
{
  "type": "offer",
  "sessionId": "GameSession",
  "senderId": "A1B2C3D4-...",
  "receiverId": "",
  "timestamp": "2025-11-04T10:30:45.123Z",
  "candidates": [
    {
      "foundation": "1",
      "componentId": 1,
      "transport": "UDP",
      "priority": 2130706431,
      "address": "192.168.1.100",
      "port": 5000,
      "type": "host"
    }
  ],
  "metadata": {}
}
```

## Limitaciones Actuales

1. **Directorio Compartido**: Requiere acceso a mismo directorio
2. **Sin Servidor**: No hay servidor centralizado de matchmaking
3. **Latencia de Archivo**: Sincronización puede tomar 1-2 segundos
4. **Limpieza Manual**: Archivos antiguos se limpian automáticamente (5 min)

## Roadmap Futuro

### Version 2.1 (Próxima)
- Señalización HTTP/REST
- Servidor de señalización de referencia
- Discovery automático en LAN

### Version 3.0 (Planeada)
- WebSocket signaling
- Matchmaking con servidor
- DTLS encryption

## Ejemplos de Código Completos

### Host (C++)

```cpp
void AMyGameMode::CreateAndHostSession()
{
    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (!OnlineSub) return;
    
    IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
    if (!Sessions.IsValid()) return;
    
    // Bind completion delegate
    Sessions->AddOnCreateSessionCompleteDelegate_Handle(
        FOnCreateSessionCompleteDelegate::CreateUObject(
            this, &AMyGameMode::OnCreateSessionComplete)
    );
    
    // Configure session
    FOnlineSessionSettings Settings;
    Settings.NumPublicConnections = 4;
    Settings.bShouldAdvertise = true;
    Settings.bIsLANMatch = false;
    Settings.bUsesPresence = true;
    Settings.bAllowJoinInProgress = true;
    
    // Create session - candidates are automatically broadcast
    Sessions->CreateSession(0, FName("MyGameSession"), Settings);
}

void AMyGameMode::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("Session created! Waiting for players..."));
        
        // Start session
        IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
        if (OnlineSub)
        {
            IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
            if (Sessions.IsValid())
            {
                Sessions->StartSession(SessionName);
            }
        }
    }
}
```

### Cliente (C++)

```cpp
void AMyGameMode::FindAndJoinSession()
{
    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (!OnlineSub) return;
    
    IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
    if (!Sessions.IsValid()) return;
    
    // Bind delegates
    Sessions->AddOnFindSessionsCompleteDelegate_Handle(
        FOnFindSessionsCompleteDelegate::CreateUObject(
            this, &AMyGameMode::OnFindSessionsComplete)
    );
    
    // Create search settings
    SessionSearch = MakeShared<FOnlineSessionSearch>();
    SessionSearch->MaxSearchResults = 10;
    SessionSearch->bIsLanQuery = false;
    
    // Start search
    Sessions->FindSessions(0, SessionSearch.ToSharedRef());
}

void AMyGameMode::OnFindSessionsComplete(bool bWasSuccessful)
{
    if (!bWasSuccessful || SessionSearch->SearchResults.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No sessions found"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("Found %d sessions"), SessionSearch->SearchResults.Num());
    
    // Join first session
    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (OnlineSub)
    {
        IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
        if (Sessions.IsValid())
        {
            Sessions->AddOnJoinSessionCompleteDelegate_Handle(
                FOnJoinSessionCompleteDelegate::CreateUObject(
                    this, &AMyGameMode::OnJoinSessionComplete)
            );
            
            // Join session - candidates are automatically exchanged
            Sessions->JoinSession(0, FName("MyJoinedSession"), SessionSearch->SearchResults[0]);
        }
    }
}

void AMyGameMode::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    if (Result == EOnJoinSessionCompleteResult::Success)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully joined session!"));
        
        // Connection will be established automatically via ICE
        // Wait for OnConnectionStateChanged or check ICE.STATUS
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to join session: %d"), (int32)Result);
    }
}
```

## Preguntas Frecuentes

**P: ¿Funciona en diferentes plataformas?**
R: Sí, siempre que tengan acceso al mismo directorio de señalización.

**P: ¿Puedo usar esto en producción?**
R: La señalización por archivos es solo para testing. Para producción, implementa un servidor de señalización HTTP/WebSocket.

**P: ¿Cuántos clientes pueden conectarse?**
R: Depende de la configuración de `NumPublicConnections` en session settings.

**P: ¿Funciona a través de Internet?**
R: Sí, si:
1. STUN descubre IP pública correctamente
2. Directorio de señalización es accesible (ej: via Dropbox)
3. NAT permite conexiones P2P (usa TURN si no)

**P: ¿Puedo desactivar la señalización automática?**
R: Sí, los comandos manuales (`ICE.SETREMOTEPEER`, etc.) siguen funcionando.

## Soporte

Para problemas e issues:
- GitHub: https://github.com/raydienull/subsystem-ice/issues
- Documentación: README.md, IMPLEMENTATION.md, TESTING_GUIDE.md
