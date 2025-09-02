#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "VaporSubsystem.generated.h"

UCLASS()
class UVaporSubsystem : public UEngineSubsystem {
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	TSharedPtr<class FVaporExtension, ESPMode::ThreadSafe> VaporExtension;
};
