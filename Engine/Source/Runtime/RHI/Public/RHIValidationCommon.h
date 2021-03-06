// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ValidationRHICommon.h: Public Valdation RHI definitions.
=============================================================================*/

#pragma once 

#ifndef ENABLE_RHI_VALIDATION
	#define ENABLE_RHI_VALIDATION	(UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
#endif

#include "RHI.h"
#include "RHIResources.h"
#include "RHIContext.h"
#include "DynamicRHI.h"

class FValidationRHIUtils
{
public:
	static void ValidateCopyTexture(
		FRHITexture*	SourceTexture,
		FRHITexture*	DestTexture,
		FIntVector			CopySize		= FIntVector::ZeroValue,
		const FIntVector&	SourcePosition	= FIntVector::ZeroValue,
		const FIntVector&	DestPosition	= FIntVector::ZeroValue)
	{
		check(SourceTexture);
		check(DestTexture);
		checkf(SourceTexture->GetFormat() == DestTexture->GetFormat(), TEXT("Some RHIs do not allow format conversion by the GPU for transfer operations!"));

		FIntVector SrcSize = SourceTexture->GetSizeXYZ();
		FIntVector DestSize = DestTexture->GetSizeXYZ();
		if (CopySize == FIntVector::ZeroValue)
		{
			CopySize = SrcSize;
		}

		checkf(CopySize.X <= DestSize.X && CopySize.Y <= DestSize.Y, TEXT("Some RHIs can't perform scaling operations [%dx%d to %dx%d] during copies!"), SrcSize.X, SrcSize.Y, DestSize.X, DestSize.Y);

		check(SourcePosition.X >= 0 && SourcePosition.Y >= 0 && SourcePosition.Z >= 0);
		check(SourcePosition.X + CopySize.X <= SrcSize.X && SourcePosition.Y + CopySize.Y <= SrcSize.Y);

		check(DestPosition.X >= 0 && DestPosition.Y >= 0 && DestPosition.Z >= 0);
		check(DestPosition.X + CopySize.X <= DestSize.X && DestPosition.Y + CopySize.Y <= DestSize.Y);

		if (SourceTexture->GetTexture3D() && DestTexture->GetTexture3D())
		{
			check(SourcePosition.Z + CopySize.Z <= SrcSize.Z);
			check(DestPosition.Z + CopySize.Z <= DestSize.Z);
		}
	}

};

