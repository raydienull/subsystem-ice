# Guía de Testing Local con Delegados

Esta guía explica cómo probar la conectividad P2P entre múltiples clientes en la misma máquina o red local usando el sistema de delegados de OnlineSubsystemICE.

## Resumen

OnlineSubsystemICE usa **delegados multicast de Unreal** para notificar cuando los candidatos ICE están listos. Esto permite implementar cualquier mecanismo de señalización que necesites.

### Arquitectura

**Sistema de Delegados:**
- Cuando se crea/une a una sesión, el sistema recopila candidatos ICE
- El delegado `OnLocalCandidatesReady` se dispara con los candidatos locales
- Tu aplicación envía estos candidatos al peer remoto (REST API, WebSocket, etc.)
- Cuando recibes candidatos remotos, llamas a `AddRemoteICECandidate()`
- El delegado `OnRemoteCandidateReceived` se dispara al recibir candidatos

### Para Testing Local

**Opción 1: Comandos de Consola (Testing Manual):**
- Usar `ICE.LISTCANDIDATES` y `ICE.ADDCANDIDATE` para intercambio manual
- Ver [TESTING_GUIDE.md](TESTING_GUIDE.md) para detalles

**Opción 2: Implementar Señalización Personalizada:**
- Implementar sistema de señalización temporal para testing
- Puede ser tan simple como una carpeta compartida o un servidor HTTP local

## Requisitos

- Dos o más instancias del juego ejecutándose
- Implementación de señalización (manual o programática)
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

## Workflow de Testing Básico

### Opción 1: Testing Manual con Comandos de Consola

Esta es la forma más simple de testing local. Ver [TESTING_GUIDE.md](TESTING_GUIDE.md) para detalles completos.

**Pasos:**
1. Instancia A: Crear sesión y ejecutar `ICE.LISTCANDIDATES`
2. Copiar candidatos de A
3. Instancia B: Unirse a sesión y ejecutar `ICE.ADDCANDIDATE <candidato>` para cada candidato de A
4. Instancia B: Ejecutar `ICE.LISTCANDIDATES`
5. Copiar candidatos de B
6. Instancia A: Ejecutar `ICE.ADDCANDIDATE <candidato>` para cada candidato de B
7. Ambas instancias: Ejecutar `ICE.STARTCHECKS`

### Opción 2: Implementar Señalización con Delegados

Para automatizar el intercambio de candidatos, implementa tu propia lógica de señalización:

```cpp
// Obtener la interfaz de sesión
IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
IOnlineSessionPtr SessionInterface = OnlineSub->GetSessionInterface();
FOnlineSessionICE* SessionICE = static_cast<FOnlineSessionICE*>(SessionInterface.Get());

// Bind al delegado de candidatos locales listos
SessionICE->OnLocalCandidatesReady.AddLambda([](FName SessionName, const TArray<FICECandidate>& Candidates)
{
    UE_LOG(LogTemp, Log, TEXT("Candidatos locales listos para %s: %d candidatos"), *SessionName.ToString(), Candidates.Num());
    
    // Aquí implementas tu lógica de señalización
    // Ejemplos:
    // - Guardar en archivo compartido
    // - Enviar por HTTP/REST API
    // - Enviar por WebSocket
    // - Usar servidor de señalización personalizado
    
    for (const FICECandidate& Candidate : Candidates)
    {
        FString CandidateString = Candidate.ToString();
        // Enviar CandidateString al peer remoto
        MySignalingSystem->SendToRemotePeer(SessionName, CandidateString);
    }
});

// Bind al delegado de candidatos remotos recibidos
SessionICE->OnRemoteCandidateReceived.AddLambda([](FName SessionName, const FICECandidate& Candidate)
{
    UE_LOG(LogTemp, Log, TEXT("Candidato remoto recibido: %s"), *Candidate.ToString());
});

// Cuando recibes un candidato del peer remoto
void OnReceiveCandidateFromRemote(const FString& SessionName, const FString& CandidateString)
{
    SessionICE->AddRemoteICECandidate(CandidateString);
}
```

### Opción 3: Ejemplo Simple de Señalización con Archivo Compartido

