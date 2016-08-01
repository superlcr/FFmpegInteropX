//*****************************************************************************
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

#include "pch.h"
#include "UncompressedVideoSampleProvider.h"
#include <mfapi.h>

extern "C"
{
#include <libavutil/imgutils.h>
}


using namespace FFmpegInterop;

UncompressedVideoSampleProvider::UncompressedVideoSampleProvider(
	FFmpegReader^ reader,
	AVFormatContext* avFormatCtx,
	AVCodecContext* avCodecCtx)
	: MediaSampleProvider(reader, avFormatCtx, avCodecCtx)
	, m_pAvFrame(nullptr)
	, m_pSwsCtx(nullptr)
{
	for (int i = 0; i < 4; i++)
	{
		m_rgVideoBufferLineSize[i] = 0;
		m_rgVideoBufferData[i] = nullptr;
	}
}

HRESULT UncompressedVideoSampleProvider::AllocateResources()
{
	HRESULT hr = S_OK;
	hr = MediaSampleProvider::AllocateResources();
	if (SUCCEEDED(hr))
	{
		// Setup software scaler to convert any decoder pixel format (e.g. YUV420P) to NV12 that is supported in Windows & Windows Phone MediaElement
		m_pSwsCtx = sws_getContext(
			m_pAvCodecCtx->width,
			m_pAvCodecCtx->height,
			m_pAvCodecCtx->pix_fmt,
			m_pAvCodecCtx->width,
			m_pAvCodecCtx->height,
			AV_PIX_FMT_NV12,
			SWS_BICUBIC,
			NULL,
			NULL,
			NULL);

		if (m_pSwsCtx == nullptr)
		{
			hr = E_OUTOFMEMORY;
		}
	}

	if (SUCCEEDED(hr))
	{
		m_pAvFrame = av_frame_alloc();
		if (m_pAvFrame == nullptr)
		{
			hr = E_OUTOFMEMORY;
		}
	}

	if (SUCCEEDED(hr))
	{
		if (av_image_alloc(m_rgVideoBufferData, m_rgVideoBufferLineSize, m_pAvCodecCtx->width, m_pAvCodecCtx->height, AV_PIX_FMT_NV12, 1) < 0)
		{
			hr = E_FAIL;
		}
	}

	return hr;
}

UncompressedVideoSampleProvider::~UncompressedVideoSampleProvider()
{
	if (m_pAvFrame)
	{
		av_freep(m_pAvFrame);
	}

	if (m_rgVideoBufferData)
	{
		av_freep(m_rgVideoBufferData);
	}
}

MediaStreamSample^ UncompressedVideoSampleProvider::GetNextSample()
{
	MediaStreamSample^ sample = MediaSampleProvider::GetNextSample();

	if (sample != nullptr)
	{
		if (m_interlaced_frame)
		{
			sample->ExtendedProperties->Insert(MFSampleExtension_Interlaced, TRUE);
			sample->ExtendedProperties->Insert(MFSampleExtension_BottomFieldFirst, m_top_field_first ? FALSE : TRUE);
			sample->ExtendedProperties->Insert(MFSampleExtension_RepeatFirstField, FALSE);
		}
		else
		{
			sample->ExtendedProperties->Insert(MFSampleExtension_Interlaced, FALSE);
		}
	}

	return sample;
}

HRESULT UncompressedVideoSampleProvider::WriteAVPacketToStream(DataWriter^ dataWriter, AVPacket* avPacket)
{
	// Convert decoded video pixel format to NV12 using FFmpeg software scaler
	if (sws_scale(m_pSwsCtx, (const uint8_t **)(m_pAvFrame->data), m_pAvFrame->linesize, 0, m_pAvCodecCtx->height, m_rgVideoBufferData, m_rgVideoBufferLineSize) < 0)
	{
		return E_FAIL;
	}

	auto YBuffer = ref new Platform::Array<uint8_t>(m_rgVideoBufferData[0], m_rgVideoBufferLineSize[0] * m_pAvCodecCtx->height);
	auto UVBuffer = ref new Platform::Array<uint8_t>(m_rgVideoBufferData[1], m_rgVideoBufferLineSize[1] * m_pAvCodecCtx->height / 2);
	dataWriter->WriteBytes(YBuffer);
	dataWriter->WriteBytes(UVBuffer);
	av_frame_unref(m_pAvFrame);

	return S_OK;
}

HRESULT UncompressedVideoSampleProvider::DecodeAVPacket(DataWriter^ dataWriter, AVPacket* avPacket)
{
	int frameComplete = 0;
	if (avcodec_decode_video2(m_pAvCodecCtx, m_pAvFrame, &frameComplete, avPacket) < 0)
	{
		DebugMessage(L"DecodeAVPacket Failed\n");
		frameComplete = 1;
	}
	else
	{
		if (frameComplete)
		{
			avPacket->pts = av_frame_get_best_effort_timestamp(m_pAvFrame);
			m_interlaced_frame = m_pAvFrame->interlaced_frame == 1;
			m_top_field_first = m_pAvFrame->top_field_first == 1;
		}
	}

	return frameComplete ? S_OK : S_FALSE;
}
