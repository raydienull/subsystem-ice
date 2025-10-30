// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystemICEPackage.h"

class FOnlineSubsystemICE;

/**
 * Unique net ID implementation for ICE
 */
class FUniqueNetIdICE : public FUniqueNetId
{
public:
	FUniqueNetIdICE();
	explicit FUniqueNetIdICE(const FString& InUniqueNetId);
	explicit FUniqueNetIdICE(FString&& InUniqueNetId);

	virtual FName GetType() const override;
	virtual const uint8* GetBytes() const override;
	virtual int32 GetSize() const override;
	virtual bool IsValid() const override;
	virtual FString ToString() const override;
	virtual FString ToDebugString() const override;

	friend uint32 GetTypeHash(const FUniqueNetIdICE& A)
	{
		return GetTypeHash(A.UniqueNetIdStr);
	}

private:
	FString UniqueNetIdStr;
};

/**
 * Identity interface implementation for ICE
 * Handles player authentication and unique ID generation
 */
class FOnlineIdentityICE : public IOnlineIdentity
{
public:
	FOnlineIdentityICE(FOnlineSubsystemICE* InSubsystem);
	virtual ~FOnlineIdentityICE();

	// IOnlineIdentity Interface
	virtual bool Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials) override;
	virtual bool Logout(int32 LocalUserNum) override;
	virtual bool AutoLogin(int32 LocalUserNum) override;
	virtual TSharedPtr<FUserOnlineAccount> GetUserAccount(const FUniqueNetId& UserId) const override;
	virtual TArray<TSharedPtr<FUserOnlineAccount>> GetAllUserAccounts() const override;
	virtual FUniqueNetIdPtr GetUniquePlayerId(int32 LocalUserNum) const override;
	virtual FUniqueNetIdPtr CreateUniquePlayerId(uint8* Bytes, int32 Size) override;
	virtual FUniqueNetIdPtr CreateUniquePlayerId(const FString& Str) override;
	virtual ELoginStatus::Type GetLoginStatus(int32 LocalUserNum) const override;
	virtual ELoginStatus::Type GetLoginStatus(const FUniqueNetId& UserId) const override;
	virtual FString GetPlayerNickname(int32 LocalUserNum) const override;
	virtual FString GetPlayerNickname(const FUniqueNetId& UserId) const override;
	virtual FString GetAuthToken(int32 LocalUserNum) const override;
	virtual void RevokeAuthToken(const FUniqueNetId& UserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate) override;
	virtual void GetUserPrivilege(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate) override;
	virtual FPlatformUserId GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const override;
	virtual FString GetAuthType() const override;

private:
	/** Reference to the main subsystem */
	FOnlineSubsystemICE* Subsystem;

	/** Logged in users */
	TMap<int32, FUniqueNetIdPtr> UserIds;

	/** Login status for each user */
	TMap<int32, ELoginStatus::Type> UserLoginStatus;

	/** Nicknames for logged in users */
	TMap<FString, FString> UserNicknames;
};

typedef TSharedPtr<FOnlineIdentityICE, ESPMode::ThreadSafe> FOnlineIdentityICEPtr;
