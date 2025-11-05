// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineIdentityInterfaceICE.h"
#include "OnlineSubsystemICE.h"
#include "OnlineError.h"
#include "Misc/Guid.h"

// FUniqueNetIdICE implementation

FUniqueNetIdICE::FUniqueNetIdICE()
	: UniqueNetIdStr()
{
}

FUniqueNetIdICE::FUniqueNetIdICE(const FString& InUniqueNetId)
	: UniqueNetIdStr(InUniqueNetId)
{
}

FUniqueNetIdICE::FUniqueNetIdICE(FString&& InUniqueNetId)
	: UniqueNetIdStr(MoveTemp(InUniqueNetId))
{
}

FName FUniqueNetIdICE::GetType() const
{
	return FName(TEXT("ICE"));
}

const uint8* FUniqueNetIdICE::GetBytes() const
{
	return reinterpret_cast<const uint8*>(UniqueNetIdStr.GetCharArray().GetData());
}

int32 FUniqueNetIdICE::GetSize() const
{
	return UniqueNetIdStr.GetCharArray().Num() * sizeof(TCHAR);
}

bool FUniqueNetIdICE::IsValid() const
{
	return !UniqueNetIdStr.IsEmpty();
}

FString FUniqueNetIdICE::ToString() const
{
	return UniqueNetIdStr;
}

FString FUniqueNetIdICE::ToDebugString() const
{
	return FString::Printf(TEXT("ICE:%s"), *UniqueNetIdStr);
}

// FOnlineIdentityICE implementation

FOnlineIdentityICE::FOnlineIdentityICE(FOnlineSubsystemICE* InSubsystem)
	: Subsystem(InSubsystem)
{
}

FOnlineIdentityICE::~FOnlineIdentityICE() = default;

bool FOnlineIdentityICE::Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials)
{
	UE_LOG(LogOnlineICE, Log, TEXT("Login for user %d"), LocalUserNum);

	// Generate a unique ID for this user
	FString UniqueId;
	if (!AccountCredentials.Id.IsEmpty())
	{
		UniqueId = AccountCredentials.Id;
	}
	else
	{
		// Generate a GUID-based unique ID
		UniqueId = FGuid::NewGuid().ToString();
	}

	FUniqueNetIdPtr UserId = MakeShared<FUniqueNetIdICE>(UniqueId);
	UserIds.Add(LocalUserNum, UserId);
	UserLoginStatus.Add(LocalUserNum, ELoginStatus::LoggedIn);

	// Store nickname if provided
	if (!AccountCredentials.Token.IsEmpty())
	{
		UserNicknames.Add(UniqueId, AccountCredentials.Token);
	}
	else
	{
		UserNicknames.Add(UniqueId, FString::Printf(TEXT("Player%d"), LocalUserNum));
	}

	TriggerOnLoginCompleteDelegates(LocalUserNum, true, *UserId, TEXT(""));
	TriggerOnLoginStatusChangedDelegates(LocalUserNum, ELoginStatus::NotLoggedIn, ELoginStatus::LoggedIn, *UserId);

	return true;
}

bool FOnlineIdentityICE::Logout(int32 LocalUserNum)
{
	UE_LOG(LogOnlineICE, Log, TEXT("Logout for user %d"), LocalUserNum);

	FUniqueNetIdPtr UserId = GetUniquePlayerId(LocalUserNum);
	if (UserId.IsValid())
	{
		UserLoginStatus.Add(LocalUserNum, ELoginStatus::NotLoggedIn);
		TriggerOnLogoutCompleteDelegates(LocalUserNum, true);
		TriggerOnLoginStatusChangedDelegates(LocalUserNum, ELoginStatus::LoggedIn, ELoginStatus::NotLoggedIn, *UserId);
		
		UserIds.Remove(LocalUserNum);
		return true;
	}

	return false;
}

bool FOnlineIdentityICE::AutoLogin(int32 LocalUserNum)
{
	// Create default credentials for auto-login
	FOnlineAccountCredentials Credentials;
	Credentials.Type = TEXT("ICE");
	return Login(LocalUserNum, Credentials);
}

