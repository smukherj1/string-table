#include "StringTable.h"
#include <cstring>
#include <numeric>
#include <iostream>
#include <sstream>
#include <chrono>

#include "tbb/tbbmalloc_proxy.h"

namespace string_table
{
	template<detail::ID chunk_size>
	detail::StringData<chunk_size>::StringData() :
		data(new char[chunk_size])
	{
		std::memset(data.get(), 0, chunk_size);
	}

	template<detail::ID chunk_size>
	char* detail::StringData<chunk_size>::write(const char* str, detail::ID size, detail::ID aligned_size)
	{
		auto start = data.get() + pos;
		std::memcpy(start, str, size);
		pos += aligned_size;
		return start;
	}

	template<detail::ID chunk_size>
	bool detail::StringData<chunk_size>::has_space(ID size) const
	{
		return (chunk_size - sizeof(ID) - pos) >= size;
	}

	std::size_t Table::CStringHasher::operator() (const char* cstr) const
	{
		std::size_t result = 7;
		for (std::size_t i = 0; cstr[i] != '\0'; ++i)
		{
			result = (result * 31) + cstr[i];
		}
		return result;
	}

	bool Table::CStringEqual::operator() (const char* lhs, const char* rhs) const
	{
		return std::strcmp(lhs, rhs) == 0;
	}

	Table::Table()
	{
		// Always insert nullstring first
		// so that it gets the id 0
		get("");
	}

	StringView Table::find(const char* str) const
	{
		tbb::reader_writer_lock::scoped_lock_read scoped_read_lock(m_rw_lock);
		auto it = m_str_to_id_map.find(str);
		if (it == m_str_to_id_map.end())
		{
			return StringView();
		}

		return StringView(this, it->second);
	}

	StringView Table::find(const std::string& str) const
	{
		return find(str.c_str());
	}

	StringView Table::get(const std::string& str)
	{
		return get(str.c_str(), str.size());
	}

	StringView Table::get(const char* str)
	{
		return get(str, std::strlen(str));
	}

	StringView Table::get(const char* str, size_t str_size)
	{
		auto sv = find(str);
		if (sv)
		{
			return sv;
		}

		// New string. Add it to table
		auto mod = (str_size + 1) & (sizeof(detail::ID) - 1);
		auto aligned_size = str_size + 1;
		if (mod != 0)
		{
			aligned_size += sizeof(detail::ID) - mod;
		}

		tbb::reader_writer_lock::scoped_lock scoped_lock(m_rw_lock);

		if (
			// Size of the string can't be larger than the chunk size
			aligned_size > detail::CHUNK_SIZE
			// Size of the string can't be larger than the limit of our
			// internal ID
			|| aligned_size > std::numeric_limits<detail::ID>::max()
			// Number of unique strings has reached ID limit
			|| m_id_to_str.size() == std::numeric_limits<detail::ID>::max()
			)
		{
			return StringView();
		}

		auto cast_str_size = static_cast<detail::ID>(str_size);
		auto cast_aligned_size = static_cast<detail::ID>(aligned_size);
		auto chunk_id = alloc_chunk(cast_aligned_size);

		auto& chunk = m_chunks[chunk_id];
		if (!chunk.has_space(cast_aligned_size))
		{
			return StringView();
		}

		auto new_str = chunk.write(str, cast_str_size, cast_aligned_size);
		auto new_id = static_cast<detail::ID>(m_id_to_str.size());
		m_id_to_str.emplace_back(new_str);
		m_str_to_id_map.emplace(new_str, new_id);

		return StringView(this, new_id);
	}

	const char* Table::get(detail::ID id) const
	{
		tbb::reader_writer_lock::scoped_lock_read scoped_read_lock(m_rw_lock);
		if (id < m_id_to_str.size())
		{
			return m_id_to_str[id];
		}
		return nullptr;
	}

	detail::ID Table::alloc_chunk(detail::ID str_size)
	{
		if (m_chunks.empty() || !m_chunks.back().has_space(str_size))
		{
			// Need to allocate new chunk
			auto next_chunk_id = m_chunks.size();

			if (next_chunk_id > std::numeric_limits<detail::ID>::max())
			{
				// Reached max number of chunks. Further allocation not possible
				return static_cast<detail::ID>(next_chunk_id - 1);
			}

			auto cast_next_chunk_id = static_cast<detail::ID>(next_chunk_id);
			m_chunks.emplace_back();
			return cast_next_chunk_id;
		}

		// The last allocated chunk has capacity for this request
		return static_cast<detail::ID>(m_chunks.size() - 1);
	}
}

int main()
{
	string_table::Table stable;
	auto start = std::chrono::steady_clock::now();

	for (std::size_t i = 0; i < 1000000; ++i)
	{
		std::stringstream ss;
		ss << "foo" << i;
		stable.get(ss.str());
	}

	auto end = std::chrono::steady_clock::now();
	auto diff_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	std::cout << "Processing took " << diff_time / 1000.0f
		<< "s\n";

	return 0;
}