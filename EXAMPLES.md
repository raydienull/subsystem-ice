# OnlineSubsystemICE - Ejemplos de Código

Esta guía proporciona ejemplos prácticos y completos para usar OnlineSubsystemICE en tu proyecto.

## Tabla de Contenidos

1. [Configuración Inicial](#configuración-inicial)
2. [Crear una Sesión (Host)](#crear-una-sesión-host)
3. [Unirse a una Sesión (Cliente)](#unirse-a-una-sesión-cliente)
4. [Sistema Completo en GameMode](#sistema-completo-en-gamemode)
5. [Integración con UI (Widgets)](#integración-con-ui-widgets)
6. [Blueprints](#blueprints)

## Configuración Inicial

### DefaultEngine.ini

```ini
[OnlineSubsystem]
DefaultPlatformService=ICE

[OnlineSubsystemICE]
STUNServer=stun.l.google.com:19302

; Opcional: servidor TURN para NAT difíciles
;TURNServer=turn.example.com:3478
;TURNUsername=username
;TURNCredential=password

[Core.Log]
LogOnlineICE=Log
```

### YourProject.Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new string[] 
{ 
    "Core", 
    "CoreUObject", 
    "Engine", 
    "InputCore",
    "OnlineSubsystem",
    "OnlineSubsystemICE",
    "OnlineSubsystemUtils"
});
```

## Crear una Sesión (Host)

### Ejemplo Mínimo

```cpp
// MyGameMode.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "MyGameMode.generated.h"

UCLASS()
class MYPROJECT_API AMyGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Multiplayer")
    void HostSession();

private:
    void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
    void OnStartSessionComplete(FName SessionName, bool bWasSuccessful);
};
```

```cpp
// MyGameMode.cpp
#include "MyGameMode.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"

void AMyGameMode::HostSession()
{
    // Obtener el subsistema online
    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (!OnlineSub)
    {
        UE_LOG(LogTemp, Error, TEXT("OnlineSubsystem not found"));
        return;
    }

    // Obtener la interfaz de sesiones
    IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
    if (!Sessions.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Session interface not valid"));
        return;
    }

    // Bind del delegate de creación
    Sessions->AddOnCreateSessionCompleteDelegate_Handle(
        FOnCreateSessionCompleteDelegate::CreateUObject(
            this, &AMyGameMode::OnCreateSessionComplete)
    );

    // Configurar la sesión
    FOnlineSessionSettings SessionSettings;
    SessionSettings.NumPublicConnections = 4;           // Máximo 4 jugadores
    SessionSettings.bShouldAdvertise = true;            // Anunciar sesión
    SessionSettings.bUsesPresence = true;               // Usar presencia
    SessionSettings.bAllowJoinInProgress = true;        // Permitir unirse durante el juego
    SessionSettings.bIsLANMatch = false;                // No es LAN (usa ICE)
    SessionSettings.bUsesStats = false;
    SessionSettings.bAllowInvites = true;

    // Agregar configuración personalizada (opcional)
    SessionSettings.Set(
        FName("SERVER_NAME"), 
        FString("Mi Servidor"), 
        EOnlineDataAdvertisementType::ViaOnlineService
    );

    // Crear la sesión
    // Los candidatos ICE se intercambiarán automáticamente
    Sessions->CreateSession(0, FName("GameSession"), SessionSettings);
    
    UE_LOG(LogTemp, Log, TEXT("Creating session..."));
}

void AMyGameMode::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("Session created successfully: %s"), *SessionName.ToString());
        
        // Obtener interfaz de sesiones
        IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
        if (OnlineSub)
        {
            IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
            if (Sessions.IsValid())
            {
                // Bind del delegate de inicio
                Sessions->AddOnStartSessionCompleteDelegate_Handle(
                    FOnStartSessionCompleteDelegate::CreateUObject(
                        this, &AMyGameMode::OnStartSessionComplete)
                );
                
                // Iniciar la sesión
                Sessions->StartSession(SessionName);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create session"));
    }
}

void AMyGameMode::OnStartSessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("Session started: %s"), *SessionName.ToString());
        UE_LOG(LogTemp, Log, TEXT("Waiting for players to join..."));
        
        // La sesión está lista, los jugadores pueden unirse
        // Los candidatos ICE se han difundido automáticamente
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to start session"));
    }
}
```

## Unirse a una Sesión (Cliente)

### Ejemplo Completo

```cpp
// MyGameMode.h (continuación)
public:
    UFUNCTION(BlueprintCallable, Category = "Multiplayer")
    void FindAndJoinSession();

private:
    TSharedPtr<FOnlineSessionSearch> SessionSearch;
    
    void OnFindSessionsComplete(bool bWasSuccessful);
    void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
