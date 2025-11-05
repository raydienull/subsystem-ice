// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemICEModule.h"
#include "OnlineSubsystemICE.h"
#include "OnlineSessionInterfaceICE.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemNames.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ICEAgent.h"

IMPLEMENT_MODULE(FOnlineSubsystemICEModule, OnlineSubsystemICE);

DEFINE_LOG_CATEGORY(LogOnlineICE);

namespace
{
	/**
	 * Helper function to find the active game world
	 * @return Pointer to the game world, or nullptr if not found
	 */
	UWorld* FindGameWorld()
	{
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}

	/**
	 * Helper function to create default session settings for ICE
	 * @return Session settings with default values
	 */
	FOnlineSessionSettings CreateDefaultSessionSettings()
	{
		FOnlineSessionSettings SessionSettings;
		SessionSettings.NumPublicConnections = ICE_DEFAULT_MAX_PLAYERS;
		SessionSettings.bShouldAdvertise = true;
		SessionSettings.bAllowJoinInProgress = true;
		SessionSettings.bIsLANMatch = false;
		SessionSettings.bUsesPresence = true;
		SessionSettings.bAllowInvites = true;
		return SessionSettings;
	}

	/**
	 * Helper function to get the ICE session interface
	 * @return Pointer to the ICE session interface, or nullptr if not available
	 */
	FOnlineSessionICE* GetICESessionInterface()
	{
		IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(FName(TEXT("ICE")));
		if (OnlineSub)
		{
			IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
			if (Sessions.IsValid())
			{
				return static_cast<FOnlineSessionICE*>(Sessions.Get());
			}
		}
		return nullptr;
	}
}

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
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.HOST [sessionName] - Host a new game session (simplified)"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.JOIN <sessionName> - Join an existing game session (simplified)"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.SETREMOTEPEER <ip> <port> - Set remote peer address"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.ADDCANDIDATE <candidate> - Add remote ICE candidate"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.LISTCANDIDATES - List local ICE candidates"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.STARTCHECKS - Start connectivity checks"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.STATUS - Show connection status"));
			UE_LOG(LogOnlineICE, Display, TEXT("  ICE.HELP - Show this help"));
		}),
		ECVF_Default
	));

	// ICE HOST
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("ICE.HOST"),
		TEXT("Host a new game session. Usage: ICE.HOST [sessionName] [mapName]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FString SessionName = Args.Num() > 0 ? Args[0] : TEXT("GameSession");
			FString MapName = Args.Num() > 1 ? Args[1] : TEXT("");
			
			IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get(FName(TEXT("ICE")));
			if (OnlineSubsystem)
			{
				IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
				if (SessionInterface.IsValid())
				{
					// Check if session already exists
					FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(FName(*SessionName));
					if (ExistingSession)
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("ICE.HOST: Session '%s' already exists. Destroy it first."), *SessionName);
						return;
					}
					
					// Create session settings
					FOnlineSessionSettings SessionSettings = CreateDefaultSessionSettings();
					
					// Bind to completion delegate with self-cleanup
					// Note: Capturing SessionName and MapName by value to ensure they outlive the async callback
					TSharedPtr<FDelegateHandle> DelegateHandlePtr = MakeShared<FDelegateHandle>();
					TSharedPtr<FDelegateHandle> ConnectionDelegateHandlePtr = MakeShared<FDelegateHandle>();
					
					*DelegateHandlePtr = SessionInterface->OnCreateSessionCompleteDelegates.AddLambda([SessionName, MapName, DelegateHandlePtr, ConnectionDelegateHandlePtr](FName InSessionName, bool bWasSuccessful)
					{
						if (bWasSuccessful)
						{
							UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Session '%s' created successfully!"), *SessionName);
							UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Use ICE.LISTCANDIDATES to see your ICE candidates"));
							UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Share candidates with remote peer using your signaling method"));
							
							// Start the session
							IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(FName(TEXT("ICE")));
							if (OnlineSub)
							{
								IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
								if (Sessions.IsValid())
								{
									Sessions->StartSession(InSessionName);
									
									// Get ICE session to bind to connection state changes
									FOnlineSessionICE* ICESession = static_cast<FOnlineSessionICE*>(Sessions.Get());
									if (ICESession)
									{
										// Bind to ICE connection state changes to open listen server when connected
										*ConnectionDelegateHandlePtr = ICESession->OnICEConnectionStateChanged.AddLambda([SessionName, MapName, ConnectionDelegateHandlePtr](FName ChangedSessionName, EICEConnectionState NewState)
										{
											if (ChangedSessionName == FName(*SessionName) && NewState == EICEConnectionState::Connected)
											{
												UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: ICE connection established for session '%s'!"), *SessionName);
												
												// Open listen server
												UWorld* World = FindGameWorld();
												if (World)
												{
													FString TravelURL;
													if (!MapName.IsEmpty())
													{
														// Use specified map
														TravelURL = MapName;
													}
													else
													{
														// Use current map
														TravelURL = World->GetMapName();
													}
													
													// Add listen parameter
													TravelURL += TEXT("?listen");
													
													UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Opening listen server with URL: %s"), *TravelURL);
													World->ServerTravel(TravelURL, false);
												}
												else
												{
													UE_LOG(LogOnlineICE, Warning, TEXT("ICE.HOST: Could not find game world to open listen server"));
												}
												
												// Clean up connection delegate after opening listen server
												IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(FName(TEXT("ICE")));
												if (OnlineSub)
												{
													IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
													if (Sessions.IsValid())
													{
														FOnlineSessionICE* ICESession = static_cast<FOnlineSessionICE*>(Sessions.Get());
														if (ICESession)
														{
															ICESession->OnICEConnectionStateChanged.Remove(*ConnectionDelegateHandlePtr);
														}
													}
												}
											}
										});
										
										UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Will automatically open listen server when ICE connection is established"));
									}
									
									// Clean up session creation delegate after use
									Sessions->ClearOnCreateSessionCompleteDelegate_Handle(*DelegateHandlePtr);
								}
							}
						}
						else
						{
							UE_LOG(LogOnlineICE, Warning, TEXT("ICE.HOST: Failed to create session '%s'"), *SessionName);
							
							// Clean up delegate even on failure
							IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(FName(TEXT("ICE")));
							if (OnlineSub)
							{
								IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
								if (Sessions.IsValid())
								{
									Sessions->ClearOnCreateSessionCompleteDelegate_Handle(*DelegateHandlePtr);
								}
							}
						}
						// DelegateHandlePtr will be automatically cleaned up by TSharedPtr
					});
					
					// Create the session
					bool bStarted = SessionInterface->CreateSession(0, FName(*SessionName), SessionSettings);
					if (bStarted)
					{
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Creating session '%s'..."), *SessionName);
						if (!MapName.IsEmpty())
						{
							UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Will open map '%s' as listen server when ICE connects"), *MapName);
						}
						else
						{
							UE_LOG(LogOnlineICE, Display, TEXT("ICE.HOST: Will open current map as listen server when ICE connects"));
						}
					}
					else
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("ICE.HOST: Failed to start session creation"));
					}
				}
				else
				{
					UE_LOG(LogOnlineICE, Warning, TEXT("ICE.HOST: Session interface not available"));
				}
			}
			else
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("ICE.HOST: OnlineSubsystemICE not initialized"));
			}
		}),
		ECVF_Default
	));

	// ICE JOIN
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("ICE.JOIN"),
		TEXT("Join an existing game session. Usage: ICE.JOIN <sessionName>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("Usage: ICE.JOIN <sessionName>"));
				return;
			}
			
			FString SessionName = Args[0];
			
			IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get(FName(TEXT("ICE")));
			if (OnlineSubsystem)
			{
				IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
				if (SessionInterface.IsValid())
				{
					// Check if session already exists
					FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(FName(*SessionName));
					if (ExistingSession)
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("ICE.JOIN: Session '%s' already exists. Destroy it first."), *SessionName);
						return;
					}
					
					// Create a mock search result for joining
					// In a real scenario, this would come from FindSessions
					FOnlineSessionSearchResult SearchResult;
					SearchResult.Session.SessionSettings = CreateDefaultSessionSettings();
					
					// Bind to completion delegate with self-cleanup
					// Note: Capturing SessionName by value to ensure it outlives the async callback
					TSharedPtr<FDelegateHandle> DelegateHandlePtr = MakeShared<FDelegateHandle>();
					TSharedPtr<FDelegateHandle> ConnectionDelegateHandlePtr = MakeShared<FDelegateHandle>();
					
					*DelegateHandlePtr = SessionInterface->OnJoinSessionCompleteDelegates.AddLambda([SessionName, DelegateHandlePtr, ConnectionDelegateHandlePtr](FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
					{
						if (Result == EOnJoinSessionCompleteResult::Success)
						{
							UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Joined session '%s' successfully!"), *SessionName);
							UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Use ICE.LISTCANDIDATES to see your ICE candidates"));
							UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Share candidates with remote peer using your signaling method"));
							UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: After exchanging candidates, use ICE.STARTCHECKS to establish P2P connection"));
							
							// Get ICE session to bind to connection state changes
							IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(FName(TEXT("ICE")));
							if (OnlineSub)
							{
								IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
								if (Sessions.IsValid())
								{
									FOnlineSessionICE* ICESession = static_cast<FOnlineSessionICE*>(Sessions.Get());
									if (ICESession)
									{
										// Bind to ICE connection state changes to travel to server when connected
										*ConnectionDelegateHandlePtr = ICESession->OnICEConnectionStateChanged.AddLambda([SessionName, ConnectionDelegateHandlePtr](FName ChangedSessionName, EICEConnectionState NewState)
										{
											if (ChangedSessionName == FName(*SessionName) && NewState == EICEConnectionState::Connected)
											{
												UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: ICE connection established for session '%s'!"), *SessionName);
												
												// Get connect string and travel to server
												IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(FName(TEXT("ICE")));
												if (OnlineSub)
												{
													IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
													if (Sessions.IsValid())
													{
														FString ConnectInfo;
														if (Sessions->GetResolvedConnectString(ChangedSessionName, ConnectInfo))
														{
															UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Connect string: %s"), *ConnectInfo);
															
															// Get player controller and travel to server
															UWorld* World = FindGameWorld();
															if (World)
															{
																APlayerController* PlayerController = World->GetFirstPlayerController();
																if (PlayerController)
																{
																	UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Traveling to server..."));
																	PlayerController->ClientTravel(ConnectInfo, TRAVEL_Absolute);
																}
																else
																{
																	UE_LOG(LogOnlineICE, Warning, TEXT("ICE.JOIN: Could not find player controller to travel"));
																}
															}
															else
															{
																UE_LOG(LogOnlineICE, Warning, TEXT("ICE.JOIN: Could not find game world"));
															}
														}
														else
														{
															UE_LOG(LogOnlineICE, Warning, TEXT("ICE.JOIN: Could not get connect string for session"));
														}
														
														// Clean up connection delegate after traveling
														FOnlineSessionICE* ICESession = static_cast<FOnlineSessionICE*>(Sessions.Get());
														if (ICESession)
														{
															ICESession->OnICEConnectionStateChanged.Remove(*ConnectionDelegateHandlePtr);
														}
													}
												}
											}
										});
										
										UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Will automatically travel to server when ICE connection is established"));
									}
									
									// Clean up session join delegate after use
									Sessions->ClearOnJoinSessionCompleteDelegate_Handle(*DelegateHandlePtr);
								}
							}
						}
						else
						{
							UE_LOG(LogOnlineICE, Warning, TEXT("ICE.JOIN: Failed to join session '%s'"), *SessionName);
							
							// Clean up delegate even on failure
							IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(FName(TEXT("ICE")));
							if (OnlineSub)
							{
								IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
								if (Sessions.IsValid())
								{
									Sessions->ClearOnJoinSessionCompleteDelegate_Handle(*DelegateHandlePtr);
								}
							}
						}
						// DelegateHandlePtr will be automatically cleaned up by TSharedPtr
					});
					
					// Join the session
					bool bStarted = SessionInterface->JoinSession(0, FName(*SessionName), SearchResult);
					if (bStarted)
					{
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Joining session '%s'..."), *SessionName);
						UE_LOG(LogOnlineICE, Display, TEXT("ICE.JOIN: Will automatically travel to server when ICE connects"));
					}
					else
					{
						UE_LOG(LogOnlineICE, Warning, TEXT("ICE.JOIN: Failed to start join session"));
					}
				}
				else
				{
					UE_LOG(LogOnlineICE, Warning, TEXT("ICE.JOIN: Session interface not available"));
				}
			}
			else
			{
				UE_LOG(LogOnlineICE, Warning, TEXT("ICE.JOIN: OnlineSubsystemICE not initialized"));
			}
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
