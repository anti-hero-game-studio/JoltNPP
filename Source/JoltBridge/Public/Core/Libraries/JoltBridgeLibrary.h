// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "JoltBridgeLogChannels.h"
#include "Engine/World.h"

#include "PhysicalMaterials/PhysicalMaterial.h"
#include "JoltBridgeMain.h"
#include "Core/DataTypes/JoltBridgeTypes.h"
#include "Core/Simulation/JoltPhysicsMaterial.h"
#include "Jolt/Physics/Body/Body.h"


// Jolt scale is 1=1m, UE is 1=1cm
// So x100
#define JOLT_TO_WORLD_SCALE 100.f
#define WORLD_TO_JOLT_SCALE 0.01f

class JoltHelpers
{
public:
	
	struct FJoltCollisionMap
	{
		FJoltCollisionMap()
		{
			
		}
		
		FJoltCollisionMap(const TEnumAsByte<ECollisionChannel>& InChannel, const uint32& InResponseMask)
			: Channel(InChannel), ResponseMask(InResponseMask)
		{
			
		}
		uint32 Channel = 0;
		uint32 ResponseMask = 0;
	};
	// Sizes, float
	inline static float ToUnrealFloat(const float& Sz)
	{
		return Sz * JOLT_TO_WORLD_SCALE;
	}

	inline static float ToJoltFloat(const float& Sz)
	{
		return Sz * WORLD_TO_JOLT_SCALE;
	}

	inline static JPH::Float3 ToJoltFloat3(const FVector3f& fvector3f)
	{
		return JPH::Float3(fvector3f.X * WORLD_TO_JOLT_SCALE, fvector3f.Z * WORLD_TO_JOLT_SCALE, fvector3f.Y * WORLD_TO_JOLT_SCALE);
	}

	inline static JPH::Vec3 ToJoltVector3(const FVector& Sv, const bool& adjustScale = true)
	{
		if (adjustScale)
			return JPH::Vec3(Sv.X, Sv.Z, Sv.Y) * WORLD_TO_JOLT_SCALE;

		return JPH::Vec3(Sv.X, Sv.Z, Sv.Y);
	}

	inline static FVector ToUnrealVector3(const JPH::Vec3& Sv, const bool& adjustScale = true)
	{
		if (adjustScale)
			return FVector(Sv.GetX(), Sv.GetZ(), Sv.GetY()) * JOLT_TO_WORLD_SCALE;

		return FVector(Sv.GetX(), Sv.GetZ(), Sv.GetY());
	}
	
	inline static FVector ToUnrealNormal(const JPH::Vec3& Sv)
	{
		return FVector(Sv.GetX(), Sv.GetZ(), Sv.GetY()).GetSafeNormal();
	}

	inline static FVector ToUnrealPosition(const JPH::RVec3& V, const FVector& WorldOrigin = FVector(0))
	{

		return FVector(V.GetX(), V.GetZ(), V.GetY()) * JOLT_TO_WORLD_SCALE + WorldOrigin;
	}

	inline static JPH::RVec3 ToJoltPosition(const FVector& V, const FVector& WorldOrigin = FVector(0))
	{
		return JPH::RVec3(V.X - WorldOrigin.X, V.Z - WorldOrigin.Z, V.Y - WorldOrigin.Y) * WORLD_TO_JOLT_SCALE;
	}

	inline static FQuat ToUnrealRotation(const JPH::Quat& Q)
	{
		return FQuat(-Q.GetX(), -Q.GetZ(), -Q.GetY(), Q.GetW());
	}

	inline static JPH::Quat ToJoltRotation(const FQuat& Q)
	{
		return JPH::Quat(-Q.X, -Q.Z, -Q.Y, Q.W);
	}

	inline static JPH::Quat ToJoltRotation(const FRotator& r)
	{
		return ToJoltRotation(r.Quaternion());
	}

	// Transforms
	inline static FTransform ToUnrealTransform(const JPH::RMat44& T, const FVector& WorldOrigin = FVector(0))
	{
		const FQuat	  Rot = ToUnrealRotation(T.GetQuaternion());
		const FVector Pos = ToUnrealPosition(T.GetTranslation(), WorldOrigin);
		return FTransform(Rot, Pos);
	}

	inline static JPH::RMat44 ToJoltTransform(const FTransform& T)
	{
		return JPH::RMat44::sRotationTranslation(ToJoltRotation(T.GetRotation()), ToJoltPosition(T.GetTranslation()));
	}

	inline static FColor ToUnrealColor(const JPH::Color& C)
	{
		return FColor(C.r, C.g, C.b, C.a);
	}

	inline static void UnrealTrace([[maybe_unused]] const char* inFMT, ...)
	{
		va_list args;
		va_start(args, inFMT);

		char buffer[1024];
		vsnprintf(buffer, sizeof(buffer), inFMT, args);

		va_end(args);

		UE_LOG(LogJoltBridge, Warning, TEXT("JoltPhysicsSubSystem: %s"), ANSI_TO_TCHAR(buffer));
	}

