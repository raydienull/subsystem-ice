# Guías de Seguridad para OnlineSubsystemICE

## Manejo de Credenciales TURN

### Almacenamiento
- Las credenciales TURN deben estar en el archivo de configuración, NO en código.
- Usar `Config` properties en clases UObject para lectura automática.
- Nunca loggear credenciales completas, solo fragmentos para diagnóstico.

### Ejemplo de configuración segura
```cpp
UCLASS(Config=Engine)
class UICESettings : public UObject
{
    GENERATED_BODY()

    UPROPERTY(Config)
    FString TURNServer;

    UPROPERTY(Config, meta=(MaskInLogs="true"))
    FString TURNUsername;

    UPROPERTY(Config, meta=(MaskInLogs="true"))
    FString TURNCredential;
};
```

### Manejo en memoria
```cpp
// Limpieza segura de credenciales
void FICEAgent::ClearCredentials()
{
    // Sobrescribir memoria antes de liberar
    FPlatformMemory::Memset(TURNCredential.GetCharArray().GetData(), 0, 
        TURNCredential.Len() * sizeof(TCHAR));
    TURNCredential.Empty();
}
```

## Validación de Peers

### Autenticación de conexiones
- Validar tokens/credenciales antes de aceptar conexiones P2P.
- Implementar timeouts para prevenir ataques de denegación de servicio.
- Usar firmas digitales para verificar la identidad de peers.

### Ejemplo de validación
```cpp
bool FOnlineSessionICE::ValidatePeer(const FUniqueNetId& PeerId, 
    const FString& AuthToken)
{
    // Rate limiting por IP
    if (!RateLimiter.CheckAndUpdateLimit(PeerId))
    {
        UE_LOG(LogOnlineICE, Warning, TEXT("Rate limit exceeded for peer"));
        return false;
    }

    // Validar token
    if (!IdentityInterface->ValidateAuthToken(AuthToken))
    {
        UE_LOG(LogOnlineICE, Warning, TEXT("Invalid auth token from peer"));
        return false;
    }

    return true;
}
```

## Protección contra Ataques

### DoS y Rate Limiting
```cpp
class FICERateLimiter
{
public:
    bool CheckAndUpdateLimit(const FString& Key)
    {
        FScopeLock Lock(&RateLimitLock);
        
        const double Now = FPlatformTime::Seconds();
        FRateLimit& Limit = Limits.FindOrAdd(Key);
        
        // Limpiar entradas antiguas
        if (Now - Limit.LastReset > ResetIntervalSeconds)
        {
            Limit.Count = 0;
            Limit.LastReset = Now;
        }
        
        // Verificar límite
        if (Limit.Count >= MaxAttemptsPerInterval)
        {
            return false;
        }
        
        Limit.Count++;
        return true;
    }

private:
    struct FRateLimit
    {
        int32 Count;
        double LastReset;
    };
    
    TMap<FString, FRateLimit> Limits;
    FCriticalSection RateLimitLock;
    
    const int32 MaxAttemptsPerInterval = 100;
    const double ResetIntervalSeconds = 60.0;
};
```

### Sanitización de Entrada
```cpp
bool FICEAgent::ValidateSTUNMessage(const uint8* Data, int32 Length)
{
    // Verificar tamaño mínimo
    if (Length < STUN_HEADER_SIZE)
    {
        return false;
    }
    
    // Validar magic cookie
    const uint32 MagicCookie = FMemory::LoadAligned<uint32>(Data + 4);
    if (MagicCookie != STUN_MAGIC_COOKIE)
    {
        return false;
    }
    
    // Validar length field
    const uint16 MessageLength = FMemory::LoadAligned<uint16>(Data + 2);
    if (MessageLength + STUN_HEADER_SIZE > Length)
    {
        return false;
    }
    
    return true;
}
```

## Mejores Prácticas de Red

### Encriptación
- Usar TLS para conexiones TURN cuando sea posible.
- Implementar cifrado end-to-end para datos de aplicación.
- Rotar claves periódicamente.

### Manejo de Errores
```cpp
void FICEAgent::HandleSecurityError(const FString& Operation, 
    const FString& Error)
{
    // Log detallado pero seguro
    UE_LOG(LogOnlineICE, Warning, 
        TEXT("Security error in %s: %s"), 
        *Operation,
        *SanitizeErrorMessage(Error));
    
    // Notificar al sistema
    SecurityErrorDelegate.ExecuteIfBound(Operation);
    
    // Cleanup si es necesario
    if (IsErrorCritical(Error))
    {
        Close();
    }
}
```