Si quieres una implementación rápida para testing, puedes usar archivos compartidos:

```cpp
// Sistema simple de señalización basado en archivos
class FSimpleFileSignaling
{
public:
    FSimpleFileSignaling(const FString& SharedDirectory)
        : SignalingDir(SharedDirectory)
    {
        // Crear directorio si no existe
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (!PlatformFile.DirectoryExists(*SignalingDir))
        {
            PlatformFile.CreateDirectory(*SignalingDir);
        }
    }
    
    void SendCandidate(FName SessionName, const FString& CandidateString)
    {
        // Generar nombre único para archivo
        FString FileName = FString::Printf(TEXT("%s_%s_%s.txt"), 
            *SessionName.ToString(), 
            *FGuid::NewGuid().ToString(), 
            *FDateTime::UtcNow().ToString());
        FString FilePath = FPaths::Combine(SignalingDir, FileName);
        
        // Guardar candidato en archivo
        FFileHelper::SaveStringToFile(CandidateString, *FilePath);
    }
    
    void CheckForCandidates(FName SessionName, TFunction<void(const FString&)> OnCandidateReceived)
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        
        // Buscar archivos en el directorio
        TArray<FString> Files;
        PlatformFile.FindFiles(Files, *SignalingDir, TEXT(".txt"));
        
        for (const FString& FileName : Files)
        {
            FString FilePath = FPaths::Combine(SignalingDir, FileName);
            FString CandidateString;
            
            if (FFileHelper::LoadFileToString(CandidateString, *FilePath))
            {
                OnCandidateReceived(CandidateString);
                // Opcional: eliminar archivo después de procesarlo
                PlatformFile.DeleteFile(*FilePath);
            }
        }
    }
    
private:
    FString SignalingDir;
};

// Uso:
FSimpleFileSignaling Signaling(TEXT("C:/SharedFolder/ICESignaling"));

// Al recibir candidatos locales
SessionICE->OnLocalCandidatesReady.AddLambda([&Signaling](FName SessionName, const TArray<FICECandidate>& Candidates)
{
    for (const FICECandidate& Candidate : Candidates)
    {
        Signaling.SendCandidate(SessionName, Candidate.ToString());
    }
});

// Tick para verificar nuevos candidatos (llamar periódicamente)
void Tick(float DeltaTime)
{
    Signaling.CheckForCandidates(FName("GameSession"), [SessionICE](const FString& CandidateString)
    {
        SessionICE->AddRemoteICECandidate(CandidateString);
    });
}
   - Verificar que archivos .json aparecen en ICESignaling/

2. **Cliente (Máquina B):**
   - Buscar y unirse a sesión
   - Verificar que puede leer archivos del host
   - Verificar que escribe sus propios archivos

3. **Verificar Conexión:**
   - Usar `ICE.STATUS` en ambas máquinas
   - Verificar logs para mensajes "Received signal"

## Comandos de Consola Útiles

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

### Problema: "Candidates not exchanged"

**Síntomas:**
```
LogOnlineICE: Gathered 3 ICE candidates for session
(Los candidatos no llegan al peer remoto)
```

**Soluciones:**
1. Verificar que el delegado `OnLocalCandidatesReady` está vinculado correctamente
2. Verificar que la lógica de señalización está enviando los candidatos
3. Verificar que el peer remoto está llamando a `AddRemoteICECandidate()`
4. Verificar logs en ambos lados para diagnosticar el problema
5. Para testing rápido, usar comandos de consola manuales

### Problema: "Connectivity checks not starting"

**Síntomas:**
```
(Los candidatos están agregados pero no se inician checks)
```

**Soluciones:**
1. Ejecutar manualmente: `ICE.STARTCHECKS`
2. Verificar que ambos lados tienen candidatos locales Y remotos
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

### Flujo de Señalización con Delegados

```
[Host Crea Sesión]
    ↓
[Recopila Candidatos ICE]
    ↓
[OnLocalCandidatesReady se dispara] ← Delegado
    ↓
[Tu código envía candidatos al peer remoto]
    (HTTP, WebSocket, archivo, etc.)
    ↓
[Peer remoto recibe candidatos]
    ↓
[Peer llama AddRemoteICECandidate()]
    ↓
