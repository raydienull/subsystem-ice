# Instrucciones de GitHub Copilot para el repositorio OnlineSubsystemICE

Idioma: Español (primario). Puedes responder en inglés técnico cuando el código o las APIs lo requieran, pero proporciona la explicación en español.

## Resumen breve del proyecto
OnlineSubsystemICE es un plugin de Unreal Engine que implementa una plataforma de "Online Subsystem" llamada ICE (Interactive Connectivity Establishment / servicio propio). Está escrito en C++ siguiendo las convenciones de Unreal Engine y contiene módulos bajo `Source/OnlineSubsystemICE` con código público y privado. El plugin se integra con el sistema de módulos de UE y está destinado a compilarse para Windows (Win64) dentro del editor Unreal.

Archivos y rutas clave:
- `OnlineSubsystemICE.uplugin` — metadatos del plugin.
- `Source/OnlineSubsystemICE/` — código fuente del plugin.
  - `Public/` — cabeceras públicas (API del plugin).
  - `Private/` — implementaciones.
- `Config/DefaultOnlineSubsystemICE.ini` — configuración por defecto.
- `Binaries/Win64/` e `Intermediate/Build/Win64/` — binarios y artefactos de compilación.

## Objetivo de estas instrucciones
- Proveer contexto del proyecto para que GitHub Copilot genere sugerencias más relevantes.
- Preferir soluciones que se integren con Unreal Engine 4/5 (según el target del repo), sigan las convenciones de rendimiento y seguridad, y respeten la arquitectura del subsystem.
- Evitar cambios grandes en la API pública sin una justificación clara en el prompt.

## Estilo de código y convenciones

### Convenciones de nombres y tipos
- Prefijos de clase:
  ```cpp
  class FICEAgent;              // F- para structs y clases sin herencia UObject
  class UICESettings;           // U- para clases derivadas de UObject
  class IOnlineIdentityICE;     // I- para interfaces
  ```

- Contenedores y smart pointers:
  ```cpp
  TArray<FICECandidate> Candidates;            // En lugar de std::vector
  TSharedPtr<FSocket, ESPMode::ThreadSafe> Socket;  // Thread-safe para recursos compartidos
  TUniquePtr<FICEWorker> Worker;              // Para ownership único
  TMap<FString, FICESession> Sessions;         // En lugar de std::map/unordered_map
  ```

### Macros y reflection
- Propiedades y funciones expuestas:
  ```cpp
  UCLASS(Config=Engine)
  class UICESettings : public UObject
  {
      GENERATED_BODY()
  
      UPROPERTY(Config)
      FString STUNServer;
  
      UFUNCTION(BlueprintCallable, Category="Online|ICE")
      bool InitializeICEAgent();
  };
  ```

### Logging y diagnóstico
- Categorías de log:
  ```cpp
  // En el .h
  DECLARE_LOG_CATEGORY_EXTERN(LogOnlineICE, Log, All);
  
  // En el .cpp
  DEFINE_LOG_CATEGORY(LogOnlineICE);
  
  // Uso
  UE_LOG(LogOnlineICE, Warning, TEXT("STUN request failed: %s"), *ErrorMessage);
  UE_LOG(LogOnlineICE, Verbose, TEXT("ICE candidate gathered: %s"), *Candidate.ToString());
  ```

### Manejo de recursos y memoria
- RAII y ownership claro:
  ```cpp
  // Preferir
  TUniquePtr<FICEWorker> Worker;
  
  // Evitar
  FICEWorker* Worker = nullptr; // Raw pointer sin ownership claro
  ```

### Asincronía y threads
- Delegates y callbacks:
  ```cpp
  DECLARE_DELEGATE_OneParam(FOnICECandidateGathered, const FICECandidate&);
  FOnICECandidateGathered OnCandidateGathered;
  
  // Para código async
  FICECompletionDelegate = FSimpleDelegate::CreateLambda([this]()
  {
      // Manejar completion
  });
  ```

### Dependencias en Build.cs
- Documentar y justificar:
  ```csharp
  PublicDependencyModuleNames.AddRange(
      new string[]
      {
          "OnlineSubsystem",     // Base OSS interfaces
          "OnlineSubsystemUtils", // Utility classes for OSS
          "Sockets",             // Network socket functionality
          "NetCore"              // Core networking
      }
  );
  ```

## Patrones y mejores prácticas del Online Subsystem

### Inicialización y shutdown
```cpp
bool FOnlineSubsystemICE::Init()
{
    if (!FOnlineSubsystemImpl::Init())
    {
        return false;
    }

    // Crear interfaces principales
    SessionInterface = MakeShared<FOnlineSessionICE, ESPMode::ThreadSafe>(this);
    IdentityInterface = MakeShared<FOnlineIdentityICE, ESPMode::ThreadSafe>(this);

    // Cargar configuración
    LoadConfig();

    return true;
}

bool FOnlineSubsystemICE::Shutdown()
{
    // Liberar en orden inverso
    SessionInterface.Reset();
    IdentityInterface.Reset();

    return FOnlineSubsystemImpl::Shutdown();
}
```

