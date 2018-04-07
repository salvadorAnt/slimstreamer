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

#include <cstddef>  // std::size_t
#include <functional>
#include <string>

#include "slim/util/Writer.hpp"


namespace slim
{
	namespace util
	{
		template<std::size_t TotalElements>
		class BufferedWriter : public Writer
		{
			public:
				virtual ~BufferedWriter() = default;

				virtual void rewind(const std::streampos pos) = 0;

				virtual void write(std::string str)
				{
					write(str.c_str(), str.length());
				}

				virtual std::size_t write(const void* data, const std::size_t size) = 0;

				virtual void writeAsync(std::string str, WriteCallback callback = [](auto&, auto) {})
				{
					writeAsync(str.c_str(), str.length(), callback);
				}

				virtual void writeAsync(const void* data, const std::size_t size, WriteCallback callback = [](auto&, auto) {}) override
				{
					//sdsd;
				}

			private:
				std::array<util::ExpandableBuffer, TotalElements> buffers;
		};
	}
}
