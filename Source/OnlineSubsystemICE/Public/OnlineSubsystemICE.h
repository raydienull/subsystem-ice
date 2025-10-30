// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemImpl.h"
#include "OnlineSubsystemICEPackage.h"

class FOnlineSessionICE;
class FOnlineIdentityICE;
class FSocketSubsystemICE;

/**
 * Main OnlineSubsystem implementation for ICE protocol
 * Provides P2P connectivity using STUN/TURN for NAT traversal
 */
class ONLINESUBSYSTEMICE_API FOnlineSubsystemICE : public FOnlineSubsystemImpl
{
public:
	virtual ~FOnlineSubsystemICE() = default;

	// IOnlineSubsystem Interface
	virtual IOnlineSessionPtr GetSessionInterface() const override;
	virtual IOnlineFriendsPtr GetFriendsInterface() const override { return nullptr; }
	virtual IOnlinePartyPtr GetPartyInterface() const override { return nullptr; }
	virtual IOnlineGroupsPtr GetGroupsInterface() const override { return nullptr; }
	virtual IOnlineSharedCloudPtr GetSharedCloudInterface() const override { return nullptr; }
	virtual IOnlineUserCloudPtr GetUserCloudInterface() const override { return nullptr; }
	virtual IOnlineLeaderboardsPtr GetLeaderboardsInterface() const override { return nullptr; }
	virtual IOnlineVoicePtr GetVoiceInterface() const override { return nullptr; }
	virtual IOnlineExternalUIPtr GetExternalUIInterface() const override { return nullptr; }
	virtual IOnlineTimePtr GetTimeInterface() const override { return nullptr; }
	virtual IOnlineIdentityPtr GetIdentityInterface() const override;
	virtual IOnlineTitleFilePtr GetTitleFileInterface() const override { return nullptr; }
	virtual IOnlineEntitlementsPtr GetEntitlementsInterface() const override { return nullptr; }
	virtual IOnlineStoreV2Ptr GetStoreV2Interface() const override { return nullptr; }
	virtual IOnlinePurchasePtr GetPurchaseInterface() const override { return nullptr; }
	virtual IOnlineEventsPtr GetEventsInterface() const override { return nullptr; }
	virtual IOnlineAchievementsPtr GetAchievementsInterface() const override { return nullptr; }
	virtual IOnlineSharingPtr GetSharingInterface() const override { return nullptr; }
	virtual IOnlineUserPtr GetUserInterface() const override { return nullptr; }
	virtual IOnlineMessagePtr GetMessageInterface() const override { return nullptr; }
	virtual IOnlinePresencePtr GetPresenceInterface() const override { return nullptr; }
	virtual IOnlineChatPtr GetChatInterface() const override { return nullptr; }
	virtual IOnlineStatsPtr GetStatsInterface() const override { return nullptr; }
	virtual IOnlineTurnBasedPtr GetTurnBasedInterface() const override { return nullptr; }
	virtual IOnlineTournamentPtr GetTournamentInterface() const override { return nullptr; }
	
	virtual bool Init() override;
	virtual bool Shutdown() override;
	virtual FString GetAppId() const override;
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
	virtual FText GetOnlineServiceName() const override;
	virtual bool IsEnabled() const override;

	// FTickerObjectBase
	virtual bool Tick(float DeltaTime) override;

PACKAGE_SCOPE:
	/** Only the factory makes instances */
	FOnlineSubsystemICE() = delete;
	explicit FOnlineSubsystemICE(FName InInstanceName);

private:
	/** Interface to the session services */
	FOnlineSessionICEPtr SessionInterface;

	/** Interface to the identity services */
	FOnlineIdentityICEPtr IdentityInterface;

	/** STUN server address */
	FString STUNServerAddress;

	/** TURN server address */
	FString TURNServerAddress;

	/** TURN username */
	FString TURNUsername;

	/** TURN credential */
	FString TURNCredential;
};

typedef TSharedPtr<FOnlineSubsystemICE, ESPMode::ThreadSafe> FOnlineSubsystemICEPtr;
