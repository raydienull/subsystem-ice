// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemICEModule.h"
#include "OnlineSubsystemICE.h"
#include "OnlineSessionInterfaceICE.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemNames.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"

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
	
	// Register console commands
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	
	// ICE HELP
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("ICE.HELP"),
		TEXT("Show available ICE console commands"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UE_LOG(LogOnlineICE, Display, TEXT("Available ICE commands:"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.SETREMOTEPEER <ip> <port> - Set remote peer address"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.ADDCANDIDATE <candidate> - Add remote ICE candidate"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.LISTCANDIDATES - List local ICE candidates"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.STARTCHECKS - Start connectivity checks"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.STATUS - Show connection status"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.HELP - Show this help"));
		}),
		ECVF_Default
	));

	// ICE SETREMOTEPEER
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("ICE.SETREMOTEPEER"),
		TEXT("Set remote peer address. Usage: ICE.SETREMOTEPEER <ip> <port>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() >= 2)
			{
				FString IPAddress = Args[0];
				int32 Port = FCString::Atoi(*Args[1]);
				
				IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get(FName(TEXT("ICE")));
				if (OnlineSubsystem)
				{
					IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
					if (SessionInterface.IsValid())
					{
						FOnlineSessionICE* ICESession = static_cast<FOnlineSessionICE*>(SessionInterface.Get());
						ICESession->SetRemotePeer(IPAddress, Port);
						UE_LOG(LogOnlineICE, Display, TEXT("ICE: Remote peer set to %s:%d"), *IPAddress, Port);
					}
					else
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("ICE: Session interface not available"));
					}
				}
				else
				{
					UE_LOG(LogOnlineICE, Warning, TEXT("ICE: OnlineSubsystemICE not initialized"));
				}
			}
			else
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("Usage: ICE.SETREMOTEPEER <ip> <port>"));
			}
		}),
		ECVF_Default
	));

	// ICE ADDCANDIDATE
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("ICE.ADDCANDIDATE"),
		TEXT("Add remote ICE candidate. Usage: ICE.ADDCANDIDATE <candidate_string>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() >= 1)
			{
				FString CandidateStr = FString::Join(Args, TEXT(" "));
				
				IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get(FName(TEXT("ICE")));
				if (OnlineSubsystem)
				{
					IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
					if (SessionInterface.IsValid())
					{
						FOnlineSessionICE* ICESession = static_cast<FOnlineSessionICE*>(SessionInterface.Get());
						ICESession->AddRemoteICECandidate(CandidateStr);
						UE_LOG(LogOnlineICE, Display, TEXT("ICE: Added remote candidate: %s"), *CandidateStr);
					}
					else
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("ICE: Session interface not available"));
					}
				}
				else
				{
					UE_LOG(LogOnlineICE, Warning, TEXT("ICE: OnlineSubsystemICE not initialized"));
				}
			}
			else
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("Usage: ICE.ADDCANDIDATE <candidate_string>"));
			}
		}),
		ECVF_Default
	));

	// ICE LISTCANDIDATES
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("ICE.LISTCANDIDATES"),
		TEXT("List local ICE candidates"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get(FName(TEXT("ICE")));
			if (OnlineSubsystem)
			{
				IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
				if (SessionInterface.IsValid())
				{
					FOnlineSessionICE* ICESession = static_cast<FOnlineSessionICE*>(SessionInterface.Get());
					TArray<FString> Candidates = ICESession->GetLocalICECandidates();
					UE_LOG(LogOnlineICE, Display, TEXT("ICE: Local candidates (%d):"), Candidates.Num());
					for (const FString& Candidate : Candidates)
					{
						UE_LOG(LogOnlineICE, Display, TEXT("  %s"), *Candidate);
					}
				}
				else
				{
					UE_LOG(LogOnlineICE, Warning, TEXT("ICE: Session interface not available"));
				}
			}
			else
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("ICE: OnlineSubsystemICE not initialized"));
			}
		}),
		ECVF_Default
	));

	// ICE STARTCHECKS
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("ICE.STARTCHECKS"),
		TEXT("Start ICE connectivity checks"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get(FName(TEXT("ICE")));
			if (OnlineSubsystem)
			{
				IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
				if (SessionInterface.IsValid())
				{
					FOnlineSessionICE* ICESession = static_cast<FOnlineSessionICE*>(SessionInterface.Get());
					bool bSuccess = ICESession->StartICEConnectivityChecks();
					UE_LOG(LogOnlineICE, Display, TEXT("ICE: Connectivity checks %s"), bSuccess ? TEXT("started") : TEXT("failed"));
				}
				else
				{
					UE_LOG(LogOnlineICE, Warning, TEXT("ICE: Session interface not available"));
				}
			}
			else
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("ICE: OnlineSubsystemICE not initialized"));
			}
		}),
		ECVF_Default
	));

	// ICE STATUS
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("ICE.STATUS"),
		TEXT("Show ICE connection status"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get(FName(TEXT("ICE")));
			if (OnlineSubsystem)
			{
				IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
				if (SessionInterface.IsValid())
				{
					FOnlineSessionICE* ICESession = static_cast<FOnlineSessionICE*>(SessionInterface.Get());
					// Create a temporary output device that logs to console
					class FLogOutputDevice : public FOutputDevice
					{
					public:
						virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
						{
							UE_LOG(LogOnlineICE, Display, TEXT("%s"), V);
						}
					};
					FLogOutputDevice LogDevice;
					ICESession->DumpICEStatus(LogDevice);
				}
				else
				{
					UE_LOG(LogOnlineICE, Warning, TEXT("ICE: Session interface not available"));
				}
			}
			else
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("ICE: OnlineSubsystemICE not initialized"));
			}
		}),
		ECVF_Default
	));
	
	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Started"));
}

void FOnlineSubsystemICEModule::ShutdownModule()
{
	UE_LOG(LogOnlineICE, Log, TEXT("OnlineSubsystemICE Module Shutting Down"));

	// Unregister console commands
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject* Command : ConsoleCommands)
	{
		ConsoleManager.UnregisterConsoleObject(Command);
	}
	ConsoleCommands.Empty();

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
