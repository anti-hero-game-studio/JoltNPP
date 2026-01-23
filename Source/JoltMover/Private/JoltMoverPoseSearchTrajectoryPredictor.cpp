// Copyright Epic Games, Inc. All Rights Reserved.

#include "JoltMoverPoseSearchTrajectoryPredictor.h"
 #include "Animation/TrajectoryTypes.h"
 #include "JoltMoverComponent.h"
 #include "MoveLibrary/JoltMovementUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(JoltMoverPoseSearchTrajectoryPredictor)
 
void UJoltMoverTrajectoryPredictor::Predict(FTransformTrajectory& InOutTrajectory,
	int32 NumPredictionSamples, float SecondsPerPredictionSample, int NumHistorySamples)
{
	if (!MoverComponent)
	{
		UE_LOG(LogJoltMover, Log, TEXT("Calling Predict without a Mover Component. This is invalid and the trajectory will not be modified."));
		return;
	}

	Predict(*MoverComponent, InOutTrajectory, NumPredictionSamples, SecondsPerPredictionSample, NumHistorySamples, static_cast<float>(MoverSamplingFrameRate.AsInterval()));
}

void UJoltMoverTrajectoryPredictor::Predict(UJoltMoverComponent& MoverComponent, FTransformTrajectory& InOutTrajectory, int32 NumPredictionSamples, float SecondsPerPredictionSample, int32 NumHistorySamples, float MoverSamplingInterval)
{
	FJoltMoverPredictTrajectoryParams PredictParams;

	// Important: the sampling frequency of the prediction does not necessarily match the output frequency on the trajectory
	float LookAheadTime = NumPredictionSamples * SecondsPerPredictionSample;
	int NumMoverPredictionSamplesRequired =  FMath::FloorToInt32(LookAheadTime / MoverSamplingInterval) + 2;
	
	PredictParams.NumPredictionSamples = NumMoverPredictionSamplesRequired;
	PredictParams.SecondsPerSample = MoverSamplingInterval;
	PredictParams.bUseVisualComponentRoot = true;
	PredictParams.bDisableGravity = true;

	// IMPORTANT! The first sample returned is actually the current state
	TArray<FJoltTrajectorySampleInfo> MoverPredictionSamples = MoverComponent.GetPredictedTrajectory(PredictParams);

	if (InOutTrajectory.Samples.Num() < (NumHistorySamples + NumPredictionSamples))
	{
		UE_LOG(LogJoltMover, Warning, TEXT("InOutTrajectory Samples array does not have enough space for %i predicted samples"), MoverPredictionSamples.Num());
	}
	else
	{
		// Subsample or supersample the mover prediction to the trajectory prediction

		int MoverPredictionSampleIndex = 1; // we start at index 1, since index 0 is actually the current state
		
		// Start at Current position
		float CurrentTime = InOutTrajectory.Samples[NumHistorySamples].TimeInSeconds;	// t == 0 is the last frame, so we need to account for the starting mover state being offset
		FTransform MoverPredictLower = MoverPredictionSamples[0].Transform;
		FTransform MoverPredictUpper = MoverPredictionSamples[1].Transform;
		float TimeInSecondsLower = CurrentTime;
		float TimeInSecondsUpper = CurrentTime + MoverSamplingInterval;
		float AccumulatedSeconds = CurrentTime + SecondsPerPredictionSample; // first prediction sample should be at time for the first prediction

		for (int32 i = 0; i < NumPredictionSamples; ++i)
		{
			// Progress target if necessary
			while (AccumulatedSeconds > TimeInSecondsUpper && MoverPredictionSampleIndex < NumMoverPredictionSamplesRequired - 1)
			{
				MoverPredictionSampleIndex++;

				// Update to next mover prediction value
				MoverPredictLower = MoverPredictionSamples[MoverPredictionSampleIndex - 1].Transform;
				MoverPredictUpper = MoverPredictionSamples[MoverPredictionSampleIndex].Transform;

				TimeInSecondsLower = ((MoverPredictionSampleIndex - 1) * MoverSamplingInterval) + CurrentTime;
				TimeInSecondsUpper = (MoverPredictionSampleIndex * MoverSamplingInterval) + CurrentTime;
			}

			const int PoseSampleIdx = i + NumHistorySamples + 1;

			float T = (AccumulatedSeconds - TimeInSecondsLower) / (TimeInSecondsUpper - TimeInSecondsLower);
			T = FMath::Clamp(T, 0.0f, 1.0f);

			InOutTrajectory.Samples[PoseSampleIdx].Position = FMath::Lerp(MoverPredictLower.GetLocation(), MoverPredictUpper.GetLocation(), T);
			InOutTrajectory.Samples[PoseSampleIdx].Facing = FQuat::Slerp(MoverPredictLower.GetRotation(), MoverPredictUpper.GetRotation(), T);
			InOutTrajectory.Samples[PoseSampleIdx].TimeInSeconds = AccumulatedSeconds;

			AccumulatedSeconds += SecondsPerPredictionSample;
		}
	}
}

void UJoltMoverTrajectoryPredictor::GetGravity(FVector& OutGravityAccel)
{
	if (!MoverComponent)
	{
		UE_LOG(LogJoltMover, Log, TEXT("Calling GetGravity without a Mover Component. Return value will be defaulted."));
		OutGravityAccel = FVector::ZeroVector;
		return;
	}

	OutGravityAccel = MoverComponent->GetGravityAcceleration();
}


void UJoltMoverTrajectoryPredictor::GetCurrentState(FVector& OutPosition, FQuat& OutFacing, FVector& OutVelocity)
{
	if (!MoverComponent)
	{
		UE_LOG(LogJoltMover, Log, TEXT("Calling GetCurrentState without a Mover Component. Return values will be defaulted."));
		OutPosition = OutVelocity = FVector::ZeroVector;
		OutFacing = FQuat::Identity;
		return;
	}

	GetCurrentState(*MoverComponent, OutPosition, OutFacing, OutVelocity);
}

void UJoltMoverTrajectoryPredictor::GetCurrentState(UJoltMoverComponent& MoverComponent, FVector& OutPosition, FQuat& OutFacing, FVector& OutVelocity)
{
	const USceneComponent* VisualComp = MoverComponent.GetPrimaryVisualComponent();

	if (VisualComp)
	{
		OutPosition = VisualComp->GetComponentLocation();
	}
	else
	{
		OutPosition = MoverComponent.GetUpdatedComponentTransform().GetLocation();
	}

	const bool bOrientRotationToMovement = true;

	if (bOrientRotationToMovement)
	{
		if (VisualComp)
		{
			OutFacing = VisualComp->GetComponentRotation().Quaternion();
		}
		else
		{
			OutFacing = MoverComponent.GetUpdatedComponentTransform().GetRotation();
		}
	}
	else
	{
		// JAH TODO: Needs a solve
		//OutFacing = FQuat::MakeFromRotator(FRotator(0, TrajectoryDataState.DesiredControllerYawLastUpdate, 0)) * TrajectoryDataDerived.MeshCompRelativeRotation;
	}

	OutVelocity = MoverComponent.GetVelocity();
}


void UJoltMoverTrajectoryPredictor::GetVelocity(FVector& OutVelocity)
{
	if (!MoverComponent)
	{
		UE_LOG(LogJoltMover, Log, TEXT("Calling GetVelocity without a Mover Component. Return value will be defaulted."));
		OutVelocity = FVector::ZeroVector;
		return;
	}

	OutVelocity = MoverComponent->GetVelocity();
}
