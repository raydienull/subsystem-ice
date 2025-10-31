// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemICEModule.h"
#include "OnlineSubsystemICE.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemNames.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FOnlineSubsystemICEModule, OnlineSubsystemICE);

DEFINE_LOG_CATEGORY(LogOnlineICE);

/**
 * Class responsible for creating instances of the ICE online subsystem
 */
class FOnlineFactoryICE : public IOnlineFactory
{
public:
	FOnlineFactoryICE() {}
	virtual ~FOnlineFactoryICE() {}

	virtual IOnlineSubsystemPtr CreateSubsystem(FName InstanceName) override
	{
		FOnlineSubsystemICEPtr OnlineSubsystem = MakeShared<FOnlineSubsystemICE, ESPMode::ThreadSafe>(InstanceName);
		if (OnlineSubsystem->IsEnabled())
		{
			if (!OnlineSubsystem->Init())
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("ICE API failed to initialize!"));
				OnlineSubsystem->Shutdown();
				OnlineSubsystem = nullptr;
			}
		}
		else
		{
			UE_LOG(LogOnlineICE, Warning, TEXT("ICE API disabled!"));
			OnlineSubsystem->Shutdown();
			OnlineSubsystem = nullptr;
		}

		return OnlineSubsystem;
	}
};

FOnlineSubsystemICEModule::FOnlineSubsystemICEModule()
	: ICEFactory(nullptr)
{
}

void FOnlineSubsystemICEModule::StartupModule()
{
	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Starting"));

	// Create and register the factory with the online subsystem manager
	ICEFactory = new FOnlineFactoryICE();
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.RegisterPlatformService(FName(TEXT("ICE")), ICEFactory);
	
	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Started"));
}

void FOnlineSubsystemICEModule::ShutdownModule()
{
	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Shutting Down"));

	// Unregister the factory
	if (ICEFactory)
	{
		FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
		OSS.UnregisterPlatformService(FName(TEXT("ICE")));
		
		delete ICEFactory;
		ICEFactory = nullptr;
	}

	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Shutdown Complete"));
}
