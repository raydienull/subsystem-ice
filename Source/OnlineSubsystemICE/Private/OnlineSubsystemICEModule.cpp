// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemICEModule.h"
#include "OnlineSubsystemICE.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FOnlineSubsystemICEModule, OnlineSubsystemICE);

DEFINE_LOG_CATEGORY(LogOnlineICE);

void FOnlineSubsystemICEModule::StartupModule()
{
	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Starting"));

	// Create the OnlineSubsystem instance
	FOnlineSubsystemICE::Create(FName(TEXT("ICE")));
	
	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Started"));
}

void FOnlineSubsystemICEModule::ShutdownModule()
{
	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Shutting Down"));

	// Destroy the OnlineSubsystem instance
	FOnlineSubsystemICE::Destroy(FName(TEXT("ICE")));

	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Shutdown Complete"));
}
