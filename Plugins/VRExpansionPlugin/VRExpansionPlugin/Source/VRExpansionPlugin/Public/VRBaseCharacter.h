// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "VRBaseCharacterMovementComponent.h"
#include "ReplicatedVRCameraComponent.h"
#include "ParentRelativeAttachmentComponent.h"
#include "GripMotionControllerComponent.h"
#include "VRBaseCharacter.generated.h"

USTRUCT(Blueprintable)
struct VREXPANSIONPLUGIN_API FVRSeatedCharacterInfo
{
	GENERATED_USTRUCT_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category = "CharacterSeatInfo")
		bool bSitting;
	UPROPERTY()
		FVector_NetQuantize100 StoredLocation;
	UPROPERTY()
		float StoredYaw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, NotReplicated, Category = "CharacterSeatInfo")
		float AllowedDistance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, NotReplicated, Category = "CharacterSeatInfo")
		float DistanceThreshold;

	bool bWasSeated;
	bool bOriginalControlRotation;
	
	void Clear()
	{
		StoredLocation = FVector::ZeroVector;
		StoredYaw = 0;
		bWasSeated = false;
		bOriginalControlRotation = false;
	}


	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		Ar.SerializeBits(&bSitting, 1);
		StoredLocation.NetSerialize(Ar, Map, bOutSuccess);

		uint16 val;
		if (Ar.IsSaving())
		{
			val = FRotator::CompressAxisToShort(StoredYaw);
			Ar << val;
		}
		else
		{
			Ar << val;
			StoredYaw = FRotator::DecompressAxisFromShort(val);
		}
		
		return bOutSuccess;
	}
};
template<>
struct TStructOpsTypeTraits< FVRSeatedCharacterInfo > : public TStructOpsTypeTraitsBase2<FVRSeatedCharacterInfo>
{
	enum
	{
		WithNetSerializer = true
	};
};

