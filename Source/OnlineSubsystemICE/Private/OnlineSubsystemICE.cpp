// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemICE.h"
#include "OnlineSessionInterfaceICE.h"
#include "OnlineIdentityInterfaceICE.h"
#include "Misc/ConfigCacheIni.h"

FOnlineSubsystemICE::FOnlineSubsystemICE(FName InInstanceName)
	: FOnlineSubsystemImpl(TEXT("ICE"), InInstanceName)
	, SessionInterface(nullptr)
	, IdentityInterface(nullptr)
{
}

bool FOnlineSubsystemICE::Init()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Initializing OnlineSubsystemICE"));

	// Read configuration from DefaultEngine.ini
	GConfig->GetString(TEXT("OnlineSubsystemICE"), TEXT("STUNServer"), STUNServerAddress, GEngineIni);
	GConfig->GetString(TEXT("OnlineSubsystemICE"), TEXT("TURNServer"), TURNServerAddress, GEngineIni);
	GConfig->GetString(TEXT("OnlineSubsystemICE"), TEXT("TURNUsername"), TURNUsername, GEngineIni);
	GConfig->GetString(TEXT("OnlineSubsystemICE"), TEXT("TURNCredential"), TURNCredential, GEngineIni);

	// Set default values if not configured
	if (STUNServerAddress.IsEmpty())
	{
		STUNServerAddress = TEXT("stun.l.google.com:19302");
	}

	UE_LOG(LogOnlineICE, Log, TEXT("STUN Server: %s"), *STUNServerAddress);
	UE_LOG(LogOnlineICE, Log, TEXT("TURN Server: %s"), *TURNServerAddress);

	// Create interfaces
	SessionInterface = MakeShared<FOnlineSessionICE, ESPMode::ThreadSafe>(this);
	IdentityInterface = MakeShared<FOnlineIdentityICE, ESPMode::ThreadSafe>(this);

	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Initialized Successfully"));
	return true;
}

bool FOnlineSubsystemICE::Shutdown()
{
	UE_LOG(LogOnlineICE, Log, TEXT("Shutting down OnlineSubsystemICE"));

	SessionInterface = nullptr;
	IdentityInterface = nullptr;

	return true;
}

FString FOnlineSubsystemICE::GetAppId() const
{
	return TEXT("ICE");
}

bool FOnlineSubsystemICE::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FOnlineSubsystemImpl::Exec(InWorld, Cmd, Ar))
	{
		return true;
	}
	return false;
}

FText FOnlineSubsystemICE::GetOnlineServiceName() const
{
	return NSLOCTEXT("OnlineSubsystemICE", "OnlineServiceName", "ICE");
}

bool FOnlineSubsystemICE::IsEnabled() const
{
	return true;
}

bool FOnlineSubsystemICE::Tick(float DeltaTime)
{
	if (!FOnlineSubsystemImpl::Tick(DeltaTime))
	{
		return false;
	}

	if (SessionInterface.IsValid())
	{
		SessionInterface->Tick(DeltaTime);
	}

	return true;
}

IOnlineSessionPtr FOnlineSubsystemICE::GetSessionInterface() const
{
	return SessionInterface;
}

IOnlineIdentityPtr FOnlineSubsystemICE::GetIdentityInterface() const
{
	return IdentityInterface;
}
