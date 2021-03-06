﻿//*****************************************************************************
//
//	Copyright 2015 Microsoft Corporation
//
//	Licensed under the Apache License, Version 2.0 (the "License");
//	you may not use this file except in compliance with the License.
//	You may obtain a copy of the License at
//
//	http ://www.apache.org/licenses/LICENSE-2.0
//
//	Unless required by applicable law or agreed to in writing, software
//	distributed under the License is distributed on an "AS IS" BASIS,
//	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//	See the License for the specific language governing permissions and
//	limitations under the License.
//
//*****************************************************************************

//
// MainPage.xaml.cpp
// Implementation of the MainPage class.
//

#include "pch.h"
#include "MainPage.xaml.h"
#include <collection.h>

using namespace FFmpegInterop;
using namespace MediaPlayerCPP;

using namespace concurrency;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Media::Core;
using namespace Windows::Media::Playback;
using namespace Windows::Storage;
using namespace Windows::Storage::AccessCache;
using namespace Windows::Storage::Pickers;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Popups;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

MainPage::MainPage()
{
	Config = ref new FFmpegInteropConfig();
	InitializeComponent();

	// Show the control panel on startup so user can start opening media
	Splitter->IsPaneOpen = true;
	AutoDetect->IsOn = true;

	VideoEffectConfiguration = ref new FFmpegInterop::VideoEffectConfiguration();

	// optionally check for recommended ffmpeg version
	//FFmpegVersionInfo::CheckRecommendedVersion();

	// populate character encodings
	cbEncodings->ItemsSource = CharacterEncoding::GetCharacterEncodings();
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Core::CoreWindow^, Windows::UI::Core::KeyEventArgs^>(this, &MediaPlayerCPP::MainPage::OnKeyDown);
}

void MediaPlayerCPP::MainPage::Page_Loaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto controls = Windows::Media::SystemMediaTransportControls::GetForCurrentView();
	controls->ButtonPressed += ref new Windows::Foundation::TypedEventHandler<Windows::Media::SystemMediaTransportControls ^, Windows::Media::SystemMediaTransportControlsButtonPressedEventArgs ^>(this, &MediaPlayerCPP::MainPage::OnButtonPressed);
}

void MediaPlayerCPP::MainPage::OnButtonPressed(Windows::Media::SystemMediaTransportControls ^sender, Windows::Media::SystemMediaTransportControlsButtonPressedEventArgs ^args)
{
}

task<void> MainPage::TryOpenLastFile()
{
	try
	{
		//Try open last file
		auto file = co_await StorageApplicationPermissions::FutureAccessList->GetFileAsync(
			StorageApplicationPermissions::FutureAccessList->Entries->GetAt(0).Token);
		co_await OpenLocalFile(file);
	}
	catch (Exception^ ex)
	{
		DisplayErrorMessage(ex->Message);
	}
}

void MainPage::OpenLocalFile(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	OpenLocalFile();
}

task<void> MainPage::OpenLocalFile()
{
	try
	{
		FileOpenPicker^ filePicker = ref new FileOpenPicker();
		filePicker->ViewMode = PickerViewMode::Thumbnail;
		filePicker->SuggestedStartLocation = PickerLocationId::VideosLibrary;
		filePicker->FileTypeFilter->Append("*");

		// Show file picker so user can select a file
		auto file = co_await filePicker->PickSingleFileAsync();
		if (file)
		{
			co_await OpenLocalFile(file);
		}
	}
	catch (Exception^ ex)
	{
		DisplayErrorMessage(ex->Message);
	}
}

task<void> MainPage::OpenLocalFile(StorageFile^ file)
{
	currentFile = file;
	mediaElement->Stop();

	// Open StorageFile as IRandomAccessStream to be passed to FFmpegInteropMSS
	try
	{
		auto stream = co_await file->OpenAsync(FileAccessMode::Read);
		// Instantiate FFmpegInteropMSS using the opened local file stream
		FFmpegMSS = co_await FFmpegInteropMSS::CreateFromStreamAsync(stream, Config);

		StorageApplicationPermissions::FutureAccessList->Clear();
		StorageApplicationPermissions::FutureAccessList->Add(file);

		playbackItem = FFmpegMSS->CreateMediaPlaybackItem();


		// Pass MediaPlaybackItem to Media Element
		mediaElement->SetPlaybackSource(playbackItem);

		// Close control panel after file open
		Splitter->IsPaneOpen = false;

		auto controls = Windows::Media::SystemMediaTransportControls::GetForCurrentView();
		controls->IsPlayEnabled = true;
		controls->IsPauseEnabled = true;
		controls->PlaybackStatus = Windows::Media::MediaPlaybackStatus::Playing;
	}
	catch (Exception^ ex)
	{
		DisplayErrorMessage(ex->Message);
	}
}

