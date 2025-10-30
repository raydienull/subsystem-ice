// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Pre-pend to log messages */
#undef ONLINE_LOG_PREFIX
#define ONLINE_LOG_PREFIX TEXT("ICE: ")

/** Logging for OnlineSubsystemICE */
DECLARE_LOG_CATEGORY_EXTERN(LogOnlineICE, Log, All);