```

```cpp
// MyGameMode.cpp (continuación)
void AMyGameMode::FindAndJoinSession()
{
    // Obtener el subsistema online
    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (!OnlineSub)
    {
        UE_LOG(LogTemp, Error, TEXT("OnlineSubsystem not found"));
        return;
    }

    // Obtener la interfaz de sesiones
    IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
    if (!Sessions.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Session interface not valid"));
        return;
    }

    // Bind del delegate de búsqueda
    Sessions->AddOnFindSessionsCompleteDelegate_Handle(
        FOnFindSessionsCompleteDelegate::CreateUObject(
            this, &AMyGameMode::OnFindSessionsComplete)
    );

    // Crear configuración de búsqueda
    SessionSearch = MakeShared<FOnlineSessionSearch>();
    SessionSearch->MaxSearchResults = 20;
    SessionSearch->bIsLanQuery = false;
    SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);

    // Iniciar búsqueda
    Sessions->FindSessions(0, SessionSearch.ToSharedRef());
    
    UE_LOG(LogTemp, Log, TEXT("Searching for sessions..."));
}

void AMyGameMode::OnFindSessionsComplete(bool bWasSuccessful)
{
    if (!bWasSuccessful)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to find sessions"));
        return;
    }

    if (SessionSearch->SearchResults.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No sessions found"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("Found %d sessions"), SessionSearch->SearchResults.Num());

    // Listar sesiones encontradas
    for (int32 i = 0; i < SessionSearch->SearchResults.Num(); i++)
    {
        FOnlineSessionSearchResult& Result = SessionSearch->SearchResults[i];
        
        FString ServerName;
        Result.Session.SessionSettings.Get(FName("SERVER_NAME"), ServerName);
        
        UE_LOG(LogTemp, Log, TEXT("Session %d: %s (Ping: %d, Players: %d/%d)"), 
            i,
            *ServerName,
            Result.PingInMs,
            Result.Session.SessionSettings.NumPublicConnections - Result.Session.NumOpenPublicConnections,
            Result.Session.SessionSettings.NumPublicConnections
        );
    }

    // Unirse a la primera sesión encontrada
    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (OnlineSub)
    {
        IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
        if (Sessions.IsValid())
        {
            // Bind del delegate de unión
            Sessions->AddOnJoinSessionCompleteDelegate_Handle(
                FOnJoinSessionCompleteDelegate::CreateUObject(
                    this, &AMyGameMode::OnJoinSessionComplete)
            );
            
            // Unirse a la sesión
            // Los candidatos ICE se intercambiarán automáticamente
            Sessions->JoinSession(0, FName("JoinedSession"), SessionSearch->SearchResults[0]);
        }
    }
}

void AMyGameMode::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    if (Result == EOnJoinSessionCompleteResult::Success)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully joined session: %s"), *SessionName.ToString());
        
        // Obtener información de conexión
        IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
        if (OnlineSub)
        {
            IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
            if (Sessions.IsValid())
            {
                FString ConnectInfo;
                if (Sessions->GetResolvedConnectString(SessionName, ConnectInfo))
                {
                    UE_LOG(LogTemp, Log, TEXT("Connect string: %s"), *ConnectInfo);
                    
                    // Viajar al servidor (cuando el connection string esté listo)
                    // GetWorld()->GetFirstPlayerController()->ClientTravel(ConnectInfo, TRAVEL_Absolute);
                }
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to join session: %d"), (int32)Result);
    }
}
```

## Sistema Completo en GameMode

### Ejemplo con Manejo de Estados

```cpp
// NetworkGameMode.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "NetworkGameMode.generated.h"

UENUM(BlueprintType)
enum class ENetworkState : uint8
{
    Idle,
    CreatingSession,
    SessionReady,
    SearchingSessions,
    JoiningSession,
    Connected,
    Error
};

UCLASS()
class MYPROJECT_API ANetworkGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ANetworkGameMode();

    UFUNCTION(BlueprintCallable, Category = "Network")
    void CreateGameSession(FString ServerName, int32 MaxPlayers);

    UFUNCTION(BlueprintCallable, Category = "Network")
    void FindGameSessions();

    UFUNCTION(BlueprintCallable, Category = "Network")
    void JoinGameSession(int32 SessionIndex);

    UFUNCTION(BlueprintCallable, Category = "Network")
    void LeaveSession();

    UFUNCTION(BlueprintPure, Category = "Network")
    ENetworkState GetNetworkState() const { return CurrentState; }

    UFUNCTION(BlueprintPure, Category = "Network")
    int32 GetSessionCount() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // Estado actual
    UPROPERTY(BlueprintReadOnly, Category = "Network", meta = (AllowPrivateAccess = "true"))
    ENetworkState CurrentState;

    // Búsqueda de sesiones
    TSharedPtr<FOnlineSessionSearch> SessionSearch;

    // Delegates
    void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
    void OnStartSessionComplete(FName SessionName, bool bWasSuccessful);
    void OnFindSessionsComplete(bool bWasSuccessful);
    void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
    void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

    // Helpers
    void SetNetworkState(ENetworkState NewState);
    void LogError(const FString& Message);
};
```

```cpp
// NetworkGameMode.cpp
#include "NetworkGameMode.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"

ANetworkGameMode::ANetworkGameMode()
    : CurrentState(ENetworkState::Idle)
{
}

void ANetworkGameMode::BeginPlay()
{
    Super::BeginPlay();
    
    // Verificar que OnlineSubsystem esté disponible
    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (!OnlineSub)
    {
        UE_LOG(LogTemp, Error, TEXT("OnlineSubsystem not available"));
        SetNetworkState(ENetworkState::Error);
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("OnlineSubsystem: %s"), *OnlineSub->GetSubsystemName().ToString());
}

void ANetworkGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Limpiar sesiones al salir
    LeaveSession();
    
    Super::EndPlay(EndPlayReason);
}

void ANetworkGameMode::CreateGameSession(FString ServerName, int32 MaxPlayers)
{
    if (CurrentState != ENetworkState::Idle)
    {
        LogError(TEXT("Cannot create session in current state"));
        return;
    }

    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (!OnlineSub) return;

    IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
    if (!Sessions.IsValid()) return;

    SetNetworkState(ENetworkState::CreatingSession);

    // Configurar sesión
    FOnlineSessionSettings Settings;
    Settings.NumPublicConnections = FMath::Clamp(MaxPlayers, 2, 16);
    Settings.bShouldAdvertise = true;
    Settings.bUsesPresence = true;
    Settings.bAllowJoinInProgress = true;
    Settings.bIsLANMatch = false;
    Settings.Set(FName("SERVER_NAME"), ServerName, EOnlineDataAdvertisementType::ViaOnlineService);

    // Bind delegates
    Sessions->OnCreateSessionCompleteDelegates.AddUObject(this, &ANetworkGameMode::OnCreateSessionComplete);
    Sessions->OnStartSessionCompleteDelegates.AddUObject(this, &ANetworkGameMode::OnStartSessionComplete);

    // Crear sesión
    Sessions->CreateSession(0, FName("GameSession"), Settings);
    
    UE_LOG(LogTemp, Log, TEXT("Creating session: %s (Max: %d)"), *ServerName, MaxPlayers);
}

void ANetworkGameMode::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("Session created: %s"), *SessionName.ToString());
        
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
    else
    {
        LogError(TEXT("Failed to create session"));
        SetNetworkState(ENetworkState::Error);
    }
}

void ANetworkGameMode::OnStartSessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("Session started: %s"), *SessionName.ToString());
        SetNetworkState(ENetworkState::SessionReady);
    }
    else
    {
        LogError(TEXT("Failed to start session"));
        SetNetworkState(ENetworkState::Error);
    }
}

void ANetworkGameMode::FindGameSessions()
{
    if (CurrentState != ENetworkState::Idle)
    {
        LogError(TEXT("Cannot search in current state"));
        return;
    }

    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (!OnlineSub) return;

    IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
    if (!Sessions.IsValid()) return;

    SetNetworkState(ENetworkState::SearchingSessions);

    // Configurar búsqueda
    SessionSearch = MakeShared<FOnlineSessionSearch>();
    SessionSearch->MaxSearchResults = 20;
    SessionSearch->bIsLanQuery = false;

    // Bind delegate
    Sessions->OnFindSessionsCompleteDelegates.AddUObject(this, &ANetworkGameMode::OnFindSessionsComplete);

    // Buscar sesiones
    Sessions->FindSessions(0, SessionSearch.ToSharedRef());
    
    UE_LOG(LogTemp, Log, TEXT("Searching for sessions..."));
}