void MainPage::URIBoxKeyUp(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e)
{
	String^ uri = safe_cast<TextBox^>(sender)->Text;

	// Only respond when the text box is not empty and after Enter key is pressed
	if (e->Key == Windows::System::VirtualKey::Enter && !uri->IsEmpty())
	{
		// Mark event as handled to prevent duplicate event to re-triggered
		e->Handled = true;

		OpenUriStream(uri);
	}
}

task<void> MainPage::OpenUriStream(Platform::String^ uri)
{
	// Set FFmpeg specific options. List of options can be found in https://www.ffmpeg.org/ffmpeg-protocols.html

	// Below are some sample options that you can set to configure RTSP streaming
	// Config->FFmpegOptions->Insert("rtsp_flags", "prefer_tcp");
	// Config->FFmpegOptions->Insert("stimeout", 100000);

	// Instantiate FFmpegInteropMSS using the URI
	mediaElement->Stop();
	try
	{
		FFmpegMSS = co_await FFmpegInteropMSS::CreateFromUriAsync(uri, Config);
		playbackItem = FFmpegMSS->CreateMediaPlaybackItem();

		// Pass MediaPlaybackItem to Media Element
		mediaElement->SetPlaybackSource(playbackItem);

		// Close control panel after opening media
		Splitter->IsPaneOpen = false;
	}
	catch (Exception^ ex)
	{
		DisplayErrorMessage(ex->Message);
	}
}

void MainPage::ExtractFrame(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	ExtractFrame();
}

task<void> MainPage::ExtractFrame()
{
	if (currentFile == nullptr)
	{
		DisplayErrorMessage("Please open a video file first.");
	}
	else
	{
		// open the file that is currently playing
		try
		{
			auto stream = co_await currentFile->OpenAsync(FileAccessMode::Read);
			bool exactSeek = grabFrameExactSeek->IsOn;
			// extract frame using FFmpegInterop and current position
			auto frameGrabber = co_await FrameGrabber::CreateFromStreamAsync(stream);
			auto frame = co_await frameGrabber->ExtractVideoFrameAsync(mediaElement->Position, exactSeek);
			auto filePicker = ref new FileSavePicker();
			filePicker->SuggestedStartLocation = PickerLocationId::VideosLibrary;
			filePicker->DefaultFileExtension = ".jpg";
			filePicker->FileTypeChoices->Insert("Jpeg file", ref new Platform::Collections::Vector<String^>(1, ".jpg"));

			// Show file picker so user can select a file
			auto file = co_await filePicker->PickSaveFileAsync();
			if (file != nullptr)
			{
				auto stream = co_await file->OpenAsync(FileAccessMode::ReadWrite);
				// encode frame as jpeg file
				co_await frame->EncodeAsJpegAsync(stream);
				stream = nullptr;

				// launch file after creation
				auto launched = co_await Windows::System::Launcher::LaunchFileAsync(file);
				if (!launched)
				{
					DisplayErrorMessage("File has been created:\n" + file->Path);
				}
			}
		}
		catch (Exception^ ex)
		{
			DisplayErrorMessage(ex->Message);
		}
	}
}

void MediaPlayerCPP::MainPage::LoadSubtitleFile(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	LoadSubtitleFile();
}

