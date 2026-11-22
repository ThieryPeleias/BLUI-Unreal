#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BluTestLogListener.generated.h"

// Test-only sink for UBluEye::LogEventEmitter (page title changes and console messages)
UCLASS(Transient, meta = (Hidden))
class UBluTestLogListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void OnLog(const FString& LogText) { Logs.Add(LogText); }

	TArray<FString> Logs;
};