UCLASS()
class VREXPANSIONPLUGIN_API AVRBaseCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AVRBaseCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// I'm sending it unreliable because it is being resent pretty often
	UFUNCTION(Unreliable, Server, WithValidation)
		void Server_SendTransformCamera(FBPVRComponentPosRep NewTransform);

	UFUNCTION(Unreliable, Server, WithValidation)
		void Server_SendTransformLeftController(FBPVRComponentPosRep NewTransform);

	UFUNCTION(Unreliable, Server, WithValidation)
		void Server_SendTransformRightController(FBPVRComponentPosRep NewTransform);

	// Called when the client is in climbing mode and is stepped up onto a platform
	// Generally you should drop the climbing at this point and go into falling movement.
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "VRMovement")
		void OnClimbingSteppedUp();

	UPROPERTY(BlueprintReadOnly, Transient, Category = "VRExpansionLibrary")
	FTransform OffsetComponentToWorld;

	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
	FVector GetVRForwardVector() const
	{
		return OffsetComponentToWorld.GetRotation().GetForwardVector();
	}

	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
		FVector GetVRRightVector() const
	{
		return OffsetComponentToWorld.GetRotation().GetRightVector();
	}

	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
		FVector GetVRUpVector() const
	{
		return OffsetComponentToWorld.GetRotation().GetUpVector();
	}

	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
		FVector GetVRLocation() const
	{
		return OffsetComponentToWorld.GetLocation();
	}

	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
		FRotator GetVRRotation() const
	{
		return OffsetComponentToWorld.GetRotation().Rotator();
	}


	virtual FVector GetTargetLocation(AActor* RequestedBy) const override
	{
		return GetVRLocation();
	}

	UPROPERTY(BlueprintReadOnly, Replicated, EditAnywhere, Category = "VRSeatComponent", ReplicatedUsing = OnRep_SeatedCharInfo)
	FVRSeatedCharacterInfo SeatInformation;

	// Called when the seated mode is changed
	UFUNCTION(BlueprintImplementableEvent, Category = "VRMovement")
		void OnSeatedModeChanged(bool bNewSeatedMode, bool bWasAlreadySeated);

	// Called when the the player either transitions to/from the threshold boundry or the scaler value of being outside the boundry changes
	// Can be used for warnings or screen darkening, ect
	UFUNCTION(BlueprintImplementableEvent, Category = "VRMovement")
		void OnSeatedModeThreshholdChanged(bool bIsWithinThreshold, float ToThresholdScaler);

	void ZeroToSeatInformation()
	{
		SetActorRelativeRotation(FRotator(0.0f, -SeatInformation.StoredYaw, 0.0f));
		SetActorRelativeLocation(-(VRReplicatedCamera->GetComponentLocation() - GetActorLocation()));

		//SetActorLocationAndRotationVR(GetVRLocation() - GetActorLocation(), FRotator(0.0f, -SeatInformation.StoredYaw, 0.0f), true);
		LeftMotionController->PostTeleportMoveGrippedActors();
		RightMotionController->PostTeleportMoveGrippedActors();
	}
	
	// Called from the movement component
	void TickSeatInformation(float DeltaTime)
	{
		if (USceneComponent * ParentComp = GetCapsuleComponent()->GetAttachParent())
		{
			//SetActorLocation(GetVRLocation() - ParentComp->GetComponentLocation());
			//SetActorRelativeLocation(GetVRLocation() - ParentComp->GetComponentLocation());
		}
	}

	UFUNCTION()
		virtual void OnRep_SeatedCharInfo()
	{
		// Handle setting up the player here

		if (UPrimitiveComponent * root = Cast<UPrimitiveComponent>(GetRootComponent()))
		{
			if (SeatInformation.bSitting && !SeatInformation.bWasSeated)
			{
				if (UCharacterMovementComponent * charMovement = Cast<UCharacterMovementComponent>(GetMovementComponent()))
					charMovement->SetMovementMode(MOVE_Custom, (uint8)EVRCustomMovementMode::VRMOVE_Seated);

				root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				SeatInformation.bWasSeated = true;
				SeatInformation.bOriginalControlRotation = bUseControllerRotationYaw;
				bUseControllerRotationYaw = false; // This forces rotation in world space, something that we don't want

				ZeroToSeatInformation();
				OnSeatedModeChanged(SeatInformation.bSitting, SeatInformation.bWasSeated);
			}
			else if(!SeatInformation.bSitting && SeatInformation.bWasSeated)
			{
				root->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

				if (UCharacterMovementComponent * charMovement = Cast<UCharacterMovementComponent>(GetMovementComponent()))
					charMovement->SetMovementMode(MOVE_Walking);

				// Re-purposing them for the new location and rotations
				SetActorLocationAndRotationVR(SeatInformation.StoredLocation, FRotator(0.0f, SeatInformation.StoredYaw, 0.0f), true);
				LeftMotionController->PostTeleportMoveGrippedActors();
				RightMotionController->PostTeleportMoveGrippedActors();
				bUseControllerRotationYaw = SeatInformation.bOriginalControlRotation;

				OnSeatedModeChanged(SeatInformation.bSitting, SeatInformation.bWasSeated);
				SeatInformation.bWasSeated = false;
			}
			else // Is just a reposition
			{
				ZeroToSeatInformation();
			}
		}
	}

	// Sets seated mode on the character and then fires off an event to handle any special setup
	UFUNCTION(BlueprintCallable, Server, Reliable, WithValidation, Category = "BaseVRCharacter", meta = (DisplayName = "SetSeatedMode"))
		void Server_SetSeatedMode(USceneComponent * SeatParent, bool bSetSeatedMode, FVector_NetQuantize100 UnSeatLoc, float UnSeatYaw);

	// Sets seated mode on the character and then fires off an event to handle any special setup
	// Should only be called on the server / net authority
	//UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "BaseVRCharacter")
	bool SetSeatedMode(USceneComponent * SeatParent, bool bSetSeatedMode, FVector UnSeatLoc, float UnSeatYaw)
	{
		if (!this->HasAuthority())
			return false;

		AController* OwningController = GetController();

		if (bSetSeatedMode)
		{
			//SeatedCharacter.SeatedCharacter = CharacterToSeat;
			SeatInformation.bSitting = true;
			SeatInformation.StoredYaw = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(VRReplicatedCamera->RelativeRotation).Yaw;// bUseControllerRotationYaw && OwningController ? OwningController->GetControlRotation() : GetActorRotation()).Yaw;
			SeatInformation.StoredLocation = VRReplicatedCamera->RelativeLocation;

			//SetReplicateMovement(false);

			if (SeatParent)
				AttachToComponent(SeatParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		else
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			SeatInformation.StoredYaw = UnSeatYaw;
			SeatInformation.StoredLocation = UnSeatLoc;
			//SetActorLocationAndRotationVR(NewWorldLocation - (GetVRLocation() - GetActorLocation()), NewWorldRotation, true);
			//SetReplicateMovement(true);
			SeatInformation.bSitting = false;
		}

		OnRep_SeatedCharInfo(); // Call this on server side because it won't call itself
		NotifyOfTeleport(); // Teleport the controllers

		return true;
	}

	// Adds a rotation delta taking into account the HMD as a pivot point (also moves the actor), returns final location difference
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter|VRLocations")
	FVector AddActorWorldRotationVR(FRotator DeltaRot, bool bUseYawOnly)
	{
			AController* OwningController = GetController();

			FVector NewLocation;
			FRotator NewRotation;
			FVector OrigLocation = GetActorLocation();
			FVector PivotPoint = GetActorTransform().InverseTransformPosition(GetVRLocation());

			NewRotation = bUseControllerRotationYaw && OwningController ? OwningController->GetControlRotation() : GetActorRotation();
			
			if (bUseYawOnly)
			{
				NewRotation.Pitch = 0.0f;
				NewRotation.Roll = 0.0f;
			}

			NewLocation = OrigLocation + NewRotation.RotateVector(PivotPoint);
			NewRotation = (NewRotation.Quaternion() * DeltaRot.Quaternion()).Rotator();
			NewLocation -= NewRotation.RotateVector(PivotPoint);

			if (bUseControllerRotationYaw && OwningController && IsLocallyControlled())
				OwningController->SetControlRotation(NewRotation);

			// Also setting actor rot because the control rot transfers to it anyway eventually
			SetActorLocationAndRotation(NewLocation, NewRotation);
			return NewLocation - OrigLocation;
	}


	// Sets the actors rotation taking into account the HMD as a pivot point (also moves the actor), returns the location difference
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter|VRLocations")
	FVector SetActorRotationVR(FRotator NewRot, bool bUseYawOnly)
	{
		AController* OwningController = GetController();

		FVector NewLocation;
		FRotator NewRotation;
		FVector OrigLocation = GetActorLocation();
		FVector PivotPoint = GetActorTransform().InverseTransformPosition(GetVRLocation());

		NewRotation = bUseControllerRotationYaw && OwningController ? OwningController->GetControlRotation() : GetActorRotation();
		
		if (bUseYawOnly)
		{
			NewRotation.Pitch = 0.0f;
			NewRotation.Roll = 0.0f;
		}

		NewLocation = OrigLocation + NewRotation.RotateVector(PivotPoint);
		NewRotation = NewRot;
		NewLocation -= NewRotation.RotateVector(PivotPoint);

		if (bUseControllerRotationYaw && OwningController && IsLocallyControlled())
			OwningController->SetControlRotation(NewRotation);

		// Also setting actor rot because the control rot transfers to it anyway eventually
		SetActorLocationAndRotation(NewLocation, NewRotation);
		return NewLocation - OrigLocation;
	}	
	
	// Sets the actors rotation and location taking into account the HMD as a pivot point (also moves the actor), returns the location difference from the rotation
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter|VRLocations")
	FVector SetActorLocationAndRotationVR(FVector NewLoc, FRotator NewRot, bool bUseYawOnly)
	{
		AController* OwningController = GetController();

		FVector NewLocation;
		FRotator NewRotation;
		FVector PivotPoint = GetActorTransform().InverseTransformPosition(GetVRLocation());

		NewRotation = bUseControllerRotationYaw && OwningController ? OwningController->GetControlRotation() : GetActorRotation();

		if (bUseYawOnly)
		{
			NewRotation.Pitch = 0.0f;
			NewRotation.Roll = 0.0f;
		}

		NewLocation = NewLoc + NewRotation.RotateVector(PivotPoint);
		NewRotation = NewRot;
		NewLocation -= NewRotation.RotateVector(PivotPoint);

		if (bUseControllerRotationYaw && OwningController && IsLocallyControlled())
			OwningController->SetControlRotation(NewRotation);

		// Also setting actor rot because the control rot transfers to it anyway eventually
		SetActorLocationAndRotation(NewLocation, NewRotation);
		return NewLocation - NewLoc;
	}

	// Regenerates the base offsetcomponenttoworld that VR uses
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter|VRLocations")
	virtual void RegenerateOffsetComponentToWorld(bool bUpdateBounds, bool bCalculatePureYaw)
	{}

	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter")
	virtual void SetCharacterSizeVR(float NewRadius, float NewHalfHeight, bool bUpdateOverlaps = true)
	{
		if (UCapsuleComponent * Capsule = Cast<UCapsuleComponent>(this->RootComponent))
		{		
			if(!FMath::IsNearlyEqual(NewRadius, Capsule->GetUnscaledCapsuleRadius()) || !FMath::IsNearlyEqual(NewHalfHeight,Capsule->GetUnscaledCapsuleHalfHeight()))
				Capsule->SetCapsuleSize(NewRadius, NewHalfHeight, bUpdateOverlaps);
		}
	}

	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter")
	virtual void SetCharacterHalfHeightVR(float HalfHeight, bool bUpdateOverlaps = true)
	{
		if (UCapsuleComponent * Capsule = Cast<UCapsuleComponent>(this->RootComponent))
		{
			if (!FMath::IsNearlyEqual(HalfHeight, Capsule->GetUnscaledCapsuleHalfHeight()))
				Capsule->SetCapsuleHalfHeight(HalfHeight, bUpdateOverlaps);
		}
	}

	// This component is used with the normal character SkeletalMesh network smoothing system for simulated proxies
	// It will lerp the characters components back to zero on simulated proxies after a move is complete.
	// The simplest method of doing this was applying the exact same offset as the mesh gets to a base component that
	// tracked objects are attached to.
	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		USceneComponent * NetSmoother;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UVRBaseCharacterMovementComponent * VRMovementReference;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UReplicatedVRCameraComponent * VRReplicatedCamera;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UParentRelativeAttachmentComponent * ParentRelativeAttachment;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UGripMotionControllerComponent * LeftMotionController;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UGripMotionControllerComponent * RightMotionController;

	/** Name of the LeftMotionController component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName LeftMotionControllerComponentName;

	/** Name of the RightMotionController component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName RightMotionControllerComponentName;

	/** Name of the VRReplicatedCamera component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName ReplicatedCameraComponentName;

	/** Name of the ParentRelativeAttachment component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName ParentRelativeAttachmentComponentName;
	
	/** Name of the ParentRelativeAttachment component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName SmoothingSceneParentComponentName;

	/*
	A helper function that offsets a given vector by the roots collision location
	pass in a teleport location and it provides the correct spot for it to be at your feet
	*/
	UFUNCTION(BlueprintPure, Category = "VRGrip")
		virtual FVector GetTeleportLocation(FVector OriginalLocation);

	UFUNCTION(Reliable, NetMulticast, Category = "VRGrip")
		virtual void NotifyOfTeleport();


	// Event triggered when a move action is performed, this is ran just prior to PerformMovement in the character tick
	UFUNCTION(BlueprintNativeEvent, Category = "VRMovement")
		void OnCustomMoveActionPerformed(EVRMoveAction MoveActionType, FVector MoveActionVector, FRotator MoveActionRotator);
	virtual void OnCustomMoveActionPerformed_Implementation(EVRMoveAction MoveActionType, FVector MoveActionVector, FRotator MoveActionRotator);

	// Event triggered when beginning to be pushed back from a wall
	// bHadLocomotionInput means that the character was moving itself
	// HmdInput is how much the HMD moved in that tick so you can compare sizes to decide what to do
	UFUNCTION(BlueprintNativeEvent, Category = "VRMovement")
		void OnBeginWallPushback(FHitResult HitResultOfImpact, bool bHadLocomotionInput, FVector HmdInput);
	virtual void OnBeginWallPushback_Implementation(FHitResult HitResultOfImpact, bool bHadLocomotionInput, FVector HmdInput);

	// Event triggered when beginning to be pushed back from a wall
	UFUNCTION(BlueprintNativeEvent, Category = "VRMovement")
		void OnEndWallPushback();
	virtual void OnEndWallPushback_Implementation();

	// Event when a navigation pathing operation has completed, auto calls stop movement for VR characters
	UFUNCTION(BlueprintImplementableEvent, Category = "VRBaseCharacter")
		void ReceiveNavigationMoveCompleted(EPathFollowingResult::Type PathingResult);

	virtual void NavigationMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
	{
		this->Controller->StopMovement();
		ReceiveNavigationMoveCompleted(Result.Code);
	}

	UFUNCTION(BlueprintCallable, Category = "VRBaseCharacter")
	EPathFollowingStatus::Type GetMoveStatus() const
	{
		if (!Controller)
			return EPathFollowingStatus::Idle;

		if (UPathFollowingComponent* pathComp = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			pathComp->GetStatus();
		}

		return EPathFollowingStatus::Idle;
	}

	/** Returns true if the current PathFollowingComponent's path is partial (does not reach desired destination). */
	UFUNCTION(BlueprintCallable, Category = "VRBaseCharacter")
	bool HasPartialPath() const
	{
		if (!Controller)
			return false;

		if (UPathFollowingComponent* pathComp = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			return pathComp->HasPartialPath();
		}

		return false;
	}

	// Instantly stops pathing
	UFUNCTION(BlueprintCallable, Category = "VRBaseCharacter")
	void StopNavigationMovement()
	{
		if (!Controller)
			return;

		if (UPathFollowingComponent* pathComp = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			// @note FPathFollowingResultFlags::ForcedScript added to make AITask_MoveTo instances 
			// not ignore OnRequestFinished notify that's going to be sent out due to this call
			pathComp->AbortMove(*this, FPathFollowingResultFlags::MovementStop | FPathFollowingResultFlags::ForcedScript);
		}
	}

	UPROPERTY(BlueprintReadWrite, Category = AI)
		TSubclassOf<UNavigationQueryFilter> DefaultNavigationFilterClass;

	// An extended simple move to location with additional parameters
	UFUNCTION(BlueprintCallable, Category = "VRBaseCharacter", Meta = (AdvancedDisplay = "bStopOnOverlap,bCanStrafe,bAllowPartialPath"))
		virtual void ExtendedSimpleMoveToLocation(const FVector& GoalLocation, float AcceptanceRadius = -1, bool bStopOnOverlap = false,
			bool bUsePathfinding = true, bool bProjectDestinationToNavigation = true, bool bCanStrafe = false,
			TSubclassOf<UNavigationQueryFilter> FilterClass = NULL, bool bAllowPartialPath = true);

};