### Manejo de sesiones P2P
```cpp
void FOnlineSessionICE::OnP2PSessionRequest(const FUniqueNetId& RemoteId)
{
    // Validar estado
    if (!ensure(SessionState == EOnlineSessionState::InProgress))
    {
        UE_LOG(LogOnlineICE, Warning, TEXT("P2P request while session not active"));
        return;
    }

    // Procesar en thread seguro
    AsyncTask(ENamedThreads::GameThread, [this, RemoteId]()
    {
        HandleP2PSessionRequest(RemoteId);
    });
}
```

### Autenticación y tokens
```cpp
bool FOnlineIdentityICE::ValidateAuthToken(const FString& AuthToken)
{
    // Nunca logs de tokens completos
    UE_LOG(LogOnlineICE, Verbose, TEXT("Validating auth token: %s...%s"), 
        *AuthToken.Left(4), *AuthToken.Right(4));

    // Validación thread-safe
    FScopeLock Lock(&AuthenticationLock);
    return InternalValidateToken(AuthToken);
}
```

### Manejo de estados y transiciones
```cpp
void FICEAgent::UpdateConnectionState(EICEConnectionState NewState)
{
    // Thread safety
    FScopeLock Lock(&StateLock);
    
    if (ConnectionState == NewState)
    {
        return;
    }

    // Logging para diagnóstico
    UE_LOG(LogOnlineICE, Log, TEXT("ICE state change: %s -> %s"),
        *GetConnectionStateName(ConnectionState),
        *GetConnectionStateName(NewState));

    // Actualizar y notificar
    ConnectionState = NewState;
    OnConnectionStateChanged.Broadcast(NewState);
}
```

### Retries y backoff
```cpp
bool FICEAgent::AttemptSTUNRequest(const FString& Server, int32 RetryCount = 3)
{
    float BackoffDelay = 1.0f;
    
    for (int32 Attempt = 0; Attempt < RetryCount; ++Attempt)
    {
        if (PerformSTUNRequest(Server))
        {
            return true;
        }

        // Exponential backoff
        if (Attempt < RetryCount - 1)
        {
            FPlatformProcess::Sleep(BackoffDelay);
            BackoffDelay *= 2.0f;
        }
    }

    return false;
}
```

### Métricas y monitoreo
```cpp
void FICEAgent::TrackNetworkMetrics(const FDateTime& StartTime, int32 BytesSent)
{
    const double LatencyMs = (FDateTime::Now() - StartTime).GetTotalMilliseconds();
    
    FScopeLock Lock(&MetricsLock);
    NetworkMetrics.UpdateLatency(LatencyMs);
    NetworkMetrics.UpdateThroughput(BytesSent);
    
    // Log periódico de métricas
    if (NetworkMetrics.ShouldLogStats())
    {
        UE_LOG(LogOnlineICE, Verbose, TEXT("Network stats - Latency: %.2fms, Throughput: %d B/s"),
            NetworkMetrics.GetAverageLatency(),
            NetworkMetrics.GetThroughputBytesPerSecond());
    }
}
```

## Qué incluir siempre en el prompt para obtener buenas sugerencias
- Ruta del archivo en el repo o fragmento relevante (p. ej. `Source/OnlineSubsystemICE/Private/ICEAgent.cpp`).
- Qué UE version o target (si lo conoces), p. ej. "UE4.26" o "UE5.4".
- El objetivo concreto: "añadir manejo de error", "optimizar uso de memoria", "implementar interfaz X".
- Requisitos de API pública: si la función es pública, dar el contrato esperado (parámetros, comportamiento ante errores, excepciones/noexcepciones).

Ejemplo de prompt claro:
"En `Source/OnlineSubsystemICE/Private/OnlineSessionInterfaceICE.cpp`, implementa `FOnlineSessionICE::CreateSession` para validar parámetros, registrar errores con `UE_LOG`, y devolver `false` en fallos. Sigue el estilo de las demás funciones en `OnlineSessionInterfaceICE.cpp` y no cambies la firma pública."

## Ejemplos de prompts útiles

### Networking y P2P
- "En `ICEAgent.cpp`, implementa la función `PerformSTUNRequest` usando `FSocket` para enviar/recibir mensajes STUN y manejar timeouts."
- "Mejora `GatherServerReflexiveCandidates` para intentar múltiples STUN servers en paralelo usando `FRunnable` y `FCriticalSection` para sincronización."
- "En `OnlineSessionInterfaceICE.cpp`, añade retry logic con backoff exponencial para reconexiones P2P fallidas."

### Seguridad y Autenticación
- "Implementa el método `CalculateHMACSHA1` en `ICEAgent.cpp` usando las APIs criptográficas de UE para MESSAGE-INTEGRITY en STUN/TURN."
- "Añade validación de certificados y manejo seguro de credenciales TURN en `OnlineSubsystemICE::Init`."
- "Implementa rate limiting para peticiones STUN/TURN en `ICEAgent` usando `FDateTime` y logging detallado."

