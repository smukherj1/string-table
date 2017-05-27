#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

// TBB includes some headers which defines "max" as a macro
// This makes std::numeric_limits<T>::max() unusable. This
// define hides the macro in this code
#define NOMINMAX
#include <tbb/reader_writer_lock.h>

namespace string_table
{
	namespace detail
	{
		using ID = uint32_t;
		constexpr ID CHUNK_SIZE = 4096;

		template
		<ID chunk_size>
		struct StringData
		{
			std::unique_ptr<char[]> data;
			ID pos = 0;

			StringData();
			bool has_space(ID size) const;
			char* write(const char* str, detail::ID size, detail::ID aligned_size);
		};

	} /* namespace detail */

	class Table;

	/**
	 * @brief Handle to a string in the string table. Castable to
	 *        bool which is false when default constructed or set
	 *        to the NULL string
	 */
	class StringView
	{
	public:
		StringView() : m_table(nullptr), m_id(0) { }

		const char* c_str() const;

		operator bool() const { return m_id != 0; }
		bool operator == (const StringView& other)
		{
			return m_table == other.m_table && m_id == other.m_id;
		}

	private:
		friend class Table;

		StringView(const Table* table, detail::ID id) : m_table(table), m_id(id) { }

		const Table* m_table;
		detail::ID m_id;
	};

	/**
	 * @brief Concurrent string table implementation
	 */
	class Table
	{
	public:
		Table();

		StringView find(const char* str) const;
		StringView find(const std::string& str) const;
		StringView get(const char* str);
		StringView get(const std::string& str);
	private:
		friend StringView;
		using StringData = detail::StringData<detail::CHUNK_SIZE>;

		struct CStringHasher
		{
			std::size_t operator()(const char* cstr) const;
		};

		struct CStringEqual
		{
			bool operator() (const char* lhs, const char* rhs) const;
		};

		StringView get(const char* str, std::size_t length);
		const char* get(detail::ID id) const;
		detail::ID alloc_chunk(detail::ID str_size);

		std::vector<StringData> m_chunks;
		std::vector<const char*> m_id_to_str;
		std::unordered_map<const char*, detail::ID, CStringHasher, CStringEqual> m_str_to_id_map;
		mutable tbb::reader_writer_lock m_rw_lock;
	};

	inline const char* StringView::c_str() const
	{
		return m_table != nullptr ? m_table->get(m_id) : nullptr;
	}

} /* namespace string_table */