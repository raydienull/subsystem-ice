# OnlineSubsystemICE Integration Guide

This guide provides step-by-step instructions for integrating OnlineSubsystemICE into your Unreal Engine project.

## Prerequisites

- Unreal Engine 5.6 or later
- Basic understanding of Unreal Engine's Online Subsystem
- C++ project (required for plugin integration)

## Step 1: Installation

### For Project Plugins

1. Create a `Plugins` folder in your project root if it doesn't exist
2. Copy or clone the `OnlineSubsystemICE` folder into the `Plugins` directory
3. Your project structure should look like:
   ```
   MyProject/
   ├── Plugins/
   │   └── OnlineSubsystemICE/
   │       ├── OnlineSubsystemICE.uplugin
   │       ├── Source/
   │       ├── Config/
   │       └── ...
   ├── Source/
   ├── Content/
   └── MyProject.uproject
   ```

### For Engine Plugins

1. Copy the `OnlineSubsystemICE` folder to `Engine/Plugins/Online/`
2. This makes it available to all projects using that engine installation

## Step 2: Enable the Plugin

1. Open your project in Unreal Engine
2. Go to **Edit > Plugins**
3. Search for "Online Subsystem ICE"
4. Check the **Enabled** checkbox
5. Restart the editor when prompted

Alternatively, edit your `.uproject` file directly:

```json
{
    "FileVersion": 3,
    "EngineAssociation": "5.x",
    "Plugins": [
        {
            "Name": "OnlineSubsystemICE",
            "Enabled": true
        }
    ]
}
```

## Step 3: Configure DefaultEngine.ini

Add the following configuration to `Config/DefaultEngine.ini`:

```ini
[OnlineSubsystem]
; Set ICE as the default online subsystem
DefaultPlatformService=ICE

[OnlineSubsystemICE]
; STUN server configuration (Google's public STUN server)
STUNServer=stun.l.google.com:19302

; Optional: Configure TURN server for relay
; TURNServer=turn.yourserver.com:3478
; TURNUsername=yourusername
; TURNCredential=yourpassword

; Enable IPv6 if needed
bEnableIPv6=false

[/Script/Engine.GameEngine]
; Ensure the online subsystem is properly loaded
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/OnlineSubsystemUtils.IpNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
```

## Step 4: Update Build Configuration

Edit your project's `*.Build.cs` file to include the required dependencies:

```csharp
// MyProject.Build.cs

public class MyProject : ModuleRules
{
    public MyProject(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    
        PublicDependencyModuleNames.AddRange(new string[] { 
            "Core", 
            "CoreUObject", 
            "Engine", 
            "InputCore",
            "OnlineSubsystem",        // Add this
            "OnlineSubsystemICE",     // Add this
            "OnlineSubsystemUtils"    // Add this
        });
    }
}
```

## Step 5: Implement in C++

### Login Implementation

```cpp
// MyGameMode.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "MyGameMode.generated.h"

UCLASS()
class MYPROJECT_API AMyGameMode : public AGameModeBase
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;
    
private:
    void LoginPlayer(int32 LocalUserNum);
    void OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);
};
```

```cpp
// MyGameMode.cpp
#include "MyGameMode.h"
#include "OnlineSubsystem.h"

void AMyGameMode::BeginPlay()
{
    Super::BeginPlay();
    
    // Login the first local player
    LoginPlayer(0);
}

void AMyGameMode::LoginPlayer(int32 LocalUserNum)
{
    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
    if (OnlineSubsystem)
    {
        IOnlineIdentityPtr Identity = OnlineSubsystem->GetIdentityInterface();
        if (Identity.IsValid())
        {
            // Bind the login completion delegate
            Identity->AddOnLoginCompleteDelegate_Handle(
                LocalUserNum,
                FOnLoginCompleteDelegate::CreateUObject(this, &AMyGameMode::OnLoginComplete)
            );
            
            // Auto-login
            Identity->AutoLogin(LocalUserNum);
        }
    }
}

void AMyGameMode::OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("Login successful for user %d: %s"), LocalUserNum, *UserId.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Login failed: %s"), *Error);
    }
}
```

### Session Creation

```cpp
// MySessionManager.h
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "MySessionManager.generated.h"

UCLASS()
class MYPROJECT_API UMySessionManager : public UObject
{
    GENERATED_BODY()

public:
    void CreateSession(int32 NumPublicConnections);
    void FindSessions();
    void JoinSession(const FOnlineSessionSearchResult& SessionResult);
    
private:
    void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
    void OnFindSessionsComplete(bool bWasSuccessful);
    void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
    
    TSharedPtr<FOnlineSessionSearch> SessionSearch;
};
```