	inline static bool UEAssertFailed(const char* inExpression, const char* inMessage, const char* inFile, uint inLine)
	{
		UE_LOG(LogJoltBridge, Error, TEXT("Assertion failed!"));
		UE_LOG(LogJoltBridge, Error, TEXT("Expression: %s"), ANSI_TO_TCHAR(inExpression));
		if (inMessage)
		{
			UE_LOG(LogJoltBridge, Error, TEXT("Message: %s"), ANSI_TO_TCHAR(inMessage));
		}
		UE_LOG(LogJoltBridge, Error, TEXT("File: %s:%u"), ANSI_TO_TCHAR(inFile), inLine);

		return false;
	}

	inline static void GenerateAssetNames(const UWorld* World, FString& OutPackageName, FString& OutAssetName)
	{
		FString LevelName = TEXT("UnknownLevel");
		if (World)
		{
			LevelName = World->GetMapName();
			LevelName.RemoveFromStart(World->StreamingLevelsPrefix);
		}

		OutPackageName = FString::Printf(TEXT("/Game/JoltData/BinaryData_%s"), *LevelName);
		OutAssetName = FString::Printf(TEXT("BinaryData_%s"), *LevelName);
	}

	inline static JoltPhysicsMaterial* ToJoltPhysicsMaterial(const UPhysicalMaterial* UEPhysicsMat)
	{
		if (!UEPhysicsMat)
			return nullptr;

		JoltPhysicsMaterial* dst = new JoltPhysicsMaterial();
		dst->Friction = UEPhysicsMat->Friction;
		dst->Restitution = UEPhysicsMat->Restitution;
		dst->SurfaceType = UEPhysicsMat->SurfaceType;
		return dst;
	}

	inline static UPhysicalMaterial* ToUEPhysicsMaterial(const JoltPhysicsMaterial* UEPhysicsMat)
	{
		if (!UEPhysicsMat)
			return nullptr;

		UPhysicalMaterial* dst = NewObject<UPhysicalMaterial>();
		dst->Friction = UEPhysicsMat->Friction;
		dst->Restitution = UEPhysicsMat->Restitution;
		dst->SurfaceType = UEPhysicsMat->SurfaceType;
		return dst;
	}

	inline static FString EMotionTypeToString(JPH::EMotionType MotionType)
	{
		switch (MotionType)
		{
			case JPH::EMotionType::Kinematic:
			{
				return TEXT("Kinematic");
			}
			case JPH::EMotionType::Dynamic:
			{
				return TEXT("Dynamic");
			}
			case JPH::EMotionType::Static:
			{
				return TEXT("Static");
			}
			default:
			{
				return TEXT("Invalid Motion type");
			}
		}
	}
	
	static inline void PackDataToGroupIDs(const void* Data, uint32& OutLo, uint32& OutHi)
	{
		const uintptr_t P = reinterpret_cast<uintptr_t>(Data);
		OutLo = static_cast<uint32>(P & 0xFFFFFFFFu);
		OutHi = static_cast<uint32>((P >> 32) & 0xFFFFFFFFu);
	}

	template <typename T>
	static inline T* UnpackDataFromGroupIDs(const uint32 Lo, const uint32 Hi)
	{
		const uintptr_t P = (static_cast<uintptr_t>(Hi) << 32) | static_cast<uintptr_t>(Lo);
		return reinterpret_cast<T*>(P);
	}
	