TSharedPtr<FUserOnlineAccount> FOnlineIdentityICE::GetUserAccount(const FUniqueNetId& UserId) const
{
	// Not implemented for basic version
	return nullptr;
}

TArray<TSharedPtr<FUserOnlineAccount>> FOnlineIdentityICE::GetAllUserAccounts() const
{
	// Not implemented for basic version
	return TArray<TSharedPtr<FUserOnlineAccount>>();
}

FUniqueNetIdPtr FOnlineIdentityICE::GetUniquePlayerId(int32 LocalUserNum) const
{
	const FUniqueNetIdPtr* FoundId = UserIds.Find(LocalUserNum);
	if (FoundId)
	{
		return *FoundId;
	}
	return nullptr;
}

FUniqueNetIdPtr FOnlineIdentityICE::CreateUniquePlayerId(uint8* Bytes, int32 Size)
{
	if (Bytes && Size > 0)
	{
		FString IdStr(Size / sizeof(TCHAR), reinterpret_cast<TCHAR*>(Bytes));
		return MakeShared<FUniqueNetIdICE>(IdStr);
	}
	return nullptr;
}

FUniqueNetIdPtr FOnlineIdentityICE::CreateUniquePlayerId(const FString& Str)
{
	return MakeShared<FUniqueNetIdICE>(Str);
}

ELoginStatus::Type FOnlineIdentityICE::GetLoginStatus(int32 LocalUserNum) const
{
	const ELoginStatus::Type* FoundStatus = UserLoginStatus.Find(LocalUserNum);
	if (FoundStatus)
	{
		return *FoundStatus;
	}
	return ELoginStatus::NotLoggedIn;
}

ELoginStatus::Type FOnlineIdentityICE::GetLoginStatus(const FUniqueNetId& UserId) const
{
	for (const auto& UserPair : UserIds)
	{
		if (*UserPair.Value == UserId)
		{
			return GetLoginStatus(UserPair.Key);
		}
	}
	return ELoginStatus::NotLoggedIn;
}

FString FOnlineIdentityICE::GetPlayerNickname(int32 LocalUserNum) const
{
	FUniqueNetIdPtr UserId = GetUniquePlayerId(LocalUserNum);
	if (UserId.IsValid())
	{
		return GetPlayerNickname(*UserId);
	}
	return FString();
}

FString FOnlineIdentityICE::GetPlayerNickname(const FUniqueNetId& UserId) const
{
	const FString* FoundNickname = UserNicknames.Find(UserId.ToString());
	if (FoundNickname)
	{
		return *FoundNickname;
	}
	return UserId.ToString();
}

FString FOnlineIdentityICE::GetAuthToken(int32 LocalUserNum) const
{
	FUniqueNetIdPtr UserId = GetUniquePlayerId(LocalUserNum);
	if (UserId.IsValid())
	{
		return UserId->ToString();
	}
	return FString();
}

void FOnlineIdentityICE::RevokeAuthToken(const FUniqueNetId& UserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate)
{
	UE_LOG(LogOnlineICE, Warning, TEXT("RevokeAuthToken not implemented"));
	Delegate.ExecuteIfBound(UserId, FOnlineError(false));
}

void FOnlineIdentityICE::GetUserPrivilege(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate, EShowPrivilegeResolveUI ShowResolveUI)
{
	// For a basic implementation, grant all privileges
	Delegate.ExecuteIfBound(UserId, Privilege, static_cast<uint32>(IOnlineIdentity::EPrivilegeResults::NoFailures));
}

FPlatformUserId FOnlineIdentityICE::GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const
{
	for (const auto& UserPair : UserIds)
	{
		if (*UserPair.Value == UniqueNetId)
		{
			return FPlatformUserId::CreateFromInternalId(UserPair.Key);
		}
	}
	return PLATFORMUSERID_NONE;
}

FString FOnlineIdentityICE::GetAuthType() const
{
	return TEXT("ICE");
}