[OnRemoteCandidateReceived se dispara] ← Delegado
    ↓
[El mismo proceso ocurre en dirección inversa]
    ↓
[Ambos tienen candidatos locales y remotos]
    ↓
[Ejecutar ICE.STARTCHECKS o llamar StartICEConnectivityChecks()]
    ↓
[Conexión P2P Establecida] ✅
```

## Ventajas del Nuevo Sistema

1. **Flexibilidad**: Implementa cualquier mecanismo de señalización
2. **Sin Dependencias**: No requiere JSON ni archivos
3. **Rendimiento**: Sin overhead de serialización o I/O de archivos
4. **Estilo Unreal**: Usa delegados multicast nativos de Unreal
5. **Testing Simple**: Comandos de consola para intercambio manual

## Ejemplos de Implementación de Señalización

### Ejemplo 1: HTTP REST API

```cpp
void MySignalingService::Initialize(FOnlineSessionICE* SessionICE)
{
    SessionICE->OnLocalCandidatesReady.AddLambda([this](FName SessionName, const TArray<FICECandidate>& Candidates)
    {
        // Serializar candidatos a JSON
        TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
        JsonObject->SetStringField(TEXT("sessionName"), SessionName.ToString());
        
        TArray<TSharedPtr<FJsonValue>> CandidatesArray;
        for (const FICECandidate& Candidate : Candidates)
        {
            CandidatesArray.Add(MakeShared<FJsonValueString>(Candidate.ToString()));
        }
        JsonObject->SetArrayField(TEXT("candidates"), CandidatesArray);
        
        // Enviar a servidor REST
        TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(TEXT("http://myserver.com/api/ice/candidates"));
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        
        FString JsonString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
        FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
        Request->SetContentAsString(JsonString);
        
        Request->ProcessRequest();
    });
}
```

### Ejemplo 2: WebSocket

```cpp
void MyWebSocketSignaling::Initialize(FOnlineSessionICE* SessionICE)
{
    SessionICE->OnLocalCandidatesReady.AddLambda([this](FName SessionName, const TArray<FICECandidate>& Candidates)
    {
        for (const FICECandidate& Candidate : Candidates)
        {
            FString Message = FString::Printf(TEXT("{\"type\":\"candidate\",\"session\":\"%s\",\"data\":\"%s\"}"),
                *SessionName.ToString(), *Candidate.ToString());
            WebSocket->Send(Message);
        }
    });
    
    // Al recibir mensaje del WebSocket
    WebSocket->OnMessage().AddLambda([SessionICE](const FString& Message)
    {
        // Parsear y agregar candidato
        SessionICE->AddRemoteICECandidate(CandidateString);
    });
}
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

## Más Ejemplos

Para ejemplos completos de código usando sesiones, ver:
- [EXAMPLES.md](EXAMPLES.md) - Ejemplos de uso de las interfaces de sesión
- [TESTING_GUIDE.md](TESTING_GUIDE.md) - Guía completa de testing manual

## Preguntas Frecuentes

**P: ¿Funciona en diferentes plataformas?**
R: Sí, funciona en Windows, Linux y Mac siempre que implementes tu mecanismo de señalización.

**P: ¿Cómo implemento señalización en producción?**
R: Usa HTTP REST API, WebSocket o cualquier protocolo que prefieras. El sistema te da la flexibilidad de elegir.

**P: ¿Cuántos clientes pueden conectarse?**
R: Depende de la configuración de `NumPublicConnections` en session settings.

**P: ¿Funciona a través de Internet?**
R: Sí, si:
1. STUN descubre IP pública correctamente
2. Tu mecanismo de señalización funciona a través de Internet
3. NAT permite conexiones P2P (usa TURN si no)

**P: ¿Puedo usar comandos manuales para testing rápido?**
R: Sí, los comandos manuales (`ICE.LISTCANDIDATES`, `ICE.ADDCANDIDATE`, etc.) siguen disponibles para testing.

## Soporte

Para problemas e issues:
- GitHub: https://github.com/raydienull/subsystem-ice/issues
- Documentación: README.md, IMPLEMENTATION.md, TESTING_GUIDE.md
