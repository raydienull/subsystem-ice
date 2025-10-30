# Quick Start Guide

Get up and running with OnlineSubsystemICE in 5 minutes.

## Step 1: Install the Plugin (2 minutes)

### Option A: Project Plugin
```bash
# Navigate to your project root
cd /path/to/YourProject

# Create Plugins folder if it doesn't exist
mkdir -p Plugins

# Copy or clone OnlineSubsystemICE
cp -r /path/to/OnlineSubsystemICE Plugins/
# OR
cd Plugins
git clone https://github.com/raydienull/subsystem-ice.git OnlineSubsystemICE
```

### Option B: Engine Plugin
```bash
# Copy to engine plugins folder
cp -r /path/to/OnlineSubsystemICE /path/to/UnrealEngine/Engine/Plugins/Online/
```

## Step 2: Configure Your Project (1 minute)

Edit `Config/DefaultEngine.ini`:

```ini
[OnlineSubsystem]
DefaultPlatformService=ICE

[OnlineSubsystemICE]
STUNServer=stun.l.google.com:19302
```

## Step 3: Add to Build Dependencies (30 seconds)

Edit `Source/YourProject/YourProject.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] { 
    "Core", "CoreUObject", "Engine", "InputCore",
    "OnlineSubsystem",
    "OnlineSubsystemICE",      // Add this
    "OnlineSubsystemUtils"     // Add this
});
```

## Step 4: Basic Code Example (1 minute)

Create or edit a GameMode class:

```cpp
// YourGameMode.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "YourGameMode.generated.h"

UCLASS()
class YOURPROJECT_API AYourGameMode : public AGameModeBase
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;

private:
    void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
};
```

```cpp
// YourGameMode.cpp
#include "YourGameMode.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"

void AYourGameMode::BeginPlay()
{
    Super::BeginPlay();

    // Get the online subsystem
    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
    if (OnlineSubsystem)
    {
        // Get session interface
        IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
        if (Sessions.IsValid())
        {
            // Bind completion delegate
            Sessions->AddOnCreateSessionCompleteDelegate_Handle(
                FOnCreateSessionCompleteDelegate::CreateUObject(
                    this, &AYourGameMode::OnCreateSessionComplete)
            );

            // Create session settings
            FOnlineSessionSettings SessionSettings;
            SessionSettings.NumPublicConnections = 4;
            SessionSettings.bShouldAdvertise = true;
            SessionSettings.bIsLANMatch = false;
            SessionSettings.bUsesPresence = true;

            // Create the session
            Sessions->CreateSession(0, FName("GameSession"), SessionSettings);
        }
    }
}

void AYourGameMode::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("Session created successfully!"));
        
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
```

## Step 5: Build and Test (30 seconds)

1. **Regenerate Project Files**
   - Right-click YourProject.uproject → Generate Visual Studio project files

2. **Build**
   - Open solution in Visual Studio
   - Build (Ctrl+Shift+B)

3. **Run**
   - Launch editor (F5)
   - Create a level with your GameMode
   - Press Play

4. **Verify**
   - Check Output Log for: `"Session created successfully!"`

## Quick Testing

### Test 1: Single Instance
```
Output Log should show:
LogOnlineICE: OnlineSubsystemICE Initialized Successfully
LogOnlineICE: CreateSession: GameSession for player 0
LogTemp: Session created successfully!
```

### Test 2: Multiple Instances
1. Package your project (File → Package Project → Windows)
2. Run two instances of the packaged game
3. Instance 1: Create session
4. Instance 2: Find and join session

## Common Issues

### "Plugin not found"
- Verify plugin is in Plugins folder
- Regenerate project files
- Clean and rebuild

### "OnlineSubsystem.h not found"
- Add dependencies to Build.cs
- Regenerate project files

### "No candidates gathered"
- Check internet connection
- Verify STUN server: `ping stun.l.google.com`
- Check firewall allows UDP traffic

## Next Steps

- Read [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) for detailed examples
- Configure TURN server for production: [TURN_SETUP.md](TURN_SETUP.md)
- Learn troubleshooting: [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

## Minimal Complete Example

Create a simple test widget in Blueprint:

1. **Create Widget Blueprint**
   - Create Widget → UserWidget → "UI_NetworkTest"

2. **Add Buttons**
   - "Create Session" button
   - "Find Sessions" button
   - "Join Session" button

3. **Bind to C++ Functions**
   - Use your GameMode or GameInstance

4. **Test**
   - Add widget to viewport
   - Click buttons to test functionality

## Blueprint-Only Quick Start

If you prefer Blueprints, create a GameInstance class:

```cpp
// MyGameInstance.h
UCLASS()
class UMyGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Online")
    void CreateOnlineSession();

    UFUNCTION(BlueprintCallable, Category = "Online")
    void FindOnlineSessions();

private:
    void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
    void OnFindSessionsComplete(bool bWasSuccessful);
    
    TSharedPtr<FOnlineSessionSearch> SessionSearch;
};
```

Then expose these functions to Blueprint and call them from widgets.

## That's It!

You now have a working P2P networking setup using OnlineSubsystemICE. 

For production use, make sure to:
- Set up a TURN server for reliable connectivity
- Implement proper error handling
- Add UI feedback for connection states
- Test with various network configurations

Happy networking! 🚀