```cpp
// MySessionManager.cpp
#include "MySessionManager.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"

void UMySessionManager::CreateSession(int32 NumPublicConnections)
{
    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
    if (OnlineSubsystem)
    {
        IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
        if (Sessions.IsValid())
        {
            // Bind delegate
            Sessions->AddOnCreateSessionCompleteDelegate_Handle(
                FOnCreateSessionCompleteDelegate::CreateUObject(this, &UMySessionManager::OnCreateSessionComplete)
            );
            
            // Configure session settings
            FOnlineSessionSettings SessionSettings;
            SessionSettings.NumPublicConnections = NumPublicConnections;
            SessionSettings.bShouldAdvertise = true;
            SessionSettings.bAllowJoinInProgress = true;
            SessionSettings.bIsLANMatch = false;
            SessionSettings.bUsesPresence = true;
            SessionSettings.bAllowInvites = true;
            SessionSettings.bUseLobbiesIfAvailable = false;
            
            // Create session
            Sessions->CreateSession(0, FName("GameSession"), SessionSettings);
        }
    }
}

void UMySessionManager::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("Session created: %s"), *SessionName.ToString());
        
        // Start the session
        IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
        if (OnlineSubsystem)
        {
            IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
            if (Sessions.IsValid())
            {
                Sessions->StartSession(SessionName);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create session"));
    }
}

void UMySessionManager::FindSessions()
{
    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
    if (OnlineSubsystem)
    {
        IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
        if (Sessions.IsValid())
        {
            // Bind delegate
            Sessions->AddOnFindSessionsCompleteDelegate_Handle(
                FOnFindSessionsCompleteDelegate::CreateUObject(this, &UMySessionManager::OnFindSessionsComplete)
            );
            
            // Configure search settings
            SessionSearch = MakeShared<FOnlineSessionSearch>();
            SessionSearch->MaxSearchResults = 20;
            SessionSearch->bIsLanQuery = false;
            SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
            
            // Start search
            Sessions->FindSessions(0, SessionSearch.ToSharedRef());
        }
    }
}

void UMySessionManager::OnFindSessionsComplete(bool bWasSuccessful)
{
    if (bWasSuccessful && SessionSearch.IsValid())
    {
        UE_LOG(LogTemp, Log, TEXT("Found %d sessions"), SessionSearch->SearchResults.Num());
        
        // Join first available session
        if (SessionSearch->SearchResults.Num() > 0)
        {
            JoinSession(SessionSearch->SearchResults[0]);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No sessions found"));
    }
}

void UMySessionManager::JoinSession(const FOnlineSessionSearchResult& SessionResult)
{
    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
    if (OnlineSubsystem)
    {
        IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
        if (Sessions.IsValid())
        {
            // Bind delegate
            Sessions->AddOnJoinSessionCompleteDelegate_Handle(
                FOnJoinSessionCompleteDelegate::CreateUObject(this, &UMySessionManager::OnJoinSessionComplete)
            );
            
            // Join the session
            Sessions->JoinSession(0, FName("GameSession"), SessionResult);
        }
    }
}

void UMySessionManager::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    if (Result == EOnJoinSessionCompleteResult::Success)
    {
        UE_LOG(LogTemp, Log, TEXT("Joined session: %s"), *SessionName.ToString());
        
        // Get connect string and travel
        IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
        if (OnlineSubsystem)
        {
            IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
            if (Sessions.IsValid())
            {
                FString ConnectInfo;
                if (Sessions->GetResolvedConnectString(SessionName, ConnectInfo))
                {
                    UE_LOG(LogTemp, Log, TEXT("Connect string: %s"), *ConnectInfo);
                    // ClientTravel to the session
                }
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to join session"));
    }
}
```

## Step 6: Blueprint Integration

You can also expose the functionality to Blueprints:

```cpp
// MyOnlineGameInstance.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "MyOnlineGameInstance.generated.h"

UCLASS()
class MYPROJECT_API UMyOnlineGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Online")
    void CreateOnlineSession(int32 NumPublicConnections);
    
    UFUNCTION(BlueprintCallable, Category = "Online")
    void FindOnlineSessions();
    
    UFUNCTION(BlueprintCallable, Category = "Online")
    void JoinOnlineSessionByIndex(int32 SessionIndex);
    
    UFUNCTION(BlueprintCallable, Category = "Online")
    void DestroySession();
};
```

## Step 7: Testing

### Local Testing

1. Build and launch two instances of your project
2. In the first instance, create a session
3. In the second instance, find and join the session
4. Verify that ICE candidates are being gathered and connections established

### Console Commands

Use these console commands for debugging:

```
log LogOnlineICE VeryVerbose
log LogNet VeryVerbose
```

## Troubleshooting

### No Candidates Gathered

- Check that your STUN server is reachable
- Verify firewall settings allow UDP traffic
- Ensure the STUN server address is correctly configured

### Connection Fails

- Try different STUN servers
- Configure a TURN server for relay
- Check NAT type (symmetric NAT may require TURN)

### Compilation Errors

- Ensure all dependencies are listed in Build.cs
- Regenerate project files
- Clean and rebuild the solution

## Next Steps

- Implement a signaling server for automatic candidate exchange
- Add encryption using DTLS
- Implement matchmaking service integration
- Add lobby system support

## Additional Resources

- [Unreal Engine Online Subsystem Overview](https://docs.unrealengine.com/en-US/ProgrammingAndScripting/Online/)
- [ICE Protocol RFC 8445](https://tools.ietf.org/html/rfc8445)
- [STUN Protocol RFC 5389](https://tools.ietf.org/html/rfc5389)
