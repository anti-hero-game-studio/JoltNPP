#pragma once

#include "Components/TextRenderComponent.h"
#include "JoltBridgeMain.h"
#include "DrawDebugHelpers.h"
#include "Core/Libraries/JoltBridgeLibrary.h"

class FJoltDebugRenderer final : public JPH::DebugRendererSimple
{
private:
	UWorld* World;

public:
	FJoltDebugRenderer(UWorld* world)
	{
		World = world;
	}

	virtual void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override
	{
		if (!World)
		{
			UE_LOG(LogJoltBridge, Warning, TEXT("JoltPhysicsSubSystem::DebugRenderer World is null."));
			return;
		}

		FColor RenderColor = World->GetNetMode() == NM_Client ? FColor::Blue : JoltHelpers::ToUnrealColor(inColor);
		DrawDebugLine(World,
			JoltHelpers::ToUnrealPosition(inFrom),
			JoltHelpers::ToUnrealPosition(inTo),
			RenderColor);
	}

	virtual void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow) override
	{
		if (!World)
		{
			UE_LOG(LogJoltBridge, Warning, TEXT("JoltPhysicsSubSystem::DebugRenderer World is null."));
			return;
		}
		FVector V1 = JoltHelpers::ToUnrealPosition(inV1);
		FVector V2 = JoltHelpers::ToUnrealPosition(inV2);
		FVector V3 = JoltHelpers::ToUnrealPosition(inV3);

		FColor Color = World->GetNetMode() == NM_Client ? FColor::Blue : JoltHelpers::ToUnrealColor(inColor);

		DrawDebugLine(World, V1, V2, Color, false, -1, 0, 1);
		DrawDebugLine(World, V2, V3, Color, false, -1, 0, 1);
		DrawDebugLine(World, V3, V1, Color, false, -1, 0, 1);
	}
	
	

	virtual void DrawText3D(JPH::RVec3Arg inPosition, const JPH::string_view& inString, JPH::ColorArg inColor, float inHeight) override
	{
		if (!World)
		{
			UE_LOG(LogJoltBridge, Warning, TEXT("JoltPhysicsSubSystem::DebugRenderer World is null."));
			return;
		}

		FVector Position = JoltHelpers::ToUnrealPosition(inPosition);
		FColor Color = World->GetNetMode() == NM_Client ? FColor::Blue : JoltHelpers::ToUnrealColor(inColor);

		FString TextString(inString.data());

		UTextRenderComponent* TextRenderComponent = NewObject<UTextRenderComponent>(World);
		if (!TextRenderComponent)
		{
			UE_LOG(LogJoltBridge, Warning, TEXT("Failed to create UTextRenderComponent."));
			return;
		}

		TextRenderComponent->SetText(FText::FromString(TextString));
		TextRenderComponent->SetTextRenderColor(Color);
		TextRenderComponent->SetWorldLocation(Position);
		TextRenderComponent->SetWorldScale3D(FVector(inHeight / 100.0f));

		TextRenderComponent->RegisterComponentWithWorld(World);
	}
};
