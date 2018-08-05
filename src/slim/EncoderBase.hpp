/*
 * Copyright 2017, Andrej Kislovskij
 *
 * This is PUBLIC DOMAIN software so use at your own risk as it comes
 * with no warranties. This code is yours to share, use and modify without
 * any restrictions or obligations.
 *
 * For more information see conwrap/LICENSE or refer refer to http://unlicense.org
 *
 * Author: gimesketvirtadieni at gmail dot com (Andrej Kislovskij)
 */

#pragma once

#include <cstddef>   // std::size_t
#include <functional>
#include <memory>
#include <string>


namespace slim
{
	class EncoderBase
	{
		protected:
			using EncodedCallbackType = std::function<void(unsigned char*, std::size_t)>;

		public:
			EncoderBase(unsigned int ch, unsigned int bs, unsigned int bv, unsigned int sr, std::string ex, std::string mm, EncodedCallbackType ec)
			: channels{ch}
			, bitsPerSample{bs}
			, bitsPerValue{bv}
			, samplingRate{sr}
			, extention{ex}
			, mime{mm}
			, encodedCallback{ec}{}

			virtual ~EncoderBase() = default;
			EncoderBase(const EncoderBase&) = delete;             // non-copyable
			EncoderBase& operator=(const EncoderBase&) = delete;  // non-assignable
			EncoderBase(EncoderBase&&) = delete;                  // non-movable
			EncoderBase& operator=(EncoderBase&&) = delete;       // non-assign-movable

			virtual void encode(unsigned char* data, const std::size_t size) = 0;

			inline auto getBitsPerSample()
			{
				return bitsPerSample;
			}

			inline auto getBitsPerValue()
			{
				return bitsPerValue;
			}

			inline auto getChannels()
			{
				return channels;
			}

			inline auto& getEncodedCallback()
			{
				return encodedCallback;
			}

			inline auto getExtention()
			{
				return extention;
			}

			inline auto getMIME()
			{
				return mime;
			}

			virtual unsigned long getSamplesEncoded() = 0;

			inline auto getSamplingRate()
			{
				return samplingRate;
			}

		private:
			unsigned int        channels;
			unsigned int        bitsPerSample;
			unsigned int        bitsPerValue;
			unsigned int        samplingRate;
			std::string         extention;
			std::string         mime;
			EncodedCallbackType encodedCallback;
	};
}