task<void> MediaPlayerCPP::MainPage::LoadSubtitleFile()
{
	if (playbackItem == nullptr)
	{
		DisplayErrorMessage("Please open a media file before loading an external subtitle for it.");
	}
	else
	{
		try
		{
			FileOpenPicker^ filePicker = ref new FileOpenPicker();
			filePicker->ViewMode = PickerViewMode::Thumbnail;
			filePicker->SuggestedStartLocation = PickerLocationId::VideosLibrary;
			filePicker->FileTypeFilter->Append("*");

			// Show file picker so user can select a file
			auto file = co_await filePicker->PickSingleFileAsync();
			if (file != nullptr)
			{
				// Open StorageFile as IRandomAccessStream to be passed to FFmpegInteropMSS
				auto stream = co_await file->OpenAsync(FileAccessMode::Read);
				auto track = TimedTextSource::CreateFromStream(stream);
				playbackItem->Source->ExternalTimedTextSources->Append(track);
				track->Resolved += ref new Windows::Foundation::TypedEventHandler<Windows::Media::Core::TimedTextSource^, Windows::Media::Core::TimedTextSourceResolveResultEventArgs^>(this, &MediaPlayerCPP::MainPage::OnResolved);
			}
		}
		catch (Exception^ e)
		{
			DisplayErrorMessage(e->Message);
		}
	}
}

void MediaPlayerCPP::MainPage::LoadSubtitleFileFFmpeg(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	LoadSubtitleFileFFmpeg();
}

task<void> MediaPlayerCPP::MainPage::LoadSubtitleFileFFmpeg()
{
	if (FFmpegMSS == nullptr)
	{
		DisplayErrorMessage("Please open a media file before loading an external subtitle for it.");
	}
	else
	{
		try
		{
			FileOpenPicker^ filePicker = ref new FileOpenPicker();
			filePicker->ViewMode = PickerViewMode::Thumbnail;
			filePicker->SuggestedStartLocation = PickerLocationId::VideosLibrary;
			filePicker->FileTypeFilter->Append("*");

			// Show file picker so user can select a file
			auto file = co_await filePicker->PickSingleFileAsync();
			if (file != nullptr)
			{
				// Open StorageFile as IRandomAccessStream to be passed to FFmpegInteropMSS
				auto stream = co_await file->OpenAsync(FileAccessMode::Read);
				if (playbackItem != nullptr)
				{
					timedMetadataTracksChangedToken = playbackItem->TimedMetadataTracksChanged += ref new Windows::Foundation::TypedEventHandler<Windows::Media::Playback::MediaPlaybackItem^, Windows::Foundation::Collections::IVectorChangedEventArgs^>(this, &MediaPlayerCPP::MainPage::OnTimedMetadataTracksChanged);
				}

				co_await this->FFmpegMSS->AddExternalSubtitleAsync(stream, file->Name);
			}
		}
		catch (Exception^ e)
		{
			DisplayErrorMessage(e->Message);
		}
	}
}

void MediaPlayerCPP::MainPage::OnTimedMetadataTracksChanged(MediaPlaybackItem ^sender, Windows::Foundation::Collections::IVectorChangedEventArgs ^args)
{
	if (args->CollectionChange == Windows::Foundation::Collections::CollectionChange::ItemInserted)
	{
		sender->TimedMetadataTracksChanged -= this->timedMetadataTracksChangedToken;

		// unselect other subs
		for (unsigned int i = 0; i < sender->TimedMetadataTracks->Size; i++)
		{
			sender->TimedMetadataTracks->SetPresentationMode(i, TimedMetadataTrackPresentationMode::Disabled);
		}

		// pre-select added subtitle
		sender->TimedMetadataTracks->SetPresentationMode(args->Index, TimedMetadataTrackPresentationMode::PlatformPresented);
	}
}

void MediaPlayerCPP::MainPage::OnResolved(TimedTextSource ^sender, TimedTextSourceResolveResultEventArgs ^args)
{
	// you can rename and pre-select the loaded subtitle track(s) if you like
	if (args->Tracks->Size > 0)
	{
		auto first = args->Tracks->GetAt(0);
		first->Label = "External";
		unsigned int index;
		if (playbackItem->TimedMetadataTracks->IndexOf(first, &index))
		{
			playbackItem->TimedMetadataTracks->SetPresentationMode(index, Windows::Media::Playback::TimedMetadataTrackPresentationMode::PlatformPresented);
		}
	}
}

void MainPage::MediaFailed(Platform::Object^ sender, Windows::UI::Xaml::ExceptionRoutedEventArgs^ e)
{
	DisplayErrorMessage(e->ErrorMessage);
	actualFFmpegMSS = nullptr;
}