### Testing y Depuración
- "Crea casos de prueba para `FICEAgent` que simulen diferentes escenarios de NAT y fallos de red."
- "Añade logging detallado en `GatherCandidates` para diagnóstico de problemas de conectividad."
- "Implementa métricas de rendimiento en `SendData`/`ReceiveData` usando `FPlatformTime` y `UE_LOG` con verbosity."

### Integración con Online Subsystem
- "En `OnlineSessionInterfaceICE.cpp`, implementa manejo de desconexiones P2P y notificación a `SessionInterface`."
- "Añade soporte para configuración de ICE desde DefaultEngine.ini con valores por defecto seguros."
- "Mejora el manejo de errores en `FOnlineIdentityICE` para reportar problemas de autenticación TURN."

## Recomendaciones y límites (Do / Don't)

### Arquitectura y Diseño
- Do: Proponer cambios que respeten la arquitectura de módulos y no rompan la compatibilidad binaria sin discusión.
- Do: Seguir el patrón de Online Subsystem para interfaces e implementaciones (ejemplo: `IOnlineSession` → `FOnlineSessionICE`).
- Do: Usar los tipos thread-safe de UE (`TSharedPtr<T, ESPMode::ThreadSafe>`) para recursos compartidos.
- Don't: Mezclar responsabilidades entre agentes ICE y la capa de Online Subsystem.

### Implementación y Rendimiento
- Do: Priorizar claridad, manejo de errores detallado, y uso de las utilidades de Unreal Engine.
- Do: Implementar timeouts y retries apropiados para operaciones de red (STUN/TURN).
- Do: Usar `AsyncTask` o `FRunnable` para operaciones costosas o bloqueantes.
- Don't: Bloquear el hilo principal con operaciones de red síncronas.

### Dependencias y Mantenimiento
- Do: Preferir las APIs estándar de UE para red (`FSocket`, `FInternetAddr`) y criptografía.
- Don't: Añadir dependencias externas (librerías de terceros) sin mencionarlo explícitamente en el prompt.
- Don't: Reescribir grandes bloques sin dar un resumen antes y una estrategia de migración.
- Don't: Modificar el manejo de memoria o threads sin considerar el impacto en diferentes plataformas.

## Validación y pruebas del código generado

### Checklist básico de validación
1. Revisar que no se haya introducido STL en lugar de tipos de UE.
2. Verificar uso correcto de macros UPROPERTY/UFUNCTION si el código expone tipos a reflection.
3. Comprobar que los logs usan `UE_LOG` con categorías definidas.
4. Asegurarse de que no haya cambios en el `Build.cs` que añadan dependencias externas sin justificación.
5. Compilar el plugin en Windows (Win64) con el editor Unreal.

### Pruebas específicas para funcionalidad de red
1. **Tests de conectividad básica**:
   - Validar inicialización de `FICEAgent` con diferentes configuraciones
   - Probar gathering de candidatos locales (interfaces de red)
   - Verificar conexión STUN básica con servidor de pruebas
   - Validar proceso completo de handshake ICE

2. **Escenarios de NAT y firewall**:
   - Simular diferentes tipos de NAT (Full Cone, Restricted, Port Restricted)
   - Probar fallback a TURN cuando la conexión directa falla
   - Verificar reconexión automática tras pérdida de conectividad

3. **Pruebas de rendimiento y estabilidad**:
   - Medir latencia en el envío/recepción de datos P2P
   - Validar manejo de congestión y control de flujo
   - Probar límites de tamaño de paquetes y fragmentación
   - Verificar comportamiento con pérdida de paquetes simulada

4. **Seguridad y validación**:
   - Verificar manejo seguro de credenciales TURN
   - Validar autenticación de peers y tokens de sesión
   - Comprobar sanitización de entradas en mensajes STUN/TURN
   - Validar protección contra flooding y DoS

### Herramientas recomendadas para pruebas
- Usar `FSocketSubsystemModule` para simular condiciones de red
- Implementar logs detallados con `UE_LOG(LogOnlineICE, Verbose, ...)`
- Utilizar perfiles de red en el editor UE para simular latencia/pérdida
- Configurar builds de test con `#if WITH_DEV_AUTOMATION_TESTS`

## Notas adicionales para reviewers/humanos
- Si Copilot propone cambios en la API pública (`Public/`), pide siempre que incluya una nota de migración o un ejemplo de uso.
- Para cambios en la red o seguridad (por ejemplo, manejo de tokens), solicitar un pequeño plan de mitigación y pruebas.

## Recursos adicionales
- Ver `security-guidelines.md` para guías detalladas sobre manejo seguro de credenciales TURN, validación de peers y protección contra ataques.
- Consultar `TESTING_GUIDE.md` para instrucciones sobre pruebas de red y simulación de condiciones adversas.
- Para ejemplos de implementación, revisar las clases existentes como `FICEAgent` y `FOnlineSessionICE`.

---

Si quieres, puedo:
- Adaptar estas instrucciones para inglés.
- Añadir más ejemplos de prompts específicos para Online Subsystem.
- Expandir las guías de seguridad o testing con casos específicos de tu proyecto.