	static bool IsAnyCollisionAllowed(const FJoltUserData* A, const FJoltUserData* B)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltHelpers::IsAnyCollisionAllowed);
		
		if (!A || !B) return false;
		
		// ObjectChannel must be 0..31
		const uint32 ChanA = static_cast<uint32>(A->ObjectChannel) & 31u;
		const uint32 ChanB = static_cast<uint32>(B->ObjectChannel) & 31u;

		const uint32 BitA = 1u << ChanA;
		const uint32 BitB = 1u << ChanB;

		// UE "blocking" convention for two-way interaction:
		// A blocks B's channel AND B blocks A's channel.
		const bool bABlocksB = (A->CombinedMask & BitB) != 0;
		const bool bBBlocksA = (B->CombinedMask & BitA) != 0;

		return bABlocksB && bBBlocksA;
	}
	
	static bool IsBlockingCollisionAllowed(const FJoltUserData* A, const FJoltUserData* B)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltHelpers::IsBlockingCollisionAllowed);
		
		if (!A || !B) return false;

		// ObjectChannel must be 0..31
		const uint32 ChanA = static_cast<uint32>(A->ObjectChannel) & 31u;
		const uint32 ChanB = static_cast<uint32>(B->ObjectChannel) & 31u;

		const uint32 BitA = 1u << ChanA;
		const uint32 BitB = 1u << ChanB;

		// UE "blocking" convention for two-way interaction:
		// A blocks B's channel AND B blocks A's channel.
		const bool bABlocksB = (A->BlockMask & BitB) != 0;
		const bool bBBlocksA = (B->BlockMask & BitA) != 0;

		return bABlocksB && bBBlocksA;
	}
	
	static bool IsOverlappingCollisionAllowed(const JPH::Body* A, const JPH::Body* B)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltHelpers::IsOverlappingCollisionAllowed);
		
		if (!A || !B) return false;
		
		const FJoltUserData* UD0 = reinterpret_cast<const FJoltUserData*>(A->GetUserData());
		const FJoltUserData* UD1 = reinterpret_cast<const FJoltUserData*>(B->GetUserData());
		
		return IsOverlappingCollisionAllowed(UD0, UD1);
	}
	
	static bool IsOverlappingCollisionAllowed(const FJoltUserData* A, const FJoltUserData* B)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltHelpers::IsOverlappingCollisionAllowed);
		
		if (!A || !B) return false;
		
		// ObjectChannel must be 0..31
		const uint32 ChanA = static_cast<uint32>(A->ObjectChannel) & 31u;
		const uint32 ChanB = static_cast<uint32>(B->ObjectChannel) & 31u;

		const uint32 BitA = 1u << ChanA;
		const uint32 BitB = 1u << ChanB;

		// UE "overlapping" convention for two-way interaction:
		// A blocks B's channel AND B blocks A's channel.
		const bool bABlocksB = (A->OverlapMask & BitB) != 0;
		const bool bBBlocksA = (B->OverlapMask & BitA) != 0;

		return bABlocksB || bBBlocksA;
	}
	
	static void BuildResponseMasks(
	const FCollisionResponseContainer& Responses,
	uint32& OutBlockMask,
	uint32& OutOverlapMask,
	uint32& OutCombinedMask)
		{
		OutBlockMask   = 0;
		OutOverlapMask = 0;
		OutCombinedMask  = 0;

		// UE supports up to 32 channels in ECollisionChannel (0..31)
		for (int32 i = 0; i < 32; ++i)
		{
			const ECollisionChannel Channel = static_cast<ECollisionChannel>(i);

			// Optional: restrict to object channels only
			// if (!IsObjectChannel(Channel)) { continue; }

			const ECollisionResponse R = Responses.GetResponse(Channel);

			
			const uint32 Bit = (1u << i);
			
			if (R != ECR_Ignore)
			{
				OutCombinedMask |= Bit;
			}
			
			switch (R)
			{
			case ECR_Block:   OutBlockMask   |= Bit; break;
			case ECR_Overlap: OutOverlapMask |= Bit; break;
			}
		}
	}
	
	// Helper: safely get FJoltUserData from a Jolt object
	static FORCEINLINE const FJoltUserData* GetUserData(const JPH::Body* Obj)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(JoltHelpers::GetUserData);
		if (!Obj) return nullptr;
		uint64 P = Obj->GetUserData();

		const FJoltUserData* UD = reinterpret_cast<const FJoltUserData*>(P);
		if (!UD) return nullptr;
		return (UD->Magic == FJoltUserData::MagicValue) ? UD : nullptr;
	}
};


class JoltShapeDataWriter : public JPH::StreamOut
{
public:
	JoltShapeDataWriter(TArray<uint8>& InArray)
		: WrappedArray(InArray)
	{
	}

	virtual void WriteBytes(const void* InData, size_t InNumBytes) override
	{
		int32 CurrentSize = WrappedArray.Num();

		WrappedArray.AddUninitialized(InNumBytes);

		FMemory::Memcpy(WrappedArray.GetData() + CurrentSize, InData, InNumBytes);
	}

	virtual bool IsFailed() const override { return false; };

	TArray<uint8>& GetTArray() { return WrappedArray; }

private:
	TArray<uint8>& WrappedArray;
};

class ShapeDataReader : public JPH::StreamIn
{
public:
	explicit ShapeDataReader(const TArray<uint8>& InData)
		: Data(InData), CurrentPosition(0) {}

	virtual void ReadBytes(void* OutData, size_t InNumBytes) override
	{
		size_t BytesAvailable = Data.Num() - CurrentPosition;
		size_t BytesToRead = FMath::Min(InNumBytes, BytesAvailable);

		if (BytesToRead > 0)
		{
			FMemory::Memcpy(OutData, Data.GetData() + CurrentPosition, BytesToRead);
			CurrentPosition += BytesToRead;
		}
	}

	virtual bool IsEOF() const override
	{
		return CurrentPosition > Data.Num();
	}

	virtual bool IsFailed() const override
	{
		return false;
	}
	
private:
	const TArray<uint8>& Data;
	size_t				 CurrentPosition;
};