void MainPage::DisplayErrorMessage(Platform::String^ message)
{
	Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this, message]()
		{
			// Display error message
			auto errorDialog = ref new MessageDialog(message);
			errorDialog->ShowAsync();
		}));
}

void MediaPlayerCPP::MainPage::CbEncodings_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	if (cbEncodings->SelectedItem)
	{
		Config->AnsiSubtitleEncoding = static_cast<CharacterEncoding^>(cbEncodings->SelectedItem);
	}
}

void MediaPlayerCPP::MainPage::PassthroughVideo_Toggled(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	Config->VideoDecoderMode = AutoDetect->IsOn ? VideoDecoderMode::Automatic : PassthroughVideo->IsOn ? VideoDecoderMode::ForceSystemDecoder : VideoDecoderMode::ForceFFmpegSoftwareDecoder;
}


void MediaPlayerCPP::MainPage::AddTestFilter(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	if (FFmpegMSS != nullptr)
	{
		Vector<AvEffectDefinition^>^ effects = ref new Vector<AvEffectDefinition^>();
		effects->Append(ref new AvEffectDefinition("aecho", "0.8:0.9:1000|1800:0.3|0.25"));
		FFmpegMSS->SetAudioEffects(effects->GetView());
	}
}


void MediaPlayerCPP::MainPage::RemoveTestFilter(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	if (FFmpegMSS != nullptr)
	{
		FFmpegMSS->DisableAudioEffects();
	}
}


void MediaPlayerCPP::MainPage::DelaySubtitles(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	if (FFmpegMSS != nullptr)
	{
		auto delay = FFmpegMSS->SubtitleDelay.Duration;
		TimeSpan newDelay;
		newDelay.Duration = delay + 10000000;
		FFmpegMSS->SetSubtitleDelay(newDelay);
		tbSubtitleDelay->Text = "Subtitle delay: " + (newDelay.Duration / 10000000).ToString() + "s";

	}
}


void MediaPlayerCPP::MainPage::QuickenSubtitles(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	if (FFmpegMSS != nullptr)
	{
		auto delay = FFmpegMSS->SubtitleDelay.Duration;
		TimeSpan newDelay;
		newDelay.Duration = delay - 10000000;
		FFmpegMSS->SetSubtitleDelay(newDelay);
		tbSubtitleDelay->Text = "Subtitle delay: " + (newDelay.Duration / 10000000).ToString() + "s";
	}
}


void MediaPlayerCPP::MainPage::MediaOpened(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	tbSubtitleDelay->Text = "Subtitle delay: 0s";
	actualFFmpegMSS = FFmpegMSS;
}


void MediaPlayerCPP::MainPage::AutoDetect_Toggled(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	PassthroughVideo->IsEnabled = !AutoDetect->IsOn;
	Config->VideoDecoderMode = AutoDetect->IsOn ? VideoDecoderMode::Automatic : PassthroughVideo->IsOn ? VideoDecoderMode::ForceSystemDecoder : VideoDecoderMode::ForceFFmpegSoftwareDecoder;
}

void MediaPlayerCPP::MainPage::EnableVideoEffects_Toggled(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	mediaElement->RemoveAllEffects();
	if (enableVideoEffects->IsOn)
	{
		VideoEffectConfiguration->AddVideoEffect(mediaElement);
	}
}

void MediaPlayerCPP::MainPage::OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args)
{
	if (args->VirtualKey == Windows::System::VirtualKey::Enter && (Window::Current->CoreWindow->GetKeyState(Windows::System::VirtualKey::Control) & Windows::UI::Core::CoreVirtualKeyStates::Down)
		== Windows::UI::Core::CoreVirtualKeyStates::Down && StorageApplicationPermissions::FutureAccessList->Entries->Size == 1)
	{
		TryOpenLastFile();
	}

	if (args->VirtualKey == Windows::System::VirtualKey::V)
	{
		if (playbackItem && playbackItem->VideoTracks->Size > 1)
		{
			playbackItem->VideoTracks->SelectedIndex =
				(playbackItem->VideoTracks->SelectedIndex + 1) % playbackItem->VideoTracks->Size;
		}
	}
}
