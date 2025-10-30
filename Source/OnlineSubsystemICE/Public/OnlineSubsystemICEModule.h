// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Online subsystem module class for ICE (Interactive Connectivity Establishment)
 * Manages the lifecycle of the OnlineSubsystemICE
 */
class FOnlineSubsystemICEModule : public IModuleInterface
{
public:
	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override { return false; }
	virtual bool SupportsAutomaticShutdown() override { return false; }

private:
	/** Handle to the OnlineSubsystemICE instance */
	class FOnlineSubsystemICE* OnlineSubsystemICE;
};