void ANetworkGameMode::OnFindSessionsComplete(bool bWasSuccessful)
{
    SetNetworkState(ENetworkState::Idle);

    if (!bWasSuccessful || !SessionSearch.IsValid())
    {
        LogError(TEXT("Failed to find sessions"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("Found %d sessions"), SessionSearch->SearchResults.Num());
}

void ANetworkGameMode::JoinGameSession(int32 SessionIndex)
{
    if (!SessionSearch.IsValid() || SessionIndex < 0 || SessionIndex >= SessionSearch->SearchResults.Num())
    {
        LogError(TEXT("Invalid session index"));
        return;
    }

    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (!OnlineSub) return;

    IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
    if (!Sessions.IsValid()) return;

    SetNetworkState(ENetworkState::JoiningSession);

    // Bind delegate
    Sessions->OnJoinSessionCompleteDelegates.AddUObject(this, &ANetworkGameMode::OnJoinSessionComplete);

    // Unirse
    Sessions->JoinSession(0, FName("JoinedSession"), SessionSearch->SearchResults[SessionIndex]);
}

void ANetworkGameMode::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    if (Result == EOnJoinSessionCompleteResult::Success)
    {
        UE_LOG(LogTemp, Log, TEXT("Joined session successfully"));
        SetNetworkState(ENetworkState::Connected);
    }
    else
    {
        LogError(FString::Printf(TEXT("Failed to join: %d"), (int32)Result));
        SetNetworkState(ENetworkState::Error);
    }
}

void ANetworkGameMode::LeaveSession()
{
    IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
    if (!OnlineSub) return;

    IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
    if (!Sessions.IsValid()) return;

    // Bind delegate
    Sessions->OnDestroySessionCompleteDelegates.AddUObject(this, &ANetworkGameMode::OnDestroySessionComplete);

    // Destruir sesión
    Sessions->DestroySession(FName("GameSession"));
    Sessions->DestroySession(FName("JoinedSession"));
}

void ANetworkGameMode::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("Session destroyed: %s"), *SessionName.ToString());
    }
    
    SetNetworkState(ENetworkState::Idle);
}

int32 ANetworkGameMode::GetSessionCount() const
{
    return SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0;
}

void ANetworkGameMode::SetNetworkState(ENetworkState NewState)
{
    CurrentState = NewState;
    UE_LOG(LogTemp, Log, TEXT("Network State: %d"), (int32)CurrentState);
}

void ANetworkGameMode::LogError(const FString& Message)
{
    UE_LOG(LogTemp, Error, TEXT("%s"), *Message);
}
```

## Integración con UI (Widgets)

### Widget de Menú Principal

```cpp
// MainMenuWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

UCLASS()
class MYPROJECT_API UMainMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Menu")
    void OnHostButtonClicked(FString ServerName, int32 MaxPlayers);

    UFUNCTION(BlueprintCallable, Category = "Menu")
    void OnJoinButtonClicked();

    UFUNCTION(BlueprintCallable, Category = "Menu")
    void OnRefreshButtonClicked();

    UFUNCTION(BlueprintImplementableEvent, Category = "Menu")
    void OnSessionsFound(int32 Count);

    UFUNCTION(BlueprintImplementableEvent, Category = "Menu")
    void OnError(const FString& ErrorMessage);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    TSharedPtr<class FOnlineSessionSearch> SessionSearch;
    
    void FindSessions();
    void OnFindSessionsComplete(bool bWasSuccessful);
};
```

## Blueprints

### GameInstance con Funciones de Red

Puedes exponer funciones del OnlineSubsystem en un GameInstance para usarlas fácilmente en Blueprints:

```cpp
// MyGameInstance.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "MyGameInstance.generated.h"

UCLASS()
class MYPROJECT_API UMyGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Network")
    void HostSession(FString ServerName, int32 MaxPlayers);

    UFUNCTION(BlueprintCallable, Category = "Network")
    void FindSessions();

    UFUNCTION(BlueprintCallable, Category = "Network")
    void JoinSession(int32 SessionIndex);

    UFUNCTION(BlueprintPure, Category = "Network")
    int32 GetSessionCount() const;

    UFUNCTION(BlueprintPure, Category = "Network")
    FString GetSessionName(int32 SessionIndex) const;

    // Events para Blueprint
    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnlineSessionCreatedDelegate OnSessionCreated;

    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnlineSessionsFoundDelegate OnSessionsFound;

    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnlineSessionJoinedDelegate OnSessionJoined;

private:
    TSharedPtr<FOnlineSessionSearch> SessionSearch;
    
    void OnCreateSessionComplete(FName SessionName, bool bSuccess);
    void OnFindSessionsComplete(bool bSuccess);
    void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
};
```

Esto te permite llamar fácilmente las funciones desde Blueprint sin código C++.

## Debugging y Testing

### Logging

```cpp
// Habilitar logs detallados en código
#if !UE_BUILD_SHIPPING
    UE_LOG(LogTemp, Verbose, TEXT("Detailed debug info here"));
#endif
```

### Console Commands

Durante el desarrollo, usa:
```
ICE.STATUS          - Ver estado de conexión
ICE.LISTCANDIDATES  - Ver candidatos ICE
log LogOnlineICE VeryVerbose  - Logs detallados
```

## Referencias

- [LOCAL_TESTING_GUIDE.md](LOCAL_TESTING_GUIDE.md) - Guía de testing local
- [IMPLEMENTATION.md](IMPLEMENTATION.md) - Documentación técnica completa
- [README.md](README.md) - Visión general del plugin
