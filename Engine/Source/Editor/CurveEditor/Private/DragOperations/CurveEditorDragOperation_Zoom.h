// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ICurveEditorDragOperation.h"

class FCurveEditor;
class SCurveEditorView;

class FCurveEditorDragOperation_Zoom : public ICurveEditorDragOperation
{
public:
	FCurveEditorDragOperation_Zoom(FCurveEditor* CurveEditor, TSharedPtr<SCurveEditorView> InOptionalView);

	virtual void OnBeginDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent) override;
	virtual void OnDrag(FVector2D InitialPosition, FVector2D CurrentPosition, const FPointerEvent& MouseEvent) override;

private:

	FCurveEditor* CurveEditor;
	TSharedPtr<SCurveEditorView> OptionalView;

	FVector2D ZoomFactor;
	double ZoomOriginX, ZoomOriginY;
	double OriginalInputRange, OriginalOutputRange;
};