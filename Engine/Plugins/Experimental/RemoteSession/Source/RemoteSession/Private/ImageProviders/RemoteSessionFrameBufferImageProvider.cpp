// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ImageProviders/RemoteSessionFrameBufferImageProvider.h"
#include "Channels/RemoteSessionFrameBufferChannel.h"
#include "RemoteSession.h"
#include "HAL/IConsoleManager.h"
#include "FrameGrabber.h"
#include "Async/Async.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SWindow.h"

DECLARE_CYCLE_STAT(TEXT("RSFrameBufferCap"), STAT_FrameBufferCapture, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RSImageCompression"), STAT_ImageCompression, STATGROUP_Game);

static int32 FramerateMasterSetting = 0;
static FAutoConsoleVariableRef CVarFramerateOverride(
	TEXT("remote.framerate"), FramerateMasterSetting,
	TEXT("Sets framerate"),
	ECVF_Default);

static int32 FrameGrabberResX = 0;
static FAutoConsoleVariableRef CVarResXOverride(
	TEXT("remote.framegrabber.resx"), FrameGrabberResX,
	TEXT("Sets the desired X resolution"),
	ECVF_Default);

static int32 FrameGrabberResY = 0;
static FAutoConsoleVariableRef CVarResYOverride(
	TEXT("remote.framegrabber.resy"), FrameGrabberResY,
	TEXT("Sets the desired Y resolution"),
	ECVF_Default);

FRemoteSessionFrameBufferImageProvider::FRemoteSessionFrameBufferImageProvider(TSharedPtr<FRemoteSessionImageChannel::FImageSender, ESPMode::ThreadSafe> InImageSender)
{
	ImageSender = InImageSender;
	LastSentImageTime = 0.0;
	ViewportResized = false;
	NumDecodingTasks = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
}

FRemoteSessionFrameBufferImageProvider::~FRemoteSessionFrameBufferImageProvider()
{
	if (TSharedPtr<FSceneViewport> PreviousSceneViewportPinned = SceneViewport.Pin())
	{
		PreviousSceneViewportPinned->SetOnSceneViewportResizeDel(FOnSceneViewportResize());
	}
	if (TSharedPtr<SWindow> SceneViewportWindowPin = SceneViewportWindow.Pin())
	{
		SceneViewportWindowPin->GetOnWindowClosedEvent().RemoveAll(this);
	}
	ReleaseFrameGrabber();
}

void FRemoteSessionFrameBufferImageProvider::ReleaseFrameGrabber()
{
	if (FrameGrabber.IsValid())
	{
		FrameGrabber->Shutdown();
		FrameGrabber = nullptr;
	}
}

void FRemoteSessionFrameBufferImageProvider::SetCaptureFrameRate(int32 InFramerate)
{
	// Set our framerate and quality cvars, if the user hasn't modified them
	if (FramerateMasterSetting == 0)
	{
		CVarFramerateOverride->Set(InFramerate);
	}
}

void FRemoteSessionFrameBufferImageProvider::SetCaptureViewport(TSharedRef<FSceneViewport> Viewport)
{
	if (Viewport != SceneViewport)
	{
		if (TSharedPtr<FSceneViewport> PreviousSceneViewportPinned = SceneViewport.Pin())
		{
			PreviousSceneViewportPinned->SetOnSceneViewportResizeDel(FOnSceneViewportResize());
		}
		if (TSharedPtr<SWindow> SceneViewportWindowPin = SceneViewportWindow.Pin())
		{
			SceneViewportWindowPin->GetOnWindowClosedEvent().RemoveAll(this);
		}

		SceneViewportWindow.Reset();
		SceneViewport = Viewport;

		if (TSharedPtr<SWindow> Window = Viewport->FindWindow())
		{
			SceneViewportWindow = Window;
			Window->GetOnWindowClosedEvent().AddRaw(this, &FRemoteSessionFrameBufferImageProvider::OnWindowClosedEvent);
		}

		CreateFrameGrabber(Viewport);

		// set the listener for the window resize event
		Viewport->SetOnSceneViewportResizeDel(FOnSceneViewportResize::CreateRaw(this, &FRemoteSessionFrameBufferImageProvider::OnViewportResized));
	}
}

void FRemoteSessionFrameBufferImageProvider::OnWindowClosedEvent(const TSharedRef<SWindow>&)
{
	ReleaseFrameGrabber();
	SceneViewport.Reset();
}

void FRemoteSessionFrameBufferImageProvider::CreateFrameGrabber(TSharedRef<FSceneViewport> Viewport)
{
	ReleaseFrameGrabber();

	// For times when we want a specific resolution
	FIntPoint FrameGrabberSize = Viewport->GetSize();
	if (FrameGrabberResX > 0)
	{
		FrameGrabberSize.X = FrameGrabberResX;
	}
	if (FrameGrabberResY > 0)
	{
		FrameGrabberSize.Y = FrameGrabberResY;
	}

	FrameGrabber = MakeShared<FFrameGrabber>(Viewport, FrameGrabberSize);
	FrameGrabber->StartCapturingFrames();
}

void FRemoteSessionFrameBufferImageProvider::Tick(const float InDeltaTime)
{
	if (FrameGrabber.IsValid())
	{
		if (ViewportResized)
		{
			ReleaseFrameGrabber();
			if (TSharedPtr<FSceneViewport> SceneViewportPinned = SceneViewport.Pin())
			{
				CreateFrameGrabber(SceneViewportPinned.ToSharedRef());
			}
			ViewportResized = false;
		}
		SCOPE_CYCLE_COUNTER(STAT_FrameBufferCapture);

		if (FrameGrabber)
		{
			FrameGrabber->CaptureThisFrame(FFramePayloadPtr());

			TArray<FCapturedFrameData> Frames = FrameGrabber->GetCapturedFrames();

			if (Frames.Num())
			{
				const double ElapsedImageTimeMS = (FPlatformTime::Seconds() - LastSentImageTime) * 1000;
				const int32 DesiredFrameTimeMS = 1000 / FramerateMasterSetting;

				// Encoding/decoding can take longer than a frame, so skip if we're still processing the previous frame
				if (NumDecodingTasks->GetValue() == 0 && ElapsedImageTimeMS >= DesiredFrameTimeMS)
				{
					NumDecodingTasks->Increment();

					FCapturedFrameData& LastFrame = Frames.Last();
					FIntPoint Size = LastFrame.BufferSize;
					TArray<FColor> ColorData = MoveTemp(LastFrame.ColorBuffer);
					TWeakPtr<FThreadSafeCounter, ESPMode::ThreadSafe> WeakNumDecodingTasks = NumDecodingTasks;
					TWeakPtr<FRemoteSessionImageChannel::FImageSender, ESPMode::ThreadSafe> WeakImageSender = ImageSender;

					AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [WeakNumDecodingTasks, WeakImageSender, Size, ColorData=MoveTemp(ColorData)]() mutable
					{
						SCOPE_CYCLE_COUNTER(STAT_ImageCompression);

						if (WeakImageSender.IsValid())
						{
							if (TSharedPtr<FThreadSafeCounter, ESPMode::ThreadSafe> NumDecodingTasksPinned = WeakNumDecodingTasks.Pin())
							{
								for (FColor& Color : ColorData)
								{
									Color.A = 255;
								}

								if (TSharedPtr<FRemoteSessionImageChannel::FImageSender, ESPMode::ThreadSafe> ImageSenderPinned = WeakImageSender.Pin())
								{
									ImageSenderPinned->SendRawImageToClients(Size.X, Size.Y, ColorData.GetData(), ColorData.GetAllocatedSize());
								}

								NumDecodingTasksPinned->Decrement();
							}
						}

					});

					LastSentImageTime = FPlatformTime::Seconds();
				}
			}
		}
	}
}

void FRemoteSessionFrameBufferImageProvider::OnViewportResized(FVector2D NewSize)
{
	ViewportResized = true;
}
