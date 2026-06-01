/* SPDX-License-Identifier: MIT */

/* rsx_lib_vulkan.cpp - libretro Vulkan GPU backend for Beetle PSX HW,
 * with the entire parallel-psx Vulkan + FBAtlas + HD texture-tracker
 * stack folded in. Previously the renderer lived under
 * parallel-psx/renderer/renderer.{hpp,cpp}; that directory is gone
 * and its contents (header content first, then implementation) sit
 * directly below the libretro plumbing's preamble. */

#include "rsx/rsx_lib_vulkan.h"

#include <stdint.h>

#include <vector>

#include "rsx/rsx_intf.h" //FPS and audio sample rate macros
#include "rsx/rsx_defer.h"
#include "rsx/tt_trace.h"
/* === folded parallel-psx/renderer/renderer.hpp === */
/* Copyright (c) 2017-2018 Hans-Kristian Arntzen, parallel-psx contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * SPDX-License-Identifier: MIT */

/* renderer.hpp - consolidated parallel-psx interface.
 *
 * Previously the implementation lived under parallel-psx/util/,
 * parallel-psx/vulkan/, parallel-psx/atlas/, and
 * parallel-psx/custom-textures/ across many separate header files.
 * They have all been folded here in topological dependency order;
 * the matching .cpp bodies live in renderer.cpp and the
 * custom-textures/ .cpp files. */

#pragma once

#include <volk.h>
#include "libretro.h"

#include <algorithm>
#include <assert.h>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <rthreads/rthreads.h>


/* ============================================================
 * util.hpp
 * ============================================================ */



namespace Granite
{
extern retro_log_printf_t libretro_log;
}
#define LOGE(...) do { if (::Granite::libretro_log) ::Granite::libretro_log(RETRO_LOG_ERROR, __VA_ARGS__); } while(0)
#define LOGI(...) do { if (::Granite::libretro_log) ::Granite::libretro_log(RETRO_LOG_INFO, __VA_ARGS__); } while(0)

#ifdef _MSC_VER
#endif

#ifdef __GNUC__
#define leading_zeroes(x) ((x) == 0 ? 32 : __builtin_clz(x))
#define trailing_zeroes(x) ((x) == 0 ? 32 : __builtin_ctz(x))
#define trailing_ones(x) __builtin_ctz(~(x))
#elif defined(_MSC_VER)
static inline uint32_t util_clz(uint32_t x)
{
	unsigned long result;
	if (_BitScanReverse(&result, x))
		return 31 - result;
	return 32;
}

static inline uint32_t util_ctz(uint32_t x)
{
	unsigned long result;
	if (_BitScanForward(&result, x))
		return result;
	return 32;
}

#define leading_zeroes(x) util_clz(x)
#define trailing_zeroes(x) util_ctz(x)
#define trailing_ones(x) util_ctz(~(x))
#else
#error "Implement me."
#endif

/* Iterate over each set bit in a uint32_t mask. Inside the body, BIT_VAR holds
 * the bit index. C-style: just expand into a for loop, no captures or lambdas. */
#define FOR_EACH_BIT(value, bit_var)                                          \
	for (uint32_t _fe_v = (uint32_t)(value), bit_var = trailing_zeroes(_fe_v); \
	     _fe_v;                                                                \
	     _fe_v &= _fe_v - 1u, bit_var = trailing_zeroes(_fe_v))

/* Iterate over each contiguous run of 1-bits in a uint32_t mask. BASE_VAR is
 * the bit index of the run's first 1, RANGE_VAR is the run length. */
#define FOR_EACH_BIT_RANGE(value, base_var, range_var)                            \
	for (uint32_t _fe_v = (uint32_t)(value),                                       \
	              base_var = trailing_zeroes(_fe_v),                               \
	              range_var = (_fe_v ? trailing_ones(_fe_v >> base_var) : 0u);     \
	     _fe_v;                                                                    \
	     _fe_v &= ~((1u << (base_var + range_var)) - 1u),                          \
	     base_var = trailing_zeroes(_fe_v),                                        \
	     range_var = (_fe_v ? trailing_ones(_fe_v >> base_var) : 0u))

/* ============================================================
 * hash.hpp
 * ============================================================ */


namespace Util
{
using Hash = uint64_t;

class Hasher
{
public:
	Hasher(Hash h)
		: h(h)
	{
	}

	Hasher() = default;

	inline void data(const uint32_t *p, size_t bytes)
	{
		size_t count = bytes / sizeof(uint32_t);
		for (size_t i = 0; i < count; i++)
			h = (h * 0x100000001b3ull) ^ p[i];
	}

	inline void u32(uint32_t value)
	{
		h = (h * 0x100000001b3ull) ^ value;
	}

	inline void u64(uint64_t value)
	{
		u32(value & 0xffffffffu);
		u32(value >> 32);
	}

	inline Hash get() const
	{
		return h;
	}

private:
	Hash h = 0xcbf29ce484222325ull;
};
}

/* ============================================================
 * intrusive.hpp
 * ============================================================ */



namespace Util
{
class SingleThreadCounter
{
public:
	inline void add_ref()
	{
		count++;
	}

	inline bool release()
	{
		return --count == 0;
	}

private:
	size_t count = 1;
};

template <typename T>
class IntrusivePtr;

template <typename T, typename Deleter = std::default_delete<T>, typename ReferenceOps = SingleThreadCounter>
class IntrusivePtrEnabled
{
public:
	using IntrusivePtrType = IntrusivePtr<T>;
	using EnabledBase = T;
	using EnabledDeleter = Deleter;
	using EnabledReferenceOp = ReferenceOps;

	void release_reference()
	{
		if (reference_count.release())
			Deleter()(static_cast<T *>(this));
	}

	void add_reference()
	{
		reference_count.add_ref();
	}

	IntrusivePtrEnabled() = default;

	IntrusivePtrEnabled(const IntrusivePtrEnabled &) = delete;

	void operator=(const IntrusivePtrEnabled &) = delete;

private:
	ReferenceOps reference_count;
};

template <typename T>
class IntrusivePtr
{
public:
	template <typename U>
	friend class IntrusivePtr;

	IntrusivePtr() = default;

	explicit IntrusivePtr(T *handle)
		: data(handle)
	{
	}

	T &operator*()
	{
		return *data;
	}

	const T &operator*() const
	{
		return *data;
	}

	T *operator->()
	{
		return data;
	}

	const T *operator->() const
	{
		return data;
	}

	explicit operator bool() const
	{
		return data != nullptr;
	}

	bool operator==(const IntrusivePtr &other) const
	{
		return data == other.data;
	}

	bool operator!=(const IntrusivePtr &other) const
	{
		return data != other.data;
	}

	T *get()
	{
		return data;
	}

	const T *get() const
	{
		return data;
	}

	void reset()
	{
		using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

		// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
		// Also makes sure that the pointer type actually inherits from this type.
		if (data)
			static_cast<ReferenceBase *>(data)->release_reference();
		data = nullptr;
	}

	template <typename U>
	IntrusivePtr &operator=(const IntrusivePtr<U> &other)
	{
		static_assert(std::is_base_of<T, U>::value,
		              "Cannot safely assign downcasted intrusive pointers.");

		using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

		reset();
		data = static_cast<T *>(other.data);

		// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
		// Also makes sure that the pointer type actually inherits from this type.
		if (data)
			static_cast<ReferenceBase *>(data)->add_reference();
		return *this;
	}

	IntrusivePtr &operator=(const IntrusivePtr &other)
	{
		using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

		if (this != &other)
		{
			reset();
			data = other.data;
			if (data)
				static_cast<ReferenceBase *>(data)->add_reference();
		}
		return *this;
	}

	template <typename U>
	IntrusivePtr(const IntrusivePtr<U> &other)
	{
		*this = other;
	}

	IntrusivePtr(const IntrusivePtr &other)
	{
		*this = other;
	}

	~IntrusivePtr()
	{
		reset();
	}

	template <typename U>
	IntrusivePtr &operator=(IntrusivePtr<U> &&other) noexcept
	{
		reset();
		data = other.data;
		other.data = nullptr;
		return *this;
	}

	IntrusivePtr &operator=(IntrusivePtr &&other) noexcept
	{
		if (this != &other)
		{
			reset();
			data = other.data;
			other.data = nullptr;
		}
		return *this;
	}

	template <typename U>
	IntrusivePtr(IntrusivePtr<U> &&other) noexcept
	{
		*this = std::move(other);
	}

	template <typename U>
	IntrusivePtr(IntrusivePtr &&other) noexcept
	{
		*this = std::move(other);
	}

private:
	T *data = nullptr;
};

}

/* ============================================================
 * intrusive_list.hpp
 * ============================================================ */


namespace Util
{
template <typename T>
struct IntrusiveListEnabled
{
	IntrusiveListEnabled<T> *prev = nullptr;
	IntrusiveListEnabled<T> *next = nullptr;
};

template <typename T>
class IntrusiveList
{
public:
	void clear()
	{
		head = nullptr;
	}

	class Iterator
	{
	public:
		friend class IntrusiveList<T>;
		Iterator(IntrusiveListEnabled<T> *node)
		    : node(node)
		{
		}

		Iterator() = default;

		explicit operator bool() const
		{
			return node != nullptr;
		}

		bool operator==(const Iterator &other) const
		{
			return node == other.node;
		}

		bool operator!=(const Iterator &other) const
		{
			return node != other.node;
		}

		T &operator*()
		{
			return *static_cast<T *>(node);
		}

		const T &operator*() const
		{
			return *static_cast<T *>(node);
		}

		T *get()
		{
			return static_cast<T *>(node);
		}

		const T *get() const
		{
			return static_cast<const T *>(node);
		}

		T *operator->()
		{
			return static_cast<T *>(node);
		}

		const T *operator->() const
		{
			return static_cast<T *>(node);
		}

		Iterator &operator++()
		{
			node = node->next;
			return *this;
		}

	private:
		IntrusiveListEnabled<T> *node = nullptr;
	};

	Iterator begin()
	{
		return Iterator(head);
	}

	Iterator end()
	{
		return Iterator();
	}

	Iterator erase(Iterator itr)
	{
		T *node = itr.get();
		IntrusiveListEnabled<T> *next = node->next;
		IntrusiveListEnabled<T> *prev = node->prev;

		if (prev)
			prev->next = next;
		else
			head = next;

		if (next)
			next->prev = prev;

		return next;
	}

	void insert_front(Iterator itr)
	{
		T *node = itr.get();
		if (head)
			head->prev = node;

		node->next = head;
		node->prev = nullptr;
		head = node;
	}

	void move_to_front(IntrusiveList<T> &other, Iterator itr)
	{
		other.erase(itr);
		insert_front(itr);
	}

	bool empty() const
	{
		return head == nullptr;
	}

private:
	IntrusiveListEnabled<T> *head = nullptr;
};
}

/* ============================================================
 * object_pool.hpp
 * ============================================================ */



namespace Util
{
template<typename T>
class ObjectPool
{
public:
	template<typename... P>
	T *allocate(P &&... p)
	{
		if (vacants.empty())
		{
			unsigned num_objects = 64u << memory.size();
			T *ptr = static_cast<T *>(malloc(num_objects * sizeof(T)));
			if (!ptr)
				return nullptr;

			for (unsigned i = 0; i < num_objects; i++)
				vacants.push_back(&ptr[i]);

			memory.emplace_back(ptr);
		}

		T *ptr = vacants.back();
		vacants.pop_back();
		new(ptr) T(std::forward<P>(p)...);
		return ptr;
	}

	void free(T *ptr)
	{
		ptr->~T();
		vacants.push_back(ptr);
	}

	void clear()
	{
		vacants.clear();
		memory.clear();
	}

protected:
	std::vector<T *> vacants;

	struct MallocDeleter
	{
		void operator()(T *ptr)
		{
			::free(ptr);
		}
	};

	std::vector<std::unique_ptr<T, MallocDeleter>> memory;
};

}

/* ============================================================
 * intrusive_hash_map.hpp
 * ============================================================ */



namespace Util
{
template <typename T>
class IntrusiveHashMapEnabled : public IntrusiveListEnabled<T>
{
public:
	IntrusiveHashMapEnabled() = default;
	IntrusiveHashMapEnabled(Util::Hash hash)
		: intrusive_hashmap_key(hash)
	{
	}

	void set_hash(Util::Hash hash)
	{
		intrusive_hashmap_key = hash;
	}

	Util::Hash get_hash() const
	{
		return intrusive_hashmap_key;
	}

private:
	Hash intrusive_hashmap_key = 0;
};

template <typename T>
struct IntrusivePODWrapper : public IntrusiveHashMapEnabled<IntrusivePODWrapper<T>>
{
	template <typename U>
	explicit IntrusivePODWrapper(U&& value_)
		: value(std::forward<U>(value_))
	{
	}

	IntrusivePODWrapper() = default;

	T& get()
	{
		return value;
	}

	const T& get() const
	{
		return value;
	}

	T value = {};
};

// This HashMap is non-owning. It just arranges a list of pointers.
// It's kind of special purpose container used by the Vulkan backend.
// Dealing with memory ownership is done through composition by a different class.
// T must inherit from IntrusiveHashMapEnabled<T>.
// Each instance of T can only be part of one hashmap.

template <typename T>
class IntrusiveHashMapHolder
{
public:
	enum { InitialSize = 16, InitialLoadCount = 3 };

	T *find(Hash hash) const
	{
		if (values.empty())
			return nullptr;

		Hash hash_mask = values.size() - 1;
		Hash masked = hash & hash_mask;
		for (unsigned i = 0; i < load_count; i++)
		{
			if (values[masked] && get_hash(values[masked]) == hash)
				return values[masked];
			masked = (masked + 1) & hash_mask;
		}

		return nullptr;
	}

	// Inserts, if value already exists, insertion does not happen.
	// Return value is the data which is not part of the hashmap.
	// It should be deleted or similar.
	// Returns nullptr if nothing was in the hashmap for this key.
	T *insert_yield(T *&value)
	{
		if (values.empty())
			grow();

		Hash hash_mask = values.size() - 1;
		Hash hash = get_hash(value);
		Hash masked = hash & hash_mask;

		for (unsigned i = 0; i < load_count; i++)
		{
			if (values[masked] && get_hash(values[masked]) == hash)
			{
				T *ret = value;
				value = values[masked];
				return ret;
			}
			else if (!values[masked])
			{
				values[masked] = value;
				list.insert_front(value);
				return nullptr;
			}
			masked = (masked + 1) & hash_mask;
		}

		grow();
		return insert_yield(value);
	}

	T *insert_replace(T *value)
	{
		if (values.empty())
			grow();

		Hash hash_mask = values.size() - 1;
		Hash hash = get_hash(value);
		Hash masked = hash & hash_mask;

		for (unsigned i = 0; i < load_count; i++)
		{
			if (values[masked] && get_hash(values[masked]) == hash)
			{
				T *tmp = values[masked];
				values[masked] = value;
				value = tmp;
				list.erase(value);
				list.insert_front(values[masked]);
				return value;
			}
			else if (!values[masked])
			{
				assert(!values[masked]);
				values[masked] = value;
				list.insert_front(value);
				return nullptr;
			}
			masked = (masked + 1) & hash_mask;
		}

		grow();
		return insert_replace(value);
	}

	T *erase(Hash hash)
	{
		Hash hash_mask = values.size() - 1;
		Hash masked = hash & hash_mask;

		for (unsigned i = 0; i < load_count; i++)
		{
			if (values[masked] && get_hash(values[masked]) == hash)
			{
				T *value = values[masked];
				list.erase(value);
				values[masked] = nullptr;
				return value;
			}
			masked = (masked + 1) & hash_mask;
		}
		return nullptr;
	}

	void clear()
	{
		list.clear();
		values.clear();
		load_count = 0;
	}

	typename IntrusiveList<T>::Iterator begin()
	{
		return list.begin();
	}

	typename IntrusiveList<T>::Iterator end()
	{
		return list.end();
	}

	IntrusiveList<T> &inner_list()
	{
		return list;
	}

private:

	inline Hash get_hash(const T *value) const
	{
		return static_cast<const IntrusiveHashMapEnabled<T> *>(value)->get_hash();
	}

	bool insert_inner(T *value)
	{
		Hash hash_mask = values.size() - 1;
		Hash hash = get_hash(value);
		Hash masked = hash & hash_mask;

		for (unsigned i = 0; i < load_count; i++)
		{
			if (!values[masked])
			{
				values[masked] = value;
				return true;
			}
			masked = (masked + 1) & hash_mask;
		}
		return false;
	}

	void grow()
	{
		bool success;
		do
		{
			for (T *&v : values)
				v = nullptr;

			if (values.empty())
			{
				values.resize(InitialSize);
				load_count = InitialLoadCount;
				//LOGI("Growing hashmap to %u elements.\n", InitialSize);
			}
			else
			{
				values.resize(values.size() * 2);
				//LOGI("Growing hashmap to %u elements.\n", unsigned(values.size()));
				load_count++;
			}

			// Re-insert.
			success = true;
			for (T &t : list)
			{
				if (!insert_inner(&t))
				{
					success = false;
					break;
				}
			}
		} while (!success);
	}

	std::vector<T *> values;
	IntrusiveList<T> list;
	unsigned load_count = 0;
};

template <typename T>
class IntrusiveHashMap
{
public:
	~IntrusiveHashMap()
	{
		clear();
	}

	IntrusiveHashMap() = default;
	IntrusiveHashMap(const IntrusiveHashMap &) = delete;
	void operator=(const IntrusiveHashMap &) = delete;

	void clear()
	{
		IntrusiveList<T> &list = hashmap.inner_list();
		typename IntrusiveList<T>::Iterator itr = list.begin();
		while (itr != list.end())
		{
			T *to_free = itr.get();
			itr = list.erase(itr);
			pool.free(to_free);
		}

		hashmap.clear();
	}

	T *find(Hash hash) const
	{
		return hashmap.find(hash);
	}

	void erase(Hash hash)
	{
		T *value = hashmap.erase(hash);
		if (value)
			pool.free(value);
	}

	template <typename... P>
	T *emplace_replace(Hash hash, P&&... p)
	{
		T *t = allocate(std::forward<P>(p)...);
		return insert_replace(hash, t);
	}

	template <typename... P>
	T *emplace_yield(Hash hash, P&&... p)
	{
		T *t = allocate(std::forward<P>(p)...);
		return insert_yield(hash, t);
	}

	template <typename... P>
	T *allocate(P&&... p)
	{
		return pool.allocate(std::forward<P>(p)...);
	}

	T *insert_replace(Hash hash, T *value)
	{
		static_cast<IntrusiveHashMapEnabled<T> *>(value)->set_hash(hash);
		T *to_delete = hashmap.insert_replace(value);
		if (to_delete)
			pool.free(to_delete);
		return value;
	}

	T *insert_yield(Hash hash, T *value)
	{
		static_cast<IntrusiveHashMapEnabled<T> *>(value)->set_hash(hash);
		T *to_delete = hashmap.insert_yield(value);
		if (to_delete)
			pool.free(to_delete);
		return value;
	}

	typename IntrusiveList<T>::Iterator begin()
	{
		return hashmap.begin();
	}

	typename IntrusiveList<T>::Iterator end()
	{
		return hashmap.end();
	}

private:
	IntrusiveHashMapHolder<T> hashmap;
	ObjectPool<T> pool;
};

}

/* ============================================================
 * temporary_hashmap.hpp
 * ============================================================ */



namespace Util
{
template <typename T>
class TemporaryHashmapEnabled
{
public:
	void set_hash(Hash hash)
	{
		this->hash = hash;
	}

	void set_index(unsigned index)
	{
		this->index = index;
	}

	Hash get_hash()
	{
		return hash;
	}

	unsigned get_index() const
	{
		return index;
	}

private:
	Hash hash = 0;
	unsigned index = 0;
};

template <typename T, unsigned RingSize = 4, bool ReuseObjects = false>
class TemporaryHashmap
{
public:
	~TemporaryHashmap()
	{
		clear();
	}

	void clear()
	{
		for (IntrusiveList<T> &ring : rings)
		{
			for (T &node : ring)
				object_pool.free(static_cast<T *>(&node));
			ring.clear();
		}
		hashmap.clear();

		for (typename IntrusiveList<T>::Iterator &vacant : vacants)
			object_pool.free(static_cast<T *>(&*vacant));
		vacants.clear();
		object_pool.clear();
	}

	void begin_frame()
	{
		index = (index + 1) & (RingSize - 1);
		for (T &node : rings[index])
		{
			hashmap.erase(node.get_hash());
			free_object(&node, ReuseTag<ReuseObjects>());
		}
		rings[index].clear();
	}

	T *request(Hash hash)
	{
		IntrusivePODWrapper<typename IntrusiveList<T>::Iterator> *v = hashmap.find(hash);
		if (v)
		{
			typename IntrusiveList<T>::Iterator node = v->get();
			if (node->get_index() != index)
			{
				rings[index].move_to_front(rings[node->get_index()], node);
				node->set_index(index);
			}

			return &*node;
		}
		else
			return nullptr;
	}

	template <typename... P>
	void make_vacant(P &&... p)
	{
		vacants.push_back(object_pool.allocate(std::forward<P>(p)...));
	}

	T *request_vacant(Hash hash)
	{
		if (vacants.empty())
			return nullptr;

		typename IntrusiveList<T>::Iterator top = vacants.back();
		vacants.pop_back();
		top->set_index(index);
		top->set_hash(hash);
		hashmap.emplace_replace(hash, top);
		rings[index].insert_front(top);
		return &*top;
	}

	template <typename... P>
	T *emplace(Hash hash, P &&... p)
	{
		T *node = object_pool.allocate(std::forward<P>(p)...);
		node->set_index(index);
		node->set_hash(hash);
		hashmap.emplace_replace(hash, node);
		rings[index].insert_front(node);
		return node;
	}

private:
	IntrusiveList<T> rings[RingSize];
	ObjectPool<T> object_pool;
	unsigned index = 0;
	IntrusiveHashMap<IntrusivePODWrapper<typename IntrusiveList<T>::Iterator>> hashmap;
	std::vector<typename IntrusiveList<T>::Iterator> vacants;

	template <bool reuse>
	struct ReuseTag
	{
	};

	void free_object(T *object, const ReuseTag<false> &)
	{
		object_pool.free(object);
	}

	void free_object(T *object, const ReuseTag<true> &)
	{
		vacants.push_back(object);
	}
};
}

/* ============================================================
 * vulkan.hpp
 * ============================================================ */



#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
// Workaround silly Xlib headers that define macros for these globally :(
#undef None
#undef Bool
#endif


#define V_S(x) #x
#define V_S_(x) V_S(x)
#define S__LINE__ V_S_(__LINE__)

#ifdef VULKAN_DEBUG
#define VK_ASSERT(x)                                             \
	do                                                           \
	{                                                            \
		if (!bool(x))                                            \
		{                                                        \
			LOGE("Vulkan error at %s:%d.\n", __FILE__, __LINE__); \
			std::abort();                                        \
		}                                                        \
	} while (0)
#else
#define VK_ASSERT(x) ((void)0)
#endif

namespace Vulkan
{
struct NoCopyNoMove
{
	NoCopyNoMove() = default;
	NoCopyNoMove(const NoCopyNoMove &) = delete;
	void operator=(const NoCopyNoMove &) = delete;
};
}

namespace Vulkan
{
struct DeviceFeatures
{
	bool supports_external = false;
	bool supports_dedicated = false;
	bool supports_debug_marker = false;
	VkPhysicalDeviceFeatures enabled_features = {};
};

enum VendorID
{
	VENDOR_ID_NVIDIA = 0x10de,
	VENDOR_ID_ARM = 0x13b5
};

class Context
{
public:
	Context(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
	        unsigned num_required_device_extensions, const char **required_device_layers,
	        unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features);

	Context(const Context &) = delete;
	void operator=(const Context &) = delete;
	static bool init_loader(PFN_vkGetInstanceProcAddr addr);

	~Context();

	VkInstance get_instance() const
	{
		return instance;
	}

	VkPhysicalDevice get_gpu() const
	{
		return gpu;
	}

	VkDevice get_device() const
	{
		return device;
	}

	VkQueue get_graphics_queue() const
	{
		return graphics_queue;
	}

	VkQueue get_compute_queue() const
	{
		return compute_queue;
	}

	VkQueue get_transfer_queue() const
	{
		return transfer_queue;
	}

	const VkPhysicalDeviceProperties &get_gpu_props() const
	{
		return gpu_props;
	}

	const VkPhysicalDeviceMemoryProperties &get_mem_props() const
	{
		return mem_props;
	}

	uint32_t get_graphics_queue_family() const
	{
		return graphics_queue_family;
	}

	uint32_t get_compute_queue_family() const
	{
		return compute_queue_family;
	}

	uint32_t get_transfer_queue_family() const
	{
		return transfer_queue_family;
	}

	void release_device()
	{
		owned_device = false;
	}

	const DeviceFeatures &get_enabled_device_features() const
	{
		return ext;
	}

	/* True iff the constructor finished successfully. The Context
	 * constructors do not throw; on failure they leave the object in
	 * a destroyable but otherwise unusable state, and the caller must
	 * check is_valid() before doing anything else with it. */
	bool is_valid() const { return valid; }

private:
	VkDevice device = VK_NULL_HANDLE;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;

	VkPhysicalDeviceProperties gpu_props;
	VkPhysicalDeviceMemoryProperties mem_props;

	VkQueue graphics_queue = VK_NULL_HANDLE;
	VkQueue compute_queue = VK_NULL_HANDLE;
	VkQueue transfer_queue = VK_NULL_HANDLE;
	uint32_t graphics_queue_family = VK_QUEUE_FAMILY_IGNORED;
	uint32_t compute_queue_family = VK_QUEUE_FAMILY_IGNORED;
	uint32_t transfer_queue_family = VK_QUEUE_FAMILY_IGNORED;

	bool create_device(VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
	                   unsigned num_required_device_extensions, const char **required_device_layers,
	                   unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features);

	bool owned_device = false;
	bool valid = false;
	DeviceFeatures ext;

	void destroy();
};
}

/* ============================================================
 * vulkan_common.hpp
 * ============================================================ */



namespace Vulkan
{
using HandleCounter = Util::SingleThreadCounter;

template <typename T>
using VulkanObjectPool = Util::ObjectPool<T>;
template <typename T>
using VulkanCache = Util::IntrusiveHashMap<T>;
}

/* ============================================================
 * limits.hpp
 * ============================================================ */


namespace Vulkan
{
static const unsigned VULKAN_NUM_DESCRIPTOR_SETS = 4;
static const unsigned VULKAN_NUM_BINDINGS = 16;
static const unsigned VULKAN_NUM_ATTACHMENTS = 8;
static const unsigned VULKAN_NUM_VERTEX_ATTRIBS = 16;
static const unsigned VULKAN_NUM_VERTEX_BUFFERS = 4;
static const unsigned VULKAN_PUSH_CONSTANT_SIZE = 128;
static const unsigned VULKAN_NUM_SPEC_CONSTANTS = 8;
}

/* ============================================================
 * quirks.hpp
 * ============================================================ */


namespace Vulkan
{
struct ImplementationWorkarounds
{
	bool optimize_all_graphics_barrier = false;
};
}

/* ============================================================
 * texture_format.hpp
 * ============================================================ */



namespace Vulkan
{
class TextureFormatLayout
{
public:
	void set_1d(VkFormat format, uint32_t width, uint32_t array_layers = 1, uint32_t mip_levels = 1);
	void set_2d(VkFormat format, uint32_t width, uint32_t height, uint32_t array_layers = 1, uint32_t mip_levels = 1);
	void set_3d(VkFormat format, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_levels = 1);

	static uint32_t format_block_size(VkFormat format);
	static void format_block_dim(VkFormat format, uint32_t &width, uint32_t &height);
	static uint32_t num_miplevels(uint32_t width, uint32_t height = 1, uint32_t depth = 1);

	void set_buffer(void *buffer, size_t size);
	inline void *get_buffer()
	{
		return buffer;
	}

	uint32_t get_width(uint32_t mip = 0) const;
	uint32_t get_height(uint32_t mip = 0) const;
	uint32_t get_depth(uint32_t mip = 0) const;
	VkFormat get_format() const;

	size_t get_required_size() const;

	size_t row_byte_stride(uint32_t row_length) const;
	size_t layer_byte_stride(uint32_t row_length, size_t row_byte_stride) const;

	inline size_t get_row_size(uint32_t mip) const
	{
		return mips[mip].block_row_length * block_stride;
	}

	inline size_t get_layer_size(uint32_t mip) const
	{
		return mips[mip].block_image_height * get_row_size(mip);
	}

	struct MipInfo
	{
		size_t offset = 0;
		uint32_t width = 1;
		uint32_t height = 1;
		uint32_t depth = 1;

		uint32_t block_image_height = 0;
		uint32_t block_row_length = 0;
		uint32_t image_height = 0;
		uint32_t row_length = 0;
	};

	const MipInfo &get_mip_info(uint32_t mip) const;

	inline void *data(uint32_t layer = 0, uint32_t mip = 0) const
	{
		assert(buffer);
		assert(buffer_size == required_size);
		const MipInfo &mip_info = mips[mip];
		uint8_t *slice = buffer + mip_info.offset;
		slice += block_stride * layer * mip_info.block_row_length * mip_info.block_image_height;
		return slice;
	}

	template <typename T>
	inline T *data_generic(uint32_t x, uint32_t y, uint32_t slice_index, uint32_t mip = 0) const
	{
		const MipInfo &mip_info = mips[mip];
		T *slice = reinterpret_cast<T *>(buffer + mip_info.offset);
		slice += slice_index * mip_info.block_row_length * mip_info.block_image_height;
		slice += y * mip_info.block_row_length;
		slice += x;
		return slice;
	}

	template <typename T>
	inline T *data_1d(uint32_t x, uint32_t layer = 0, uint32_t mip = 0) const
	{
		assert(sizeof(T) == block_stride);
		assert(buffer);
		assert(image_type == VK_IMAGE_TYPE_1D);
		assert(buffer_size == required_size);
		return data_generic<T>(x, 0, layer, mip);
	}

	template <typename T>
	inline T *data_2d(uint32_t x, uint32_t y, uint32_t layer = 0, uint32_t mip = 0) const
	{
		assert(sizeof(T) == block_stride);
		assert(buffer);
		assert(image_type == VK_IMAGE_TYPE_2D);
		assert(buffer_size == required_size);
		return data_generic<T>(x, y, layer, mip);
	}

	template <typename T>
	inline T *data_3d(uint32_t x, uint32_t y, uint32_t z, uint32_t mip = 0) const
	{
		assert(sizeof(T) == block_stride);
		assert(buffer);
		assert(image_type == VK_IMAGE_TYPE_3D);
		assert(buffer_size == required_size);
		return data_generic<T>(x, y, z, mip);
	}

	void build_buffer_image_copies(VkBufferImageCopy *copies, unsigned &num_copies) const;

private:
	uint8_t *buffer = nullptr;
	size_t buffer_size = 0;

	VkImageType image_type = VK_IMAGE_TYPE_RANGE_SIZE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	size_t required_size = 0;

	uint32_t block_stride = 1;
	uint32_t mip_levels = 1;
	uint32_t array_layers = 1;
	uint32_t block_dim_x = 1;
	uint32_t block_dim_y = 1;

	MipInfo mips[16];

	void fill_mipinfo(uint32_t width, uint32_t height, uint32_t depth);
};
}

/* ============================================================
 * cookie.hpp
 * ============================================================ */



namespace Vulkan
{
class Device;

class Cookie
{
public:
	Cookie(Device *device);

	uint64_t get_cookie() const
	{
		return cookie;
	}

private:
	uint64_t cookie;
};

template <typename T>
using HashedObject = Util::IntrusiveHashMapEnabled<T>;
}

/* ============================================================
 * format.hpp
 * ============================================================ */



/* Pure-VkFormat predicates and the aspect-mask classifier are all simple
 * switch statements; they have no C++ dependencies and could be lowered to C
 * verbatim after a future namespace-removal pass. The TextureFormatLayout-
 * dependent helpers below are kept separate and remain C++-only. */
static inline bool format_has_depth_aspect(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return true;

	default:
		return false;
	}
}

static inline bool format_has_stencil_aspect(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
	case VK_FORMAT_S8_UINT:
		return true;

	default:
		return false;
	}
}

static inline bool format_has_depth_or_stencil_aspect(VkFormat format)
{
	return format_has_depth_aspect(format) || format_has_stencil_aspect(format);
}

static inline VkImageAspectFlags format_to_aspect_mask(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_UNDEFINED:
		return 0;

	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;

	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;

	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
		return VK_IMAGE_ASPECT_DEPTH_BIT;

	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

/* ============================================================
 * sampler.hpp
 * ============================================================ */



namespace Vulkan
{
enum class StockSampler
{
	NearestClamp,
	LinearClamp,
	TrilinearClamp,
	NearestWrap,
	LinearWrap,
	TrilinearWrap,
	NearestShadow,
	LinearShadow,
	Count
};

struct SamplerCreateInfo
{
	VkFilter mag_filter;
	VkFilter min_filter;
	VkSamplerMipmapMode mipmap_mode;
	VkSamplerAddressMode address_mode_u;
	VkSamplerAddressMode address_mode_v;
	VkSamplerAddressMode address_mode_w;
	float mip_lod_bias;
	VkBool32 anisotropy_enable;
	float max_anisotropy;
	VkBool32 compare_enable;
	VkCompareOp compare_op;
	float min_lod;
	float max_lod;
	VkBorderColor border_color;
	VkBool32 unnormalized_coordinates;
};

class Sampler;
struct SamplerDeleter
{
	void operator()(Sampler *sampler);
};

class Sampler : public Util::IntrusivePtrEnabled<Sampler, SamplerDeleter, HandleCounter>,
                public Cookie
{
public:
	friend struct SamplerDeleter;
	~Sampler();

	VkSampler get_sampler() const
	{
		return sampler;
	}

private:
	friend class Util::ObjectPool<Sampler>;
	Sampler(Device *device, VkSampler sampler);

	Device *device;
	VkSampler sampler;
};
using SamplerHandle = Util::IntrusivePtr<Sampler>;
}

/* ============================================================
 * memory_allocator.hpp
 * ============================================================ */

#ifndef FRAMEWORK_MEMORY_ALLOCATOR_HPP
#define FRAMEWORK_MEMORY_ALLOCATOR_HPP


namespace Vulkan
{
static inline uint32_t log2_integer(uint32_t v)
{
	v--;
	return 32 - leading_zeroes(v);
}

enum MemoryClass
{
	MEMORY_CLASS_SMALL = 0,
	MEMORY_CLASS_MEDIUM,
	MEMORY_CLASS_LARGE,
	MEMORY_CLASS_HUGE,
	MEMORY_CLASS_COUNT
};

enum AllocationTiling
{
	ALLOCATION_TILING_LINEAR = 0,
	ALLOCATION_TILING_OPTIMAL,
	ALLOCATION_TILING_COUNT
};

enum MemoryAccessFlag
{
	MEMORY_ACCESS_WRITE_BIT = 1,
	MEMORY_ACCESS_READ_BIT = 2,
	MEMORY_ACCESS_READ_WRITE_BIT = MEMORY_ACCESS_WRITE_BIT | MEMORY_ACCESS_READ_BIT
};
using MemoryAccessFlags = uint32_t;

struct DeviceAllocation;
class DeviceAllocator;

class Block
{
public:
	enum
	{
		NumSubBlocks = 32u,
		AllFree = ~0u
	};

	Block(const Block &) = delete;
	void operator=(const Block &) = delete;

	Block()
	{
		for (uint32_t &v : free_blocks)
			v = AllFree;
		longest_run = 32;
	}

	~Block()
	{
		if (free_blocks[0] != AllFree)
			LOGE("Memory leak in block detected.\n");
	}

	inline bool full() const
	{
		return free_blocks[0] == 0;
	}

	inline bool empty() const
	{
		return free_blocks[0] == AllFree;
	}

	inline uint32_t get_longest_run() const
	{
		return longest_run;
	}

	void allocate(uint32_t num_blocks, DeviceAllocation *block);
	void free(uint32_t mask);

private:
	uint32_t free_blocks[NumSubBlocks];
	uint32_t longest_run = 0;

	inline void update_longest_run()
	{
		uint32_t f = free_blocks[0];
		longest_run = 0;

		while (f)
		{
			free_blocks[longest_run++] = f;
			f &= f >> 1;
		}
	}
};

struct MiniHeap;
class ClassAllocator;
class DeviceAllocator;
class Allocator;

struct DeviceAllocation
{
	friend class ClassAllocator;
	friend class Allocator;
	friend class Block;
	friend class DeviceAllocator;

public:
	inline VkDeviceMemory get_memory() const
	{
		return base;
	}

	inline uint32_t get_offset() const
	{
		return offset;
	}

	inline uint32_t get_size() const
	{
		return size;
	}

	void free_immediate();
	void free_immediate(DeviceAllocator &allocator);

private:
	VkDeviceMemory base = VK_NULL_HANDLE;
	uint8_t *host_base = nullptr;
	ClassAllocator *alloc = nullptr;
	Util::IntrusiveList<MiniHeap>::Iterator heap = {};
	uint32_t offset = 0;
	uint32_t mask = 0;
	uint32_t size = 0;

	uint8_t tiling = 0;
	uint8_t memory_type = 0;
	bool hierarchical = false;

	void free_global(DeviceAllocator &allocator, uint32_t size, uint32_t memory_type);
};

struct MiniHeap : Util::IntrusiveListEnabled<MiniHeap>
{
	DeviceAllocation allocation;
	Block heap;
};

class Allocator;

class ClassAllocator
{
public:
	friend class Allocator;
	~ClassAllocator();

	inline void set_tiling_mask(uint32_t mask)
	{
		tiling_mask = mask;
	}

	inline void set_sub_block_size(uint32_t size)
	{
		sub_block_size_log2 = log2_integer(size);
		sub_block_size = size;
	}

	bool allocate(uint32_t size, AllocationTiling tiling, DeviceAllocation *alloc, bool hierarchical);
	void free(DeviceAllocation *alloc);

private:
	ClassAllocator() = default;
	struct AllocationTilingHeaps
	{
		Util::IntrusiveList<MiniHeap> heaps[Block::NumSubBlocks];
		Util::IntrusiveList<MiniHeap> full_heaps;
		uint32_t heap_availability_mask = 0;
	};
	ClassAllocator *parent = nullptr;
	AllocationTilingHeaps tiling_modes[ALLOCATION_TILING_COUNT];
	Util::ObjectPool<MiniHeap> object_pool;

	uint32_t sub_block_size = 1;
	uint32_t sub_block_size_log2 = 0;
	uint32_t tiling_mask = ~0u;
	uint32_t memory_type = 0;
	DeviceAllocator *global_allocator = nullptr;

	void set_global_allocator(DeviceAllocator *allocator)
	{
		global_allocator = allocator;
	}

	void set_memory_type(uint32_t type)
	{
		memory_type = type;
	}

	void suballocate(uint32_t num_blocks, uint32_t tiling, uint32_t memory_type, MiniHeap &heap,
	                 DeviceAllocation *alloc);

	inline void set_parent(ClassAllocator *allocator)
	{
		parent = allocator;
	}
};

class Allocator
{
public:
	Allocator();
	void operator=(const Allocator &) = delete;
	Allocator(const Allocator &) = delete;

	bool allocate(uint32_t size, uint32_t alignment, AllocationTiling tiling, DeviceAllocation *alloc);
	bool allocate_global(uint32_t size, DeviceAllocation *alloc);
	bool allocate_dedicated(uint32_t size, DeviceAllocation *alloc, VkImage image);
	inline ClassAllocator &get_class_allocator(MemoryClass clazz)
	{
		return classes[static_cast<unsigned>(clazz)];
	}

	static void free(DeviceAllocation *alloc)
	{
		alloc->free_immediate();
	}

	void set_memory_type(uint32_t memory_type)
	{
		for (ClassAllocator &sub : classes)
			sub.set_memory_type(memory_type);
		this->memory_type = memory_type;
	}

	void set_global_allocator(DeviceAllocator *allocator)
	{
		for (ClassAllocator &sub : classes)
			sub.set_global_allocator(allocator);
		global_allocator = allocator;
	}

private:
	ClassAllocator classes[MEMORY_CLASS_COUNT];
	DeviceAllocator *global_allocator = nullptr;
	uint32_t memory_type = 0;
};

class DeviceAllocator
{
public:
	void init(VkPhysicalDevice gpu, VkDevice device);
	void set_supports_dedicated_allocation(bool enable)
	{
		use_dedicated = enable;
	}

	~DeviceAllocator();

	bool allocate(uint32_t size, uint32_t alignment, uint32_t memory_type, AllocationTiling tiling,
	              DeviceAllocation *alloc);
	bool allocate_image_memory(uint32_t size, uint32_t alignment, uint32_t memory_type, AllocationTiling tiling,
	                           DeviceAllocation *alloc, VkImage image);

	bool allocate_global(uint32_t size, uint32_t memory_type, DeviceAllocation *alloc);

	void garbage_collect();
	void *map_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags);
	void unmap_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags);

	bool allocate(uint32_t size, uint32_t memory_type, VkDeviceMemory *memory, uint8_t **host_memory, VkImage dedicated_image);
	void free(uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory);
	void free_no_recycle(uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory);

private:
	std::vector<std::unique_ptr<Allocator>> allocators;
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties mem_props;
	VkDeviceSize atom_alignment = 1;
	bool use_dedicated = false;

	struct Allocation
	{
		VkDeviceMemory memory;
		uint8_t *host_memory;
		uint32_t size;
		uint32_t type;
	};

	struct Heap
	{
		uint64_t size = 0;
		std::vector<Allocation> blocks;
		void garbage_collect(VkDevice device);
	};

	std::vector<Heap> heaps;
};
}

#endif

/* ============================================================
 * buffer.hpp
 * ============================================================ */



namespace Vulkan
{
class Device;

static inline VkPipelineStageFlags buffer_usage_to_possible_stages(VkBufferUsageFlags usage)
{
	VkPipelineStageFlags flags = 0;
	if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
		flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
		flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
	if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
	             VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
		         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

	return flags;
}

static inline VkAccessFlags buffer_usage_to_possible_access(VkBufferUsageFlags usage)
{
	VkAccessFlags flags = 0;
	if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
		flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
		flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
		flags |= VK_ACCESS_INDEX_READ_BIT;
	if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
		flags |= VK_ACCESS_UNIFORM_READ_BIT;
	if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		flags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

	return flags;
}

enum class BufferDomain
{
	Device, // Device local. Probably not visible from CPU.
	Host, // Host-only, needs to be synced to GPU. Might be device local as well on iGPUs.
	CachedHost // Host-only, used for readbacks.
};

struct BufferCreateInfo
{
	BufferDomain domain = BufferDomain::Device;
	VkDeviceSize size = 0;
	VkBufferUsageFlags usage = 0;
};

class Buffer;
struct BufferDeleter
{
	void operator()(Buffer *buffer);
};

class BufferView;
struct BufferViewDeleter
{
	void operator()(BufferView *view);
};

class Buffer : public Util::IntrusivePtrEnabled<Buffer, BufferDeleter, HandleCounter>,
               public Cookie
{
public:
	friend struct BufferDeleter;
	~Buffer();

	VkBuffer get_buffer() const
	{
		return buffer;
	}

	const BufferCreateInfo &get_create_info() const
	{
		return info;
	}

	const DeviceAllocation &get_allocation() const
	{
		return alloc;
	}

private:
	friend class Util::ObjectPool<Buffer>;
	Buffer(Device *device, VkBuffer buffer, const DeviceAllocation &alloc, const BufferCreateInfo &info);

	Device *device;
	VkBuffer buffer;
	DeviceAllocation alloc;
	BufferCreateInfo info;
};
using BufferHandle = Util::IntrusivePtr<Buffer>;

struct BufferViewCreateInfo
{
	const Buffer *buffer;
	VkFormat format;
	VkDeviceSize offset;
	VkDeviceSize range;
};

class BufferView : public Util::IntrusivePtrEnabled<BufferView, BufferViewDeleter, HandleCounter>,
                   public Cookie
{
public:
	friend struct BufferViewDeleter;
	~BufferView();

	VkBufferView get_view() const
	{
		return view;
	}

	const Buffer &get_buffer() const
	{
		return *info.buffer;
	}

private:
	friend class Util::ObjectPool<BufferView>;
	BufferView(Device *device, VkBufferView view, const BufferViewCreateInfo &info);

	Device *device;
	VkBufferView view;
	BufferViewCreateInfo info;
};
using BufferViewHandle = Util::IntrusivePtr<BufferView>;
}

/* ============================================================
 * image.hpp
 * ============================================================ */



namespace Vulkan
{
class Device;

static inline VkPipelineStageFlags image_usage_to_possible_stages(VkImageUsageFlags usage)
{
	VkPipelineStageFlags flags = 0;

	if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
		         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

	if (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
	{
		VkPipelineStageFlags possible = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		                                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

		if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
			possible |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		flags &= possible;
	}

	return flags;
}

static inline VkAccessFlags image_layout_to_possible_access(VkImageLayout layout)
{
	switch (layout)
	{
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
		return VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		return VK_ACCESS_TRANSFER_READ_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return VK_ACCESS_TRANSFER_WRITE_BIT;
	default:
		return ~0u;
	}
}

static inline VkAccessFlags image_usage_to_possible_access(VkImageUsageFlags usage)
{
	VkAccessFlags flags = 0;

	if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_ACCESS_SHADER_READ_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

	// Transient attachments can only be attachments, and never other resources.
	if (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
	{
		flags &= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		         VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	}

	return flags;
}

static inline uint32_t image_num_miplevels(const VkExtent3D &extent)
{
	uint32_t wh = (extent.width > extent.height) ? extent.width : extent.height;
	uint32_t size = (wh > extent.depth) ? wh : extent.depth;
	uint32_t levels = 0;
	while (size)
	{
		levels++;
		size >>= 1;
	}
	return levels;
}

static inline VkFormatFeatureFlags image_usage_to_features(VkImageUsageFlags usage)
{
	VkFormatFeatureFlags flags = 0;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

	return flags;
}

struct ImageInitialData
{
	const void *data;
	unsigned row_length;
	unsigned image_height;
};

enum ImageMiscFlagBits
{
	IMAGE_MISC_GENERATE_MIPS_BIT = 1 << 0
};
using ImageMiscFlags = uint32_t;

class Image;

struct ImageViewCreateInfo
{
	Image *image = nullptr;
	VkFormat format = VK_FORMAT_UNDEFINED;
	unsigned base_level = 0;
	unsigned levels = VK_REMAINING_MIP_LEVELS;
	unsigned base_layer = 0;
	unsigned layers = VK_REMAINING_ARRAY_LAYERS;
	VkComponentMapping swizzle = {
			VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,
	};
};

class ImageView;

struct ImageViewDeleter
{
	void operator()(ImageView *view);
};

class ImageView : public Util::IntrusivePtrEnabled<ImageView, ImageViewDeleter, HandleCounter>,
                  public Cookie
{
public:
	friend struct ImageViewDeleter;

	ImageView(Device *device, VkImageView view, const ImageViewCreateInfo &info);

	~ImageView();

	void set_alt_views(VkImageView depth, VkImageView stencil)
	{
		VK_ASSERT(depth_view == VK_NULL_HANDLE);
		VK_ASSERT(stencil_view == VK_NULL_HANDLE);
		depth_view = depth;
		stencil_view = stencil;
	}

	void set_render_target_views(std::vector<VkImageView> views)
	{
		VK_ASSERT(render_target_views.empty());
		render_target_views = std::move(views);
	}

	// By default, gets a combined view which includes all aspects in the image.
	// This would be used mostly for render targets.
	VkImageView get_view() const
	{
		return view;
	}

	VkImageView get_render_target_view(unsigned layer) const;

	// Gets an image view which only includes floating point domains.
	// Takes effect when we want to sample from an image which is Depth/Stencil,
	// but we only want to sample depth.
	VkImageView get_float_view() const
	{
		return depth_view != VK_NULL_HANDLE ? depth_view : view;
	}

	// Gets an image view which only includes integer domains.
	// Takes effect when we want to sample from an image which is Depth/Stencil,
	// but we only want to sample stencil.
	VkImageView get_integer_view() const
	{
		return stencil_view != VK_NULL_HANDLE ? stencil_view : view;
	}

	VkFormat get_format() const
	{
		return info.format;
	}

	const Image &get_image() const
	{
		return *info.image;
	}

	Image &get_image()
	{
		return *info.image;
	}

	const ImageViewCreateInfo &get_create_info() const
	{
		return info;
	}

private:
	Device *device;
	VkImageView view;
	std::vector<VkImageView> render_target_views;
	VkImageView depth_view = VK_NULL_HANDLE;
	VkImageView stencil_view = VK_NULL_HANDLE;
	ImageViewCreateInfo info;
};

using ImageViewHandle = Util::IntrusivePtr<ImageView>;

enum class ImageDomain
{
	Physical,
	Transient
};

struct ImageCreateInfo
{
	ImageDomain domain = ImageDomain::Physical;
	unsigned width = 0;
	unsigned height = 0;
	unsigned depth = 1;
	unsigned levels = 1;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImageType type = VK_IMAGE_TYPE_2D;
	unsigned layers = 1;
	VkImageUsageFlags usage = 0;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageCreateFlags flags = 0;
	ImageMiscFlags misc = 0;
	VkImageLayout initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	VkComponentMapping swizzle = {
			VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,
	};

	static ImageCreateInfo immutable_2d_image(unsigned width, unsigned height, VkFormat format, bool mipmapped = false)
	{
		ImageCreateInfo info;
		info.width = width;
		info.height = height;
		info.depth = 1;
		info.levels = mipmapped ? 0u : 1u;
		info.format = format;
		info.type = VK_IMAGE_TYPE_2D;
		info.layers = 1;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.flags = 0;
		info.misc = mipmapped ? unsigned(IMAGE_MISC_GENERATE_MIPS_BIT) : 0u;
		info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		return info;
	}

	static ImageCreateInfo render_target(unsigned width, unsigned height, VkFormat format)
	{
		ImageCreateInfo info;
		info.width = width;
		info.height = height;
		info.depth = 1;
		info.levels = 1;
		info.format = format;
		info.type = VK_IMAGE_TYPE_2D;
		info.layers = 1;
		info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
		              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
		             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.flags = 0;
		info.misc = 0;
		info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		return info;
	}

	static ImageCreateInfo transient_render_target(unsigned width, unsigned height, VkFormat format)
	{
		ImageCreateInfo info;
		info.domain = ImageDomain::Transient;
		info.width = width;
		info.height = height;
		info.depth = 1;
		info.levels = 1;
		info.format = format;
		info.type = VK_IMAGE_TYPE_2D;
		info.layers = 1;
		info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
		              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
		             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.flags = 0;
		info.misc = 0;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		return info;
	}
};

class Image;

struct ImageDeleter
{
	void operator()(Image *image);
};

enum class Layout
{
	Optimal,
	General
};

class Image : public Util::IntrusivePtrEnabled<Image, ImageDeleter, HandleCounter>,
              public Cookie
{
public:
	friend struct ImageDeleter;

	~Image();

	Image(Image &&) = delete;

	Image &operator=(Image &&) = delete;

	const ImageView &get_view() const
	{
		VK_ASSERT(view);
		return *view;
	}

	ImageView &get_view()
	{
		VK_ASSERT(view);
		return *view;
	}

	VkImage get_image() const
	{
		return image;
	}

	VkFormat get_format() const
	{
		return create_info.format;
	}

	uint32_t get_width(uint32_t lod = 0) const
	{
		uint32_t v = create_info.width >> lod;
		return v > 1u ? v : 1u;
	}

	uint32_t get_height(uint32_t lod = 0) const
	{
		uint32_t v = create_info.height >> lod;
		return v > 1u ? v : 1u;
	}

	uint32_t get_depth(uint32_t lod = 0) const
	{
		uint32_t v = create_info.depth >> lod;
		return v > 1u ? v : 1u;
	}

	const ImageCreateInfo &get_create_info() const
	{
		return create_info;
	}

	VkImageLayout get_layout(VkImageLayout optimal) const
	{
		return layout_type == Layout::Optimal ? optimal : VK_IMAGE_LAYOUT_GENERAL;
	}

	Layout get_layout_type() const
	{
		return layout_type;
	}

	void set_layout(Layout layout)
	{
		layout_type = layout;
	}

	void set_stage_flags(VkPipelineStageFlags flags)
	{
		stage_flags = flags;
	}

	void set_access_flags(VkAccessFlags flags)
	{
		access_flags = flags;
	}

	VkPipelineStageFlags get_stage_flags() const
	{
		return stage_flags;
	}

	VkAccessFlags get_access_flags() const
	{
		return access_flags;
	}

	const DeviceAllocation &get_allocation() const
	{
		return alloc;
	}

private:
	friend class Util::ObjectPool<Image>;

	Image(Device *device, VkImage image, VkImageView default_view, const DeviceAllocation &alloc,
	      const ImageCreateInfo &info);

	Device *device;
	VkImage image;
	ImageViewHandle view;
	DeviceAllocation alloc;
	ImageCreateInfo create_info;

	Layout layout_type = Layout::Optimal;
	VkPipelineStageFlags stage_flags = 0;
	VkAccessFlags access_flags = 0;
};

using ImageHandle = Util::IntrusivePtr<Image>;
}

/* ============================================================
 * fence.hpp
 * ============================================================ */



namespace Vulkan
{
class Device;

class FenceHolder;
struct FenceHolderDeleter
{
	void operator()(FenceHolder *fence);
};

class FenceHolder : public Util::IntrusivePtrEnabled<FenceHolder, FenceHolderDeleter, HandleCounter>
{
public:
	friend struct FenceHolderDeleter;

	~FenceHolder();
	void wait();

private:
	friend class Util::ObjectPool<FenceHolder>;
	FenceHolder(Device *device, VkFence fence) : device(device), fence(fence)
	{
	}

	Device *device;
	VkFence fence;
};

using Fence = Util::IntrusivePtr<FenceHolder>;
}

/* ============================================================
 * fence_manager.hpp
 * ============================================================ */



namespace Vulkan
{
class FenceManager
{
public:
	void init(VkDevice device);
	~FenceManager();

	VkFence request_cleared_fence();
	void recycle_fence(VkFence fence);

private:
	VkDevice device;
	std::vector<VkFence> fences;
};
}

/* ============================================================
 * semaphore.hpp
 * ============================================================ */



namespace Vulkan
{
class Device;

class SemaphoreHolder;
struct SemaphoreHolderDeleter
{
	void operator()(SemaphoreHolder *semaphore);
};

class SemaphoreHolder : public Util::IntrusivePtrEnabled<SemaphoreHolder, SemaphoreHolderDeleter, HandleCounter>
{
public:
	friend struct SemaphoreHolderDeleter;

	~SemaphoreHolder();

	bool is_signalled() const
	{
		return signalled;
	}

	VkSemaphore consume()
	{
		VkSemaphore ret = semaphore;
		VK_ASSERT(semaphore);
		VK_ASSERT(signalled);
		semaphore = VK_NULL_HANDLE;
		signalled = false;
		return ret;
	}

private:
	friend class Util::ObjectPool<SemaphoreHolder>;
	SemaphoreHolder(Device *device, VkSemaphore semaphore, bool signalled)
	    : device(device)
	    , semaphore(semaphore)
	    , signalled(signalled)
	{
	}

	Device *device;
	VkSemaphore semaphore;
	bool signalled = true;
};

using Semaphore = Util::IntrusivePtr<SemaphoreHolder>;
}

/* ============================================================
 * semaphore_manager.hpp
 * ============================================================ */



namespace Vulkan
{
class SemaphoreManager
{
public:
	void init(VkDevice device);
	~SemaphoreManager();

	VkSemaphore request_cleared_semaphore();
	void recycle(VkSemaphore semaphore);

private:
	VkDevice device = VK_NULL_HANDLE;
	std::vector<VkSemaphore> semaphores;
};
}

/* ============================================================
 * descriptor_set.hpp
 * ============================================================ */



namespace Vulkan
{
class Device;
struct DescriptorSetLayout
{
	uint32_t sampled_image_mask = 0;
	uint32_t storage_image_mask = 0;
	uint32_t uniform_buffer_mask = 0;
	uint32_t storage_buffer_mask = 0;
	uint32_t sampled_buffer_mask = 0;
	uint32_t input_attachment_mask = 0;
	uint32_t sampler_mask = 0;
	uint32_t separate_image_mask = 0;
	uint32_t fp_mask = 0;
	uint32_t immutable_sampler_mask = 0;
	uint64_t immutable_samplers = 0;
};

// Avoid -Wclass-memaccess warnings since we hash DescriptorSetLayout.

static inline bool has_immutable_sampler(const DescriptorSetLayout &layout, unsigned binding)
{
	return (layout.immutable_sampler_mask & (1u << binding)) != 0;
}

static inline StockSampler get_immutable_sampler(const DescriptorSetLayout &layout, unsigned binding)
{
	VK_ASSERT(has_immutable_sampler(layout, binding));
	return static_cast<StockSampler>((layout.immutable_samplers >> (4 * binding)) & 0xf);
}

static inline void set_immutable_sampler(DescriptorSetLayout &layout, unsigned binding, StockSampler sampler)
{
	layout.immutable_samplers |= uint64_t(sampler) << (4 * binding);
	layout.immutable_sampler_mask |= 1u << binding;
}

static const unsigned VULKAN_NUM_SETS_PER_POOL = 16;
static const unsigned VULKAN_DESCRIPTOR_RING_SIZE = 8;

class DescriptorSetAllocator : public HashedObject<DescriptorSetAllocator>
{
public:
	DescriptorSetAllocator(Util::Hash hash, Device *device, const DescriptorSetLayout &layout, const uint32_t *stages_for_bindings);
	~DescriptorSetAllocator();
	void operator=(const DescriptorSetAllocator &) = delete;
	DescriptorSetAllocator(const DescriptorSetAllocator &) = delete;

	void begin_frame();
	std::pair<VkDescriptorSet, bool> find(Util::Hash hash);

	VkDescriptorSetLayout get_layout() const
	{
		return set_layout;
	}

	void clear();

private:
	struct DescriptorSetNode : Util::TemporaryHashmapEnabled<DescriptorSetNode>, Util::IntrusiveListEnabled<DescriptorSetNode>
	{
		DescriptorSetNode(VkDescriptorSet set)
		    : set(set)
		{
		}

		VkDescriptorSet set;
	};

	Device *device;
	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;

	struct PerThread
	{
		Util::TemporaryHashmap<DescriptorSetNode, VULKAN_DESCRIPTOR_RING_SIZE, true> set_nodes;
		std::vector<VkDescriptorPool> pools;
		bool should_begin = true;
	};
	PerThread per_thread;
	std::vector<VkDescriptorPoolSize> pool_size;
};
}

/* ============================================================
 * shader.hpp
 * ============================================================ */



namespace Vulkan
{
class Device;

enum class ShaderStage
{
	Vertex = 0,
	TessControl = 1,
	TessEvaluation = 2,
	Geometry = 3,
	Fragment = 4,
	Compute = 5,
	Count
};

struct ResourceLayout
{
	uint32_t input_mask = 0;
	uint32_t output_mask = 0;
	uint32_t push_constant_size = 0;
	uint32_t spec_constant_mask = 0;
	DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS];
};

struct CombinedResourceLayout
{
	uint32_t attribute_mask = 0;
	uint32_t render_target_mask = 0;
	DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	uint32_t stages_for_bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
	uint32_t stages_for_sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	VkPushConstantRange push_constant_range = {};
	uint32_t descriptor_set_mask = 0;
	uint32_t spec_constant_mask[(unsigned)ShaderStage::Count] = {};
	uint32_t combined_spec_constant_mask = 0;
	Util::Hash push_constant_layout_hash = 0;
};

class PipelineLayout : public HashedObject<PipelineLayout>
{
public:
	PipelineLayout(Util::Hash hash, Device *device, const CombinedResourceLayout &layout);
	~PipelineLayout();

	const CombinedResourceLayout &get_resource_layout() const
	{
		return layout;
	}

	VkPipelineLayout get_layout() const
	{
		return pipe_layout;
	}

	DescriptorSetAllocator *get_allocator(unsigned set) const
	{
		return set_allocators[set];
	}

private:
	Device *device;
	VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
	CombinedResourceLayout layout;
	DescriptorSetAllocator *set_allocators[VULKAN_NUM_DESCRIPTOR_SETS] = {};
};

class Shader : public HashedObject<Shader>
{
public:
	Shader(Util::Hash hash, Device *device, const uint32_t *data, size_t size);
	~Shader();

	const ResourceLayout &get_layout() const
	{
		return layout;
	}

	VkShaderModule get_module() const
	{
		return module;
	}

	static const char *stage_to_name(ShaderStage stage);

private:
	Device *device;
	VkShaderModule module;
	ResourceLayout layout;
};

class Program : public HashedObject<Program>
{
public:
	Program(Device *device, Shader *vertex, Shader *fragment);
	Program(Device *device, Shader *compute);
	~Program();

	inline const Shader *get_shader(ShaderStage stage) const
	{
		return shaders[(unsigned)stage];
	}

	void set_pipeline_layout(PipelineLayout *new_layout)
	{
		layout = new_layout;
	}

	PipelineLayout *get_pipeline_layout() const
	{
		return layout;
	}

	VkPipeline get_pipeline(Util::Hash hash) const;
	VkPipeline add_pipeline(Util::Hash hash, VkPipeline pipeline);

private:
	void set_shader(ShaderStage stage, Shader *handle);
	Device *device;
	Shader *shaders[(unsigned)ShaderStage::Count] = {};
	PipelineLayout *layout = nullptr;
	VulkanCache<Util::IntrusivePODWrapper<VkPipeline>> pipelines;
};
}

/* ============================================================
 * render_pass.hpp
 * ============================================================ */



namespace Vulkan
{
enum RenderPassOp
{
	RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT = 1 << 0,
	RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT = 1 << 1,
	RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT = 1 << 2,
	RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT = 1 << 3,
	RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT = 1 << 4,
	RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT = 1 << 5
};
using RenderPassOpFlags = uint32_t;

class ImageView;
struct RenderPassInfo
{
	ImageView *color_attachments[VULKAN_NUM_ATTACHMENTS];
	ImageView *depth_stencil = nullptr;
	unsigned num_color_attachments = 0;
	RenderPassOpFlags op_flags = 0;
	uint32_t clear_attachments = 0;
	uint32_t load_attachments = 0;
	uint32_t store_attachments = 0;
	uint32_t layer = 0;

	// Render area will be clipped to the actual framebuffer.
	VkRect2D render_area = { { 0, 0 }, { UINT32_MAX, UINT32_MAX } };

	VkClearColorValue clear_color[VULKAN_NUM_ATTACHMENTS] = {};
	VkClearDepthStencilValue clear_depth_stencil = { 1.0f, 0 };

	enum class DepthStencil
	{
		None,
		ReadOnly,
		ReadWrite
	};

	struct Subpass
	{
		uint32_t color_attachments[VULKAN_NUM_ATTACHMENTS];
		uint32_t input_attachments[VULKAN_NUM_ATTACHMENTS];
		uint32_t resolve_attachments[VULKAN_NUM_ATTACHMENTS];
		unsigned num_color_attachments = 0;
		unsigned num_input_attachments = 0;
		unsigned num_resolve_attachments = 0;
		DepthStencil depth_stencil_mode = DepthStencil::ReadWrite;
	};
	// If 0/nullptr, assume a default subpass.
	const Subpass *subpasses = nullptr;
	unsigned num_subpasses = 0;
};

class RenderPass : public HashedObject<RenderPass>, public NoCopyNoMove
{
public:
	struct SubpassInfo
	{
		VkAttachmentReference color_attachments[VULKAN_NUM_ATTACHMENTS];
		unsigned num_color_attachments;
		VkAttachmentReference input_attachments[VULKAN_NUM_ATTACHMENTS];
		unsigned num_input_attachments;
		VkAttachmentReference depth_stencil_attachment;

		unsigned samples;
	};

	RenderPass(Util::Hash hash, Device *device, const RenderPassInfo &info);
	~RenderPass();

	VkRenderPass get_render_pass() const
	{
		return render_pass;
	}

	uint32_t get_sample_count(unsigned subpass) const
	{
		VK_ASSERT(subpass < subpasses.size());
		return subpasses[subpass].samples;
	}

	unsigned get_num_color_attachments(unsigned subpass) const
	{
		VK_ASSERT(subpass < subpasses.size());
		return subpasses[subpass].num_color_attachments;
	}

	unsigned get_num_input_attachments(unsigned subpass) const
	{
		VK_ASSERT(subpass < subpasses.size());
		return subpasses[subpass].num_input_attachments;
	}

	const VkAttachmentReference &get_color_attachment(unsigned subpass, unsigned index) const
	{
		VK_ASSERT(subpass < subpasses.size());
		VK_ASSERT(index < subpasses[subpass].num_color_attachments);
		return subpasses[subpass].color_attachments[index];
	}

	const VkAttachmentReference &get_input_attachment(unsigned subpass, unsigned index) const
	{
		VK_ASSERT(subpass < subpasses.size());
		VK_ASSERT(index < subpasses[subpass].num_input_attachments);
		return subpasses[subpass].input_attachments[index];
	}

	bool has_depth(unsigned subpass) const
	{
		VK_ASSERT(subpass < subpasses.size());
		return subpasses[subpass].depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED &&
		       format_has_depth_aspect(depth_stencil);
	}

private:
	Device *device;
	VkRenderPass render_pass = VK_NULL_HANDLE;

	VkFormat color_attachments[VULKAN_NUM_ATTACHMENTS] = {};
	VkFormat depth_stencil = VK_FORMAT_UNDEFINED;
	std::vector<SubpassInfo> subpasses;

	void fixup_render_pass_nvidia(VkRenderPassCreateInfo &create_info, VkAttachmentDescription *attachments);
};

class Framebuffer : public Cookie, public NoCopyNoMove
{
public:
	Framebuffer(Device *device, const RenderPass &rp, const RenderPassInfo &info);
	~Framebuffer();

	VkFramebuffer get_framebuffer() const
	{
		return framebuffer;
	}

	ImageView *get_attachment(unsigned index) const
	{
		assert(index < num_attachments);
		return attachments[index];
	}

	uint32_t get_width() const
	{
		return width;
	}

	uint32_t get_height() const
	{
		return height;
	}

	const RenderPass &get_compatible_render_pass() const
	{
		return render_pass;
	}

private:
	Device *device;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	const RenderPass &render_pass;
	RenderPassInfo info;
	uint32_t width = 0;
	uint32_t height = 0;

	ImageView *attachments[VULKAN_NUM_ATTACHMENTS + 1] = {};
	unsigned num_attachments = 0;
};

static const unsigned VULKAN_FRAMEBUFFER_RING_SIZE = 8;
class FramebufferAllocator
{
public:
	FramebufferAllocator(Device *device);
	Framebuffer &request_framebuffer(const RenderPassInfo &info);

	void begin_frame();
	void clear();

private:
	struct FramebufferNode : Util::TemporaryHashmapEnabled<FramebufferNode>,
	                         Util::IntrusiveListEnabled<FramebufferNode>,
	                         Framebuffer
	{
		FramebufferNode(Device *device, const RenderPass &rp, const RenderPassInfo &info)
		    : Framebuffer(device, rp, info)
		{
		}
	};

	Device *device;
	Util::TemporaryHashmap<FramebufferNode, VULKAN_FRAMEBUFFER_RING_SIZE, false> framebuffers;
};

class AttachmentAllocator
{
public:
	AttachmentAllocator(Device *device)
		: device(device)
	{
	}

	ImageView &request_attachment(unsigned width, unsigned height, VkFormat format,
	                              unsigned index = 0, unsigned samples = 1, unsigned layers = 1);

	void begin_frame();
	void clear();

private:
	struct TransientNode : Util::TemporaryHashmapEnabled<TransientNode>, Util::IntrusiveListEnabled<TransientNode>
	{
		TransientNode(ImageHandle handle)
		    : handle(handle)
		{
		}

		ImageHandle handle;
	};

	Device *device;
	Util::TemporaryHashmap<TransientNode, VULKAN_FRAMEBUFFER_RING_SIZE, false> attachments;
};

}


/* ============================================================
 * buffer_pool.hpp
 * ============================================================ */



namespace Vulkan
{
class Device;
class Buffer;

struct BufferBlockAllocation
{
	uint8_t *host;
	VkDeviceSize offset;
};

struct BufferBlock
{
	~BufferBlock();
	Util::IntrusivePtr<Buffer> gpu;
	Util::IntrusivePtr<Buffer> cpu;
	VkDeviceSize offset = 0;
	VkDeviceSize alignment = 0;
	VkDeviceSize size = 0;
	uint8_t *mapped = nullptr;

	BufferBlockAllocation allocate(VkDeviceSize allocate_size)
	{
		VkDeviceSize aligned_offset = (offset + alignment - 1) & ~(alignment - 1);
		if (aligned_offset + allocate_size <= size)
		{
			uint8_t *ret = mapped + aligned_offset;
			offset = aligned_offset + allocate_size;
			return { ret, aligned_offset };
		}
		else
			return { nullptr, 0 };
	}
};

class BufferPool
{
public:
	~BufferPool();
	void init(Device *device, VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage);
	void reset();

	VkDeviceSize get_block_size() const
	{
		return block_size;
	}

	BufferBlock request_block(VkDeviceSize minimum_size);
	void recycle_block(BufferBlock &&block);

private:
	Device *device = nullptr;
	VkDeviceSize block_size = 0;
	VkDeviceSize alignment = 0;
	VkBufferUsageFlags usage = 0;
	std::vector<BufferBlock> blocks;
	BufferBlock allocate_block(VkDeviceSize size);
};
}

/* ============================================================
 * command_pool.hpp
 * ============================================================ */



namespace Vulkan
{
class CommandPool
{
public:
	CommandPool(VkDevice device, uint32_t queue_family_index);
	~CommandPool();

	CommandPool(CommandPool &&) noexcept;
	CommandPool &operator=(CommandPool &&) noexcept;
	CommandPool(const CommandPool &) = delete;
	void operator=(const CommandPool &) = delete;

	void begin();
	VkCommandBuffer request_command_buffer();
	void signal_submitted(VkCommandBuffer cmd);

private:
	VkDevice device = VK_NULL_HANDLE;
	VkCommandPool pool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> buffers;
#ifdef VULKAN_DEBUG
	std::unordered_set<VkCommandBuffer> in_flight;
#endif
	unsigned index = 0;
};
}

/* ============================================================
 * command_buffer.hpp
 * ============================================================ */



namespace Vulkan
{

enum CommandBufferDirtyBits
{
	COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT = 1 << 0,
	COMMAND_BUFFER_DIRTY_PIPELINE_BIT = 1 << 1,

	COMMAND_BUFFER_DIRTY_VIEWPORT_BIT = 1 << 2,
	COMMAND_BUFFER_DIRTY_SCISSOR_BIT = 1 << 3,

	COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT = 1 << 6,

	COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT = 1 << 7,

	COMMAND_BUFFER_DYNAMIC_BITS = COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | COMMAND_BUFFER_DIRTY_SCISSOR_BIT
};
using CommandBufferDirtyFlags = uint32_t;

#define COMPARE_OP_BITS 3
#define BLEND_FACTOR_BITS 5
#define BLEND_OP_BITS 3
#define CULL_MODE_BITS 2
union PipelineState {
	struct State
	{
		// Depth state.
		unsigned depth_write : 1;
		unsigned depth_test : 1;
		unsigned blend_enable : 1;

		unsigned cull_mode : CULL_MODE_BITS;

		unsigned depth_compare : COMPARE_OP_BITS;

		unsigned alpha_to_coverage : 1;
		unsigned alpha_to_one : 1;
		unsigned sample_shading : 1;

		unsigned src_color_blend : BLEND_FACTOR_BITS;
		unsigned dst_color_blend : BLEND_FACTOR_BITS;
		unsigned color_blend_op : BLEND_OP_BITS;
		unsigned src_alpha_blend : BLEND_FACTOR_BITS;
		unsigned dst_alpha_blend : BLEND_FACTOR_BITS;
		unsigned alpha_blend_op : BLEND_OP_BITS;
		unsigned topology : 4;

		unsigned spec_constant_mask : 8;
	} state;
	uint32_t words[4];
};

struct PotentialState
{
	float blend_constants[4];
	uint32_t spec_constants[VULKAN_NUM_SPEC_CONSTANTS];
};

struct VertexAttribState
{
	uint32_t binding;
	VkFormat format;
	uint32_t offset;
};

struct VertexBindingState
{
	VkBuffer buffers[VULKAN_NUM_VERTEX_BUFFERS];
	VkDeviceSize offsets[VULKAN_NUM_VERTEX_BUFFERS];
	VkDeviceSize strides[VULKAN_NUM_VERTEX_BUFFERS];
	VkVertexInputRate input_rates[VULKAN_NUM_VERTEX_BUFFERS];
};

struct ResourceBinding
{
	union {
		VkDescriptorBufferInfo buffer;
		struct
		{
			VkDescriptorImageInfo fp;
			VkDescriptorImageInfo integer;
		} image;
		VkBufferView buffer_view;
	};
};

struct ResourceBindings
{
	ResourceBinding bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint64_t cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint64_t secondary_cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint8_t push_constant_data[VULKAN_PUSH_CONSTANT_SIZE];
};

static_assert(VULKAN_NUM_DESCRIPTOR_SETS == 4, "Number of descriptor sets != 4.");

class CommandBuffer;
struct CommandBufferDeleter
{
	void operator()(CommandBuffer *cmd);
};

class Device;
class CommandBuffer : public Util::IntrusivePtrEnabled<CommandBuffer, CommandBufferDeleter, HandleCounter>
{
public:
	friend struct CommandBufferDeleter;
	enum class Type
	{
		Generic,
		AsyncGraphics,
		AsyncCompute,
		AsyncTransfer
	};

	~CommandBuffer();
	VkCommandBuffer get_command_buffer() const
	{
		return cmd;
	}

	void begin_region(const char *name, const float *color = nullptr);
	void end_region();

	Device &get_device()
	{
		return *device;
	}

	void clear_image(const Image &image, const VkClearValue &value);

	void copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
	                 VkDeviceSize size);
	void copy_buffer(const Buffer &dst, const Buffer &src);

	void copy_buffer_to_image(const Image &image, const Buffer &buffer, VkDeviceSize buffer_offset,
	                          const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
	                          unsigned slice_height, const VkImageSubresourceLayers &subresrouce);
	void copy_buffer_to_image(const Image &image, const Buffer &buffer, unsigned num_blits, const VkBufferImageCopy *blits);

	void copy_image_to_buffer(const Buffer &dst, const Image &src, VkDeviceSize buffer_offset, const VkOffset3D &offset,
	                          const VkExtent3D &extent, unsigned row_length, unsigned slice_height,
	                          const VkImageSubresourceLayers &subresrouce);

	void full_barrier();
	void pixel_barrier();
	void barrier(VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	             VkAccessFlags dst_access);

	void barrier(VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
	             unsigned barriers, const VkMemoryBarrier *globals,
	             unsigned buffer_barriers, const VkBufferMemoryBarrier *buffers,
	             unsigned image_barriers, const VkImageMemoryBarrier *images);

	void image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
	                   VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	                   VkAccessFlags dst_access);

	void blit_image(const Image &dst,
	                const Image &src,
	                const VkOffset3D &dst_offset0, const VkOffset3D &dst_extent,
	                const VkOffset3D &src_offset0, const VkOffset3D &src_extent, unsigned dst_level, unsigned src_level,
	                unsigned dst_base_layer = 0, uint32_t src_base_layer = 0, unsigned num_layers = 1,
	                VkFilter filter = VK_FILTER_LINEAR);

	// Prepares an image to have its mipmap generated.
	// Puts the top-level into TRANSFER_SRC_OPTIMAL, and all other levels are invalidated with an UNDEFINED -> TRANSFER_DST_OPTIMAL.
	void barrier_prepare_generate_mipmap(const Image &image, VkImageLayout base_level_layout, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
	                                     bool need_top_level_barrier = true);

	// The image must have been transitioned with barrier_prepare_generate_mipmap before calling this function.
	// After calling this function, the image will be entirely in TRANSFER_SRC_OPTIMAL layout.
	// Wait for TRANSFER stage to drain before transitioning away from TRANSFER_SRC_OPTIMAL.
	void generate_mipmap(const Image &image);

	void begin_render_pass(const RenderPassInfo &info, VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
	void end_render_pass();
	void set_program(Program &program);


	void set_buffer_view(unsigned set, unsigned binding, const BufferView &view);
	void set_input_attachments(unsigned set, unsigned start_binding);
	void set_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, StockSampler sampler);
	void set_storage_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_sampler(unsigned set, unsigned binding, const Sampler &sampler);
	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range);
	void push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range);

	void *allocate_constant_data(unsigned set, unsigned binding, VkDeviceSize size);

	template <typename T>
	T *allocate_typed_constant_data(unsigned set, unsigned binding, unsigned count)
	{
		return static_cast<T *>(allocate_constant_data(set, binding, count * sizeof(T)));
	}

	void *allocate_vertex_data(unsigned binding, VkDeviceSize size, VkDeviceSize stride,
	                           VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);

	void set_viewport(const VkViewport &viewport);
	const VkViewport &get_viewport() const;
	void set_scissor(const VkRect2D &rect);

	void set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset);
	void set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
	                        VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);

	void draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0,
	          uint32_t first_instance = 0);

	void dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);

	void set_opaque_state();
	void set_quad_state();

#define SET_STATIC_STATE(value)                               \
	do                                                        \
	{                                                         \
		if (static_state.state.value != value)                \
		{                                                     \
			static_state.state.value = value;                 \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)

#define SET_POTENTIALLY_STATIC_STATE(value)                   \
	do                                                        \
	{                                                         \
		if (potential_static_state.value != value)            \
		{                                                     \
			potential_static_state.value = value;             \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)

	inline void set_depth_test(bool depth_test, bool depth_write)
	{
		SET_STATIC_STATE(depth_test);
		SET_STATIC_STATE(depth_write);
	}

	inline void set_depth_compare(VkCompareOp depth_compare)
	{
		SET_STATIC_STATE(depth_compare);
	}

	inline void set_blend_enable(bool blend_enable)
	{
		SET_STATIC_STATE(blend_enable);
	}

	inline void set_blend_factors(VkBlendFactor src_color_blend, VkBlendFactor src_alpha_blend,
	                              VkBlendFactor dst_color_blend, VkBlendFactor dst_alpha_blend)
	{
		SET_STATIC_STATE(src_color_blend);
		SET_STATIC_STATE(dst_color_blend);
		SET_STATIC_STATE(src_alpha_blend);
		SET_STATIC_STATE(dst_alpha_blend);
	}

	inline void set_blend_factors(VkBlendFactor src_blend, VkBlendFactor dst_blend)
	{
		set_blend_factors(src_blend, src_blend, dst_blend, dst_blend);
	}

	inline void set_blend_op(VkBlendOp color_blend_op, VkBlendOp alpha_blend_op)
	{
		SET_STATIC_STATE(color_blend_op);
		SET_STATIC_STATE(alpha_blend_op);
	}

	inline void set_blend_op(VkBlendOp blend_op)
	{
		set_blend_op(blend_op, blend_op);
	}

	inline void set_primitive_topology(VkPrimitiveTopology topology)
	{
		SET_STATIC_STATE(topology);
	}

	inline void set_multisample_state(bool alpha_to_coverage, bool alpha_to_one = false, bool sample_shading = false)
	{
		SET_STATIC_STATE(alpha_to_coverage);
		SET_STATIC_STATE(alpha_to_one);
		SET_STATIC_STATE(sample_shading);
	}

	inline void set_cull_mode(VkCullModeFlags cull_mode)
	{
		SET_STATIC_STATE(cull_mode);
	}

	inline void set_blend_constants(const float blend_constants[4])
	{
		SET_POTENTIALLY_STATIC_STATE(blend_constants[0]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[1]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[2]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[3]);
	}

	inline void set_specialization_constant_mask(uint32_t spec_constant_mask)
	{
		VK_ASSERT((spec_constant_mask & ~((1u << VULKAN_NUM_SPEC_CONSTANTS) - 1u)) == 0u);
		SET_STATIC_STATE(spec_constant_mask);
	}

	template <typename T>
	inline void set_specialization_constant(unsigned index, const T &value)
	{
		VK_ASSERT(index < VULKAN_NUM_SPEC_CONSTANTS);
		static_assert(sizeof(value) == sizeof(uint32_t), "Spec constant data must be 32-bit.");
		if (memcmp(&potential_static_state.spec_constants[index], &value, sizeof(value)))
		{
			memcpy(&potential_static_state.spec_constants[index], &value, sizeof(value));
			if (static_state.state.spec_constant_mask & (1u << index))
				set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
		}
	}

	inline Type get_command_buffer_type() const
	{
		return type;
	}

	void end();

private:
	friend class Util::ObjectPool<CommandBuffer>;
	CommandBuffer(Device *device, VkCommandBuffer cmd, Type type);

	Device *device;
	VkCommandBuffer cmd;
	Type type;

	const Framebuffer *framebuffer = nullptr;
	const RenderPass *actual_render_pass = nullptr;
	const RenderPass *compatible_render_pass = nullptr;

	VertexAttribState attribs[VULKAN_NUM_VERTEX_ATTRIBS] = {};
	VertexBindingState vbo = {};
	ResourceBindings bindings;

	VkPipeline current_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout current_pipeline_layout = VK_NULL_HANDLE;
	PipelineLayout *current_layout = nullptr;
	Program *current_program = nullptr;
	unsigned current_subpass = 0;
	VkSubpassContents current_contents = VK_SUBPASS_CONTENTS_INLINE;

	VkViewport viewport = {};
	VkRect2D scissor = {};

	CommandBufferDirtyFlags dirty = ~0u;
	uint32_t dirty_sets = 0;
	uint32_t dirty_vbos = 0;
	uint32_t active_vbos = 0;
	bool is_compute = true;

	void set_dirty(CommandBufferDirtyFlags flags)
	{
		dirty |= flags;
	}

	CommandBufferDirtyFlags get_and_clear(CommandBufferDirtyFlags flags)
	{
		CommandBufferDirtyFlags mask = dirty & flags;
		dirty &= ~flags;
		return mask;
	}

	PipelineState static_state;
	PotentialState potential_static_state = {};
#ifndef _MSC_VER
	static_assert(sizeof(static_state.words) >= sizeof(static_state.state),
	              "Hashable pipeline state is not large enough!");
#endif

	void flush_render_state();
	VkPipeline build_graphics_pipeline(Util::Hash hash);
	VkPipeline build_compute_pipeline(Util::Hash hash);
	void flush_graphics_pipeline();
	void flush_compute_pipeline();
	void flush_descriptor_sets();
	void begin_graphics();
	void flush_descriptor_set(uint32_t set);
	void begin_compute();
	void begin_context();

	void flush_compute_state();

	BufferBlock vbo_block;
	BufferBlock ubo_block;

	void set_texture(unsigned set, unsigned binding, VkImageView float_view, VkImageView integer_view,
	                 VkImageLayout layout,
	                 uint64_t cookie);

	void init_viewport_scissor(const RenderPassInfo &info, const Framebuffer *framebuffer);
};


using CommandBufferHandle = Util::IntrusivePtr<CommandBuffer>;
}

/* ============================================================
 * device.hpp
 * ============================================================ */




namespace Vulkan
{
struct InitialImageBuffer
{
	BufferHandle buffer;
	// Bound matches the implicit invariant in TextureFormatLayout::mips[16]:
	// callers must pass <= 16 mip levels (no runtime check exists in
	// fill_mipinfo). build_buffer_image_copies is the sole writer of these
	// fields; it asserts num_blits <= 16 before writing.
	VkBufferImageCopy blits[16];
	unsigned num_blits = 0;
};

struct HandlePool
{
	VulkanObjectPool<Buffer> buffers;
	VulkanObjectPool<Image> images;
	VulkanObjectPool<ImageView> image_views;
	VulkanObjectPool<BufferView> buffer_views;
	VulkanObjectPool<Sampler> samplers;
	VulkanObjectPool<FenceHolder> fences;
	VulkanObjectPool<SemaphoreHolder> semaphores;
	VulkanObjectPool<CommandBuffer> command_buffers;
};

class Device
{
public:
	// Device-based objects which need to poke at internal data structures when their lifetimes end.
	// Don't want to expose a lot of internal guts to make this work.
	friend class SemaphoreHolder;
	friend struct SemaphoreHolderDeleter;
	friend class FenceHolder;
	friend struct FenceHolderDeleter;
	friend class Sampler;
	friend struct SamplerDeleter;
	friend class Buffer;
	friend struct BufferDeleter;
	friend class BufferView;
	friend struct BufferViewDeleter;
	friend class ImageView;
	friend struct ImageViewDeleter;
	friend class Image;
	friend struct ImageDeleter;
	friend class CommandBuffer;
	friend struct CommandBufferDeleter;
	friend class Program;
	friend class Cookie;
	friend class Framebuffer;
	friend class PipelineLayout;
	friend class FramebufferAllocator;

	Device();
	~Device();

	// No move-copy.
	void operator=(Device &&) = delete;
	Device(Device &&) = delete;

	// Only called by main thread, during setup phase.
	void set_context(const Context &context);
	void init_frame_contexts(unsigned count);

	// Frame-pushing interface.
	void next_frame_context();
	void wait_idle();

	// Set names for objects for debuggers and profilers.
	void set_name(const Buffer &buffer, const char *name);
	void set_name(const Image &image, const char *name);

	// Submission interface, may be called from any thread at any time.
	void flush_frame();
	CommandBufferHandle request_command_buffer(CommandBuffer::Type type = CommandBuffer::Type::Generic);
	void submit(CommandBufferHandle &cmd, Fence *fence = nullptr,
	            unsigned semaphore_count = 0, Semaphore *semaphore = nullptr);
	void add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush);
	CommandBuffer::Type get_physical_queue_type(CommandBuffer::Type queue_type) const;

	// Request shaders and programs. These objects are owned by the Device.
	Shader *request_shader(const uint32_t *code, size_t size);
	Program *request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
	                         size_t fragment_size);
	Program *request_program(const uint32_t *compute_data, size_t compute_size);
	Program *request_program(Shader *vertex, Shader *fragment);
	Program *request_program(Shader *compute);

	// Map and unmap buffer objects.
	void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access);
	void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access);

	// Create buffers and images.
	BufferHandle create_buffer(const BufferCreateInfo &info, const void *initial = nullptr);
	ImageHandle create_image(const ImageCreateInfo &info, const ImageInitialData *initial = nullptr);
	ImageHandle create_image_from_staging_buffer(const ImageCreateInfo &info, const InitialImageBuffer *buffer);

	// Create staging buffers for images.
	InitialImageBuffer create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial);

	// Create image view, buffer views and samplers.
	ImageViewHandle create_image_view(const ImageViewCreateInfo &view_info);
	BufferViewHandle create_buffer_view(const BufferViewCreateInfo &view_info);

	// Render pass helpers.
	bool image_format_is_supported(VkFormat format, VkFormatFeatureFlags required, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) const;
	bool get_image_format_properties(VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
	                                 VkImageFormatProperties *properties);

	VkFormat get_default_depth_format() const;
	ImageView &get_transient_attachment(unsigned width, unsigned height, VkFormat format,
	                                    unsigned index = 0, unsigned samples = 1, unsigned layers = 1);

	VkDevice get_device()
	{
		return device;
	}

	const VkPhysicalDeviceProperties &get_gpu_properties() const
	{
		return gpu_props;
	}

	const Sampler &get_stock_sampler(StockSampler sampler) const;


	const ImplementationWorkarounds &get_workarounds() const
	{
		return workarounds;
	}

	const DeviceFeatures &get_device_features() const
	{
		return ext;
	}

private:
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue graphics_queue = VK_NULL_HANDLE;
	VkQueue compute_queue = VK_NULL_HANDLE;
	VkQueue transfer_queue = VK_NULL_HANDLE;

	uint64_t cookie = 0;

	uint64_t allocate_cookie();
	void bake_program(Program &program);

	void request_vertex_block(BufferBlock &block, VkDeviceSize size);
	void request_uniform_block(BufferBlock &block, VkDeviceSize size);

	PipelineLayout *request_pipeline_layout(const CombinedResourceLayout &layout);
	DescriptorSetAllocator *request_descriptor_set_allocator(const DescriptorSetLayout &layout, const uint32_t *stages_for_sets);
	const Framebuffer &request_framebuffer(const RenderPassInfo &info);
	const RenderPass &request_render_pass(const RenderPassInfo &info, bool compatible);

	VkPhysicalDeviceMemoryProperties mem_props;
	VkPhysicalDeviceProperties gpu_props;

	DeviceFeatures ext;
	void init_stock_samplers();

	// Make sure this is deleted last.
	HandlePool handle_pool;

	struct Managers
	{
		DeviceAllocator memory;
		FenceManager fence;
		SemaphoreManager semaphore;
		BufferPool vbo, ubo;
	};
	Managers managers;

	struct
	{
		unsigned counter = 0;
	} lock;

	struct PerFrame
	{
		PerFrame(Device *device);
		~PerFrame();
		void operator=(const PerFrame &) = delete;
		PerFrame(const PerFrame &) = delete;

		void begin();

		VkDevice device;
		Managers &managers;
		CommandPool graphics_cmd_pool;
		CommandPool compute_cmd_pool;
		CommandPool transfer_cmd_pool;

		std::vector<BufferBlock> vbo_blocks;
		std::vector<BufferBlock> ubo_blocks;

		std::vector<VkFence> wait_fences;
		std::vector<VkFence> recycle_fences;
		std::vector<DeviceAllocation> allocations;
		std::vector<VkFramebuffer> destroyed_framebuffers;
		std::vector<VkSampler> destroyed_samplers;
		std::vector<VkPipeline> destroyed_pipelines;
		std::vector<VkImageView> destroyed_image_views;
		std::vector<VkBufferView> destroyed_buffer_views;
		std::vector<VkImage> destroyed_images;
		std::vector<VkBuffer> destroyed_buffers;
		std::vector<CommandBufferHandle> graphics_submissions;
		std::vector<CommandBufferHandle> compute_submissions;
		std::vector<CommandBufferHandle> transfer_submissions;
		std::vector<VkSemaphore> recycled_semaphores;
		std::vector<VkSemaphore> destroyed_semaphores;
	};
	// The per frame structure must be destroyed after
	// the hashmap data structures below, so it must be declared before.
	std::vector<std::unique_ptr<PerFrame>> per_frame;

	struct QueueData
	{
		std::vector<Semaphore> wait_semaphores;
		std::vector<VkPipelineStageFlags> wait_stages;
		bool need_fence = false;
	} graphics, compute, transfer;

	// Pending buffers which need to be copied from CPU to GPU before submitting graphics or compute work.
	struct
	{
		std::vector<BufferBlock> vbo;
		std::vector<BufferBlock> ubo;
	} dma;

	void submit_queue(CommandBuffer::Type type, VkFence *fence,
	                  unsigned semaphore_count = 0,
	                  Semaphore *semaphore = nullptr);

	PerFrame &frame()
	{
		VK_ASSERT(frame_context_index < per_frame.size());
		VK_ASSERT(per_frame[frame_context_index]);
		return *per_frame[frame_context_index];
	}

	const PerFrame &frame() const
	{
		VK_ASSERT(frame_context_index < per_frame.size());
		VK_ASSERT(per_frame[frame_context_index]);
		return *per_frame[frame_context_index];
	}

	unsigned frame_context_index = 0;
	uint32_t graphics_queue_family_index = 0;
	uint32_t compute_queue_family_index = 0;
	uint32_t transfer_queue_family_index = 0;

	uint32_t find_memory_type(BufferDomain domain, uint32_t mask);
	uint32_t find_memory_type(ImageDomain domain, uint32_t mask);
	bool memory_type_is_host_visible(uint32_t type) const;

	SamplerHandle samplers[static_cast<unsigned>(StockSampler::Count)];

	VulkanCache<PipelineLayout> pipeline_layouts;
	VulkanCache<DescriptorSetAllocator> descriptor_set_allocators;
	VulkanCache<RenderPass> render_passes;
	VulkanCache<Shader> shaders;
	VulkanCache<Program> programs;

	FramebufferAllocator framebuffer_allocator;
	AttachmentAllocator transient_allocator;

	SamplerHandle create_sampler(const SamplerCreateInfo &info, StockSampler sampler);

	CommandPool &get_command_pool(CommandBuffer::Type type);
	QueueData &get_queue_data(CommandBuffer::Type type);
	std::vector<CommandBufferHandle> &get_queue_submissions(CommandBuffer::Type type);
	void clear_wait_semaphores();
	void submit_staging(CommandBufferHandle &cmd, VkBufferUsageFlags usage, bool flush);

	void flush_frame(CommandBuffer::Type type);
	void sync_buffer_blocks();
	void submit_empty_inner(CommandBuffer::Type type, VkFence *fence,
	                        unsigned semaphore_count,
	                        Semaphore *semaphore);

	void reset_fence(VkFence fence);

	void destroy_buffer_nolock(VkBuffer buffer);
	void destroy_image_nolock(VkImage image);
	void destroy_image_view_nolock(VkImageView view);
	void destroy_buffer_view_nolock(VkBufferView view);
	void destroy_pipeline_nolock(VkPipeline pipeline);
	void destroy_sampler_nolock(VkSampler sampler);
	void destroy_framebuffer_nolock(VkFramebuffer framebuffer);
	void destroy_semaphore_nolock(VkSemaphore semaphore);
	void recycle_semaphore_nolock(VkSemaphore semaphore);
	void free_memory_nolock(const DeviceAllocation &alloc);

	void flush_frame_nolock();
	CommandBufferHandle request_command_buffer_nolock(CommandBuffer::Type type = CommandBuffer::Type::Generic);
	void submit_nolock(CommandBufferHandle cmd, Fence *fence,
	                   unsigned semaphore_count, Semaphore *semaphore);
	void add_wait_semaphore_nolock(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages,
	                               bool flush);

	void request_vertex_block_nolock(BufferBlock &block, VkDeviceSize size);
	void request_uniform_block_nolock(BufferBlock &block, VkDeviceSize size);

	void add_frame_counter_nolock();
	void decrement_frame_counter_nolock();
	void wait_idle_nolock();
	void end_frame_nolock();

	ImplementationWorkarounds workarounds;
	void init_workarounds();
};
}

/* ============================================================
 * atlas.hpp
 * ============================================================ */


namespace PSX
{
static const unsigned FB_WIDTH = 1024;
static const unsigned FB_HEIGHT = 512;
static const unsigned BLOCK_WIDTH = 8;
static const unsigned BLOCK_HEIGHT = 8;
static const unsigned NUM_BLOCKS_X = FB_WIDTH / BLOCK_WIDTH;
static const unsigned NUM_BLOCKS_Y = FB_HEIGHT / BLOCK_HEIGHT;

enum class Domain : unsigned
{
	Unscaled,
	Scaled
};

enum class Stage : unsigned
{
	Compute,
	Transfer,
	Fragment,
	FragmentTexture
};

enum class TextureMode
{
	None,
	Palette4bpp,
	Palette8bpp,
	ABGR1555
};

struct Rect
{
	unsigned x = 0;
	unsigned y = 0;
	unsigned width = 0;
	unsigned height = 0;

	Rect() = default;
	Rect(unsigned x, unsigned y, unsigned width, unsigned height)
	    : x(x)
	    , y(y)
	    , width(width)
	    , height(height)
	{
	}

	inline bool operator==(const Rect &rect) const
	{
		return x == rect.x && y == rect.y && width == rect.width && height == rect.height;
	}

	inline bool operator!=(const Rect &rect) const
	{
		return x != rect.x || y != rect.y || width != rect.width || height != rect.height;
	}

	inline bool contains(const Rect &rect) const
	{
		return x <= rect.x && y <= rect.y && (x + width) >= (rect.x + rect.width) &&
		       (y + height) >= (rect.y + rect.height);
	}

	inline bool intersects(const Rect &rect) const
	{
		unsigned x_end_self = x + width;
		unsigned x_end_other = rect.x + rect.width;
		unsigned y_end_self = y + height;
		unsigned y_end_other = rect.y + rect.height;
		unsigned xend = (x_end_self < x_end_other) ? x_end_self : x_end_other;
		unsigned xbegin = (x > rect.x) ? x : rect.x;
		unsigned yend = (y_end_self < y_end_other) ? y_end_self : y_end_other;
		unsigned ybegin = (y > rect.y) ? y : rect.y;
		return xbegin < xend && ybegin < yend;
	}

	inline Rect scissor(const Rect &rect) const
	{
		unsigned x_end_self = x + width;
		unsigned x_end_other = rect.x + rect.width;
		unsigned y_end_self = y + height;
		unsigned y_end_other = rect.y + rect.height;
		unsigned x0 = (x > rect.x) ? x : rect.x;
		unsigned y0 = (y > rect.y) ? y : rect.y;
		unsigned x1 = (x_end_self < x_end_other) ? x_end_self : x_end_other;
		unsigned y1 = (y_end_self < y_end_other) ? y_end_self : y_end_other;
		unsigned width = (x1 > x0) ? (x1 - x0) : 0u;
		unsigned height = (y1 > y0) ? (y1 - y0) : 0u;
		return { x0, y0, width, height };
	}

	inline void extend_bounding_box(const Rect &rect)
	{
		unsigned x_end_self = x + width;
		unsigned x_end_other = rect.x + rect.width;
		unsigned y_end_self = y + height;
		unsigned y_end_other = rect.y + rect.height;
		unsigned x0 = (x < rect.x) ? x : rect.x;
		unsigned y0 = (y < rect.y) ? y : rect.y;
		unsigned x1 = (x_end_self > x_end_other) ? x_end_self : x_end_other;
		unsigned y1 = (y_end_self > y_end_other) ? y_end_self : y_end_other;
		x = x0;
		y = y0;
		width = x1 - x0;
		height = y1 - y0;
	}
};

enum StatusFlag
{
	STATUS_FB_ONLY = 0,
	STATUS_FB_PREFER = 1,
	STATUS_SFB_ONLY = 2,
	STATUS_SFB_PREFER = 3,
	STATUS_OWNERSHIP_MASK = 3,

	STATUS_COMPUTE_FB_READ = 1 << 2,
	STATUS_COMPUTE_FB_WRITE = 1 << 3,
	STATUS_COMPUTE_SFB_READ = 1 << 4,
	STATUS_COMPUTE_SFB_WRITE = 1 << 5,

	STATUS_TRANSFER_FB_READ = 1 << 6,
	STATUS_TRANSFER_SFB_READ = 1 << 7,
	STATUS_TRANSFER_FB_WRITE = 1 << 8,
	STATUS_TRANSFER_SFB_WRITE = 1 << 9,

	STATUS_FRAGMENT_SFB_READ = 1 << 10,
	STATUS_FRAGMENT_SFB_WRITE = 1 << 11,
	STATUS_FRAGMENT_FB_READ = 1 << 12,
	STATUS_FRAGMENT_FB_WRITE = 1 << 13,

	// A special stage to allow fragment to detect when it's causing a feedback loop with texture read -> fragment write.
	// This flag is added in combination with FRAGMENT_FB_READ.
	STATUS_TEXTURE_READ = 1 << 14,

	// For determining if a texture read is from a loaded image or previous rendered content
	STATUS_TEXTURE_RENDERED = 1 << 15,

	STATUS_FB_READ = STATUS_COMPUTE_FB_READ | STATUS_TRANSFER_FB_READ | STATUS_FRAGMENT_FB_READ,
	STATUS_FB_WRITE = STATUS_COMPUTE_FB_WRITE | STATUS_TRANSFER_FB_WRITE | STATUS_FRAGMENT_FB_WRITE,
	STATUS_SFB_READ = STATUS_COMPUTE_SFB_READ | STATUS_TRANSFER_SFB_READ | STATUS_FRAGMENT_SFB_READ,
	STATUS_SFB_WRITE = STATUS_COMPUTE_SFB_WRITE | STATUS_TRANSFER_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE,
	STATUS_FRAGMENT =
	    STATUS_FRAGMENT_FB_READ | STATUS_FRAGMENT_FB_WRITE | STATUS_FRAGMENT_SFB_READ | STATUS_FRAGMENT_SFB_WRITE,
	STATUS_ALL = STATUS_FB_READ | STATUS_FB_WRITE | STATUS_SFB_READ | STATUS_SFB_WRITE
};
using StatusFlags = uint16_t;

class Renderer;

class FBAtlas
{
public:
	FBAtlas();

	void set_hazard_listener(Renderer *hazard)
	{
		listener = hazard;
	}

	void read_compute(Domain domain, const Rect &rect);
	void write_compute(Domain domain, const Rect &rect);
	void read_transfer(Domain domain, const Rect &rect);
	void write_transfer(Domain domain, const Rect &rect);
	void read_fragment(Domain domain, const Rect &rect);
	Domain blit_vram(const Rect &dst, const Rect &src);
	void load_image(const Rect &rect);
	bool texture_rendered(const Rect &rect);

	void write_fragment(Domain domain, const Rect &rect);
	void clear_rect(const Rect &rect, uint32_t color);
	void set_draw_rect(const Rect &rect);
	void set_texture_window(const Rect &rect);

	TextureMode set_texture_mode(TextureMode mode)
	{
		TextureMode old = renderpass.texture_mode;
		renderpass.texture_mode = mode;
		return old;
	}

	void set_texture_offset(unsigned x, unsigned y)
	{
		renderpass.texture_offset_x = x;
		renderpass.texture_offset_y = y;
	}

	void set_palette_offset(unsigned x, unsigned y)
	{
		renderpass.palette_offset_x = x;
		renderpass.palette_offset_y = y;
	}

	void pipeline_barrier(StatusFlags domains);
	void notify_external_barrier(StatusFlags domains);
	void flush_render_pass();

private:
	StatusFlags fb_info[NUM_BLOCKS_X * NUM_BLOCKS_Y];
	Renderer *listener = nullptr;

	void read_domain(Domain domain, Stage stage, const Rect &rect);
	bool write_domain(Domain domain, Stage stage, const Rect &rect);
	void sync_domain(Domain domain, const Rect &rect);
	void read_texture(Domain domain);
	Domain find_suitable_domain(const Rect &rect);

	struct
	{
		Rect rect;
		Rect scissor;
		Rect texture_window;
		unsigned texture_offset_x = 0, texture_offset_y = 0;
		unsigned palette_offset_x = 0, palette_offset_y = 0;
		TextureMode texture_mode = TextureMode::None;
		bool inside = false;
	} renderpass;

	void extend_render_pass(const Rect &rect, bool scissor);

	StatusFlags &info(unsigned block_x, unsigned block_y)
	{
		block_x &= NUM_BLOCKS_X - 1;
		block_y &= NUM_BLOCKS_Y - 1;
		return fb_info[NUM_BLOCKS_X * block_y + block_x];
	}

	const StatusFlags &info(unsigned block_x, unsigned block_y) const
	{
		block_x &= NUM_BLOCKS_X - 1;
		block_y &= NUM_BLOCKS_Y - 1;
		return fb_info[NUM_BLOCKS_X * block_y + block_x];
	}

	void discard_render_pass();
	bool inside_render_pass(const Rect &rect);
};
}

/* ============================================================
 * config_parser.h
 * ============================================================ */


namespace PSX {
    class RectMatch {
    public:
        RectMatch(int x, int y, int w, int h): x(x), y(y), w(w), h(h) {}
        bool matches(Rect r) {
            return (x == -1 || x== r.x) && (y == -1 || y == r.y) && (w == -1 || w == r.width) && (h == -1 || h == r.height);
        }
        int x = -1;
        int y = -1;
        int w = -1;
        int h = -1;
    private:
    };
};

std::vector<PSX::RectMatch> parse_config_file(const char *path);

/* ============================================================
 * texture_tracker.hpp
 * ============================================================ */


extern retro_log_printf_t log_cb;

// #define VERBOSE_TEXTURE_TRACKING

#define TT_LOG(...) log_cb(__VA_ARGS__)
#ifdef VERBOSE_TEXTURE_TRACKING
#define TT_LOG_VERBOSE(...) TT_LOG(__VA_ARGS__)
#else
/* No-op variant must still consume its arguments at the syntactic
 * level, otherwise locals only used in the verbose branch trip
 * -Wunused-but-set-variable.  The "(void)0," prefix lets sizeof
 * accept a 1-arg invocation through the comma operator; sizeof
 * itself is unevaluated, so each arg is read by the type system
 * without generating runtime code. */
#define TT_LOG_VERBOSE(...) ((void)sizeof(((void)0, __VA_ARGS__)))
#endif

namespace PSX {

struct HdTextureId {
    uint32_t hash;
    uint32_t palette_hash;

    bool operator>(const HdTextureId &other) const
    {
        if (hash != other.hash)
			return hash > other.hash;
		return palette_hash > other.palette_hash;
    }
    bool operator<(const HdTextureId &other) const
    {
        if (hash != other.hash)
			return hash < other.hash;
		return palette_hash < other.palette_hash;
    }
};

typedef int RectIndex; // I wanted a newtype but it's too much work in C++, so maybe TODO that later
struct HdTextureHandle {
    RectIndex index;
    uint32_t palette_hash;
    bool fused;

    bool operator==(const HdTextureHandle &other) const
    {
        return index == other.index && palette_hash == other.palette_hash && fused == other.fused;
    }

    bool operator!=(const HdTextureHandle &other) const
    {
        return !(*this == other);
    }

    bool operator>(const HdTextureHandle &other) const
    {
        if (index != other.index)
			return index > other.index;
		if (palette_hash != other.palette_hash)
            return palette_hash > other.palette_hash;
        return fused > other.fused;
    }

    static HdTextureHandle make(RectIndex index, uint32_t palette_hash) {
        return HdTextureHandle(index, palette_hash, false);
    }
    static HdTextureHandle make_fused(RectIndex index) {
        return HdTextureHandle(index, 0, true);
    }
    static HdTextureHandle make_none() {
        return HdTextureHandle::make(-1, 0);
    }

private:
    HdTextureHandle(RectIndex index, uint32_t palette_hash, bool fused)
    : index(index), palette_hash(palette_hash), fused(fused)
    {

    }
};

struct SRect {
    int x;
    int y;
    int width;
    int height;
    // Default-constructed SRect zero-initializes all fields. The result is in
    // an "invalid" state by the 4-arg constructor's invariant (width == 0 and
    // height == 0 would fail its width > 0 / height > 0 check) and is intended
    // only as a placeholder — array slot or struct field — to be overwritten
    // before being read. The 4-arg constructor below is the validated path;
    // use it for any SRect that is meant to be immediately usable.
    SRect() : x(0), y(0), width(0), height(0) {}
    SRect(int x, int y, int width, int height):
    x(x), y(y), width(width), height(height) {
        if (width <= 0 || height <= 0) {
            printf("Illegally sized SRect: %d, %d\n", width, height);
            exit(1);
        }
    }
    inline int left() {
        return x;
    }
    inline int right() {
        return x + width;
    }
    inline int top() {
        return y;
    }
    inline int bottom() {
        return y + height;
    }

	inline bool operator==(const SRect &other) const
	{
		return x == other.x && y == other.y && width == other.width && height == other.height;
	}
    inline bool operator!=(const SRect &other) const
    {
        return !(*this == other);
    }
};

struct HdTexture {
    SRect vram_rect;
    SRect texel_rect; // hd texels
    Vulkan::ImageHandle texture;
};

struct DumpedMode {
    TextureMode mode;
    uint32_t palette_hash;

	inline bool operator==(const DumpedMode &other) const
	{
		return mode == other.mode && palette_hash == other.palette_hash;
	}
};

struct UsedMode {
    TextureMode mode;
    unsigned int palette_offset_x;
    unsigned int palette_offset_y;

	inline bool operator==(const UsedMode &other) const
	{
		return mode == other.mode && palette_offset_x == other.palette_offset_x && palette_offset_y == other.palette_offset_y;
	}
};

struct HdImageHandle {
    Vulkan::ImageHandle image;
    int alpha_flags;
};
struct TextureUpload {
    std::vector<uint16_t> image;
    bool dumpable;
    int width;
    int height;
    uint32_t hash;
    std::vector<DumpedMode> dumped_modes;
	std::map<uint32_t, HdImageHandle> textures; // palette hash -> imagehandle
};

struct LoadedImage {
    std::vector<uint8_t> owned_data; // RGBA format
    int width;
    int height;
};

class Renderer;

enum class IORequestKind {
    Load,
    Dump,
};

struct IORequest {
    IORequestKind kind;
    // Load payload (valid when kind == Load):
    uint32_t hash;
    uint32_t palette_hash;
    // Dump payload (valid when kind == Dump):
    std::string path;
    int width;
    int height;
    std::vector<uint8_t> bytes;
};

const int ALPHA_FLAG_OPAQUE = 1;
const int ALPHA_FLAG_SEMI_TRANSPARENT = 2;
const int ALPHA_FLAG_TRANSPARENT = 4;

struct IOResponse {
    uint32_t hash;
    uint32_t palette_hash;
    int alpha_flags;
    std::vector<LoadedImage> levels;
};

class IOChannel {
public:
    IOChannel();
    ~IOChannel();
    slock_t *lock;
    scond_t *cond;
    std::vector<IORequest> requests;
    std::vector<IOResponse> responses;
    bool done = false;
private:
};

class IOThread {
public:
    IOThread();
    ~IOThread();
    std::shared_ptr<IOChannel> channel;
private:
};

struct Palette {
    uint16_t *data;
    uint32_t hash;
};

struct CachedPaletteHash {
    Rect rect;
    uint32_t hash;
};

//============
// RectTracker
struct TextureRect {
    std::shared_ptr<TextureUpload> upload;
    // the offset into the original upload rect (offset_x + vram_rect.width <= upload->width)
    int offset_x;
    int offset_y;
    SRect vram_rect;
    TextureRect(std::shared_ptr<TextureUpload> upload, int offset_x, int offset_y, SRect vram_rect): 
    upload(upload), offset_x(offset_x), offset_y(offset_y), vram_rect(vram_rect)
    {
    }

    // in vram size (not hd), local to the uploaded data, different hd textures for different palettes could have different sizes anyway
    SRect texture_subrect() const {
        return SRect(offset_x, offset_y, vram_rect.width, vram_rect.height);
    }

	inline bool operator==(const TextureRect &other) const
	{
		return upload.get() == other.upload.get() && offset_x == other.offset_x && offset_y == other.offset_y && vram_rect == other.vram_rect;
	}
    inline bool operator!=(const TextureRect &other) const
    {
        return !(*this == other);
    }
};

// TODO: better name
struct EnduringTextureRect {
    TextureRect texture_rect;
    bool alive;
};

const int LOOKUP_GRID_COLUMNS = 16;
const int LOOKUP_GRID_ROWS = 2;
const int LOOKUP_CELL_WIDTH = 64;
const int LOOKUP_CELL_HEIGHT = 256;

class LookupGrid {
public:
    void insert(SRect r, RectIndex index);
    void get(SRect r, std::unordered_set<RectIndex> &results);
    void clear();
private:
    struct LookupEntry {
        SRect rect;
        RectIndex index;
    };
    std::vector<LookupEntry> cells[LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS]; // Each cell is a psx texture page, 64x256
};

class RectTracker {
public:
    void place(TextureRect texture);
    void upload(SRect rect, std::shared_ptr<TextureUpload> upload);
    void blit(SRect dst, SRect src);
    void clear(SRect rect);
    void releaseDeadHandles();
    std::vector<EnduringTextureRect> textures;
    std::unordered_set<RectIndex>& overlapping(Rect rect, std::unordered_set<RectIndex>& results);

    /**
     * This pointer will be valid until the next upload/blit/clear/endFrame, so use it immediately and don't try anything funny.
     * Returns nullptr when index is out of range
    **/
    TextureRect* get_index(RectIndex index);

    /** Returns nullptr if no texture with the given hash can be found */
    std::shared_ptr<TextureUpload> find_upload(uint32_t hash);
private:
    LookupGrid lookup_grid;
    bool lookup_grid_dirty = false;

    void clear_rect(SRect &rect);
    void rebuild_lookup_grid();
};
// RectTracker
//============

struct FusionRects {
    std::vector<TextureRect> rects;
    Rect vram_rect;
    unsigned int scaleX = 0;
    unsigned int scaleY = 0;

    bool operator==(const FusionRects &other) const {
        return vram_rect == other.vram_rect && scaleX == other.scaleX && scaleY == other.scaleY && rects == other.rects;
    }

    bool operator!=(const FusionRects &other) const {
        return !(*this == other);
    }
};

struct FusedPage {
    Vulkan::ImageHandle texture;

    uint32_t palette;
    Rect full_page_rect;

    bool dirty = false;
    bool dead = false;

    FusionRects fusion;
};

class FusedPages {
public:
    HdTextureHandle get_or_make(Rect page_rect, uint32_t palette, RectTracker &tracker, Renderer *uploader);
    HdTexture get_from_handle(HdTextureHandle handle, Vulkan::ImageHandle &default_hd_texture);
    void mark_dirty(Rect rect); // For blit dst, upload, and hd texture load
    void mark_dead(Rect rect); // For clear
    void rebuild_dirty(RectTracker &tracker, Renderer *uploader);
    void remove_dead();

    void dbg_print_info();
private:
    std::vector<FusedPage> pages;
};

struct RestorableRect {
    Rect rect;
    uint32_t hash;
    std::vector<TextureRect> to_restore;
};

class DbgHotkey {
public:
    DbgHotkey(retro_key key): key(key) {}
    bool query();
    retro_key key;
private:
    bool was_key_down = false;
};

struct CacheEntry {
    Rect rect;
    HdTextureHandle handle;
};

class HandleLRUCache {
public:
    HandleLRUCache(int max_size): max_size(max_size) { entries.reserve(max_size); }
    std::pair<HdTextureHandle, bool> get(Rect rect, uint32_t palette_hash);
    void insert(Rect rect, uint32_t palette_hash, HdTextureHandle handle);
    void clear();
    int64_t dbg_hits;
    int64_t dbg_misses;
private:
    int max_size;
    std::vector<CacheEntry> entries;
};

//========================================
// Save State
struct TextureRectSaveState {
    uint32_t upload_hash;
    int offset_x;
    int offset_y;
    SRect vram_rect;
};

struct RestorableRectSaveState {
    Rect rect;
    uint32_t hash;
    std::vector<TextureRectSaveState> to_restore;
};

struct TextureTrackerSaveState {
    std::vector<TextureRectSaveState> rects;
    std::vector<RestorableRectSaveState> restorable;
    std::map<uint32_t, TextureUpload> uploads;
};
// End of Save State
//========================================

class TextureTracker {
public:
    TextureTracker();

    TextureTrackerSaveState save_state();
    void load_state(const TextureTrackerSaveState &state);

    // Put texture in highres vram
    void upload(Rect rect, uint16_t *vram);
    void blit(Rect dst, Rect src);
    // Clear highres vram to fallback to lowres
    void clearRegion(Rect rect);
    // Monitor VRAM readback
    void notifyReadback(Rect rect, uint16_t *vram);
    uint32_t dbgHashVram(Rect rect, uint16_t *vram);

    HdTextureHandle get_hd_texture_index(Rect rect, UsedMode &mode, unsigned int page_x, unsigned int page_y, bool &fastpath_capable, bool &cache_hit);
    HdTexture get_hd_texture(HdTextureHandle index);
    void endFrame();
    void on_queues_reset();

	void set_texture_uploader(Renderer *t);

    bool dump_enabled = false;
    bool hd_textures_enabled = false;
private:
    IOThread iothread;
    Renderer *uploader;

    Vulkan::ImageHandle default_hd_texture;

    Palette get_palette(Rect palette_rect);
    uint32_t get_palette_hash(Rect palette_rect);

    std::vector<RectMatch> dump_ignore;

    std::set<HdTextureId> known_files;
    std::vector<CachedPaletteHash> cached_palette_hashes;
    std::vector<RestorableRect> restorable_rects;
    FusedPages fused_pages;
    uint64_t frame = 0;

    RectTracker tracker;
    HandleLRUCache handle_cache = 4;
    void dump_texture(std::shared_ptr<TextureUpload> &upload, UsedMode &mode, DumpedMode dump_mode);
    
    DbgHotkey frame_dump_key = RETROK_LEFTBRACKET; // disgusting
    std::ofstream *frame_dump = nullptr;
    bool frame_dump_need_comma = false;

    DbgHotkey hd_toggle_key = RETROK_RIGHTBRACKET;

    void load_hd_texture(uint32_t hash);

    DbgHotkey reload_key = RETROK_QUOTE;
    void reload_textures_from_disk();

    DbgHotkey fastpath_key = RETROK_SEMICOLON;
    bool fastpath_enabled = true;

    void dump_image(TextureUpload &upload, UsedMode &mode);

    void clear_palette_cache(Rect rect);

    /** Returns nullptr if no texture with the given hash can be found */
    std::shared_ptr<TextureUpload> find_upload(uint32_t hash);
};

}

/* ============================================================
 * existing renderer.hpp content
 * ============================================================ */



namespace PSX
{

struct Vertex
{
	float x, y, w;
	uint32_t color;
	uint16_t u, v;
};

struct TextureWindow
{
	uint8_t mask_x, mask_y, or_x, or_y;
};

struct UVRect
{
	uint16_t min_u, min_v, max_u, max_v;
};

enum class SemiTransparentMode
{
	None,
	Average,
	Add,
	Sub,
	AddQuarter
};

enum class PrimitiveType
{
	Sprite,
	Polygon,
	May_Be_2D_Polygon
};

class Renderer
{
public:
	enum class ScanoutMode
	{
		// Use extra precision bits.
		ABGR1555_555,
		// Use extra precision bits to dither down to a native ABGR1555 image.
		// The dither happens in the wrong place, but should be "good" enough to feel authentic.
		ABGR1555_Dither,
		// MDEC
		BGR24
	};

	enum class ScanoutFilter
	{
		None,
		SSAA,
		MDEC_YUV
	};

	enum class WidthMode
	{
		WIDTH_MODE_256 = 0,
		WIDTH_MODE_320 = 1,
		WIDTH_MODE_512 = 2,
		WIDTH_MODE_640 = 3,
		WIDTH_MODE_368 = 4
	};

	struct DisplayRect
	{
		// Unlike Rect, the x-y coordinates for a DisplayRect can be negative
		int x = 0;
		int y = 0;
		unsigned width = 0;
		unsigned height = 0;

		DisplayRect() = default;
		DisplayRect(int x, int y, unsigned width, unsigned height)
		    : x(x)
		    , y(y)
		    , width(width)
		    , height(height)
		{
		}
	};

	struct RenderState
	{
		//Rect display_mode;
		Rect display_fb_rect;
		TextureWindow texture_window;
		Rect cached_window_rect;
		Rect draw_rect;
		int draw_offset_x = 0;
		int draw_offset_y = 0;
		unsigned palette_offset_x = 0;
		unsigned palette_offset_y = 0;
		unsigned texture_offset_x = 0;
		unsigned texture_offset_y = 0;

		int vert_start = 0x10;
		int vert_end = 0x100;
		int horiz_start = 0x200;
		int horiz_end = 0xC00;

		bool is_pal = false;
		bool is_480i = false;
		WidthMode width_mode = WidthMode::WIDTH_MODE_320;
		int crop_overscan = 0;
		unsigned image_crop = 0;

		// Experimental horizontal offset feature
		int offset_cycles = 0;

		int slstart = 0;
		int slend = 239;

		int slstart_pal = 0;
		int slend_pal = 287;

		unsigned display_fb_xstart = 0;
		unsigned display_fb_ystart = 0;

		TextureMode texture_mode = TextureMode::None;
		SemiTransparentMode semi_transparent = SemiTransparentMode::None;
		PrimitiveType primitive_type = PrimitiveType::Polygon;
		ScanoutMode scanout_mode = ScanoutMode::ABGR1555_555;
		ScanoutFilter scanout_filter = ScanoutFilter::None;
		ScanoutFilter scanout_mdec_filter = ScanoutFilter::None;
		bool dither_native_resolution = false;
		bool force_mask_bit = false;
		bool texture_color_modulate = false;
		bool mask_test = false;
		bool display_on = false;
		bool adaptive_smoothing = true;

		UVRect UVLimits;
	};

	struct SaveState
	{
		std::vector<uint32_t> vram;
		RenderState state;
		TextureTrackerSaveState tracker_state;
	};

	Renderer(Vulkan::Device &device, unsigned scaling, unsigned msaa, const SaveState *save_state);
	~Renderer();

	void set_track_textures(bool enable);
	void set_dump_textures(bool enable);
	void set_replace_textures(bool enable);

	void set_adaptive_smoothing(bool enable)
	{
		render_state.adaptive_smoothing = enable;
	}

	void set_draw_rect(const Rect &rect);
	inline void set_draw_offset(int x, int y)
	{
		render_state.draw_offset_x = x;
		render_state.draw_offset_y = y;
	}

	inline void set_scissored_invariant(bool invariant)
	{
		queue.scissor_invariant = invariant;
	}

	void set_texture_window(const TextureWindow &rect);
	inline void set_texture_offset(unsigned x, unsigned y)
	{
		atlas.set_texture_offset(x, y);
		render_state.texture_offset_x = x;
		render_state.texture_offset_y = y;
	}

	inline void set_palette_offset(unsigned x, unsigned y)
	{
		atlas.set_palette_offset(x, y);
		render_state.palette_offset_x = x;
		render_state.palette_offset_y = y;
	}

	Vulkan::BufferHandle copy_cpu_to_vram(const Rect &rect);
	void copy_vram_to_cpu_synchronous(const Rect &rect, uint16_t *vram);
	uint16_t *begin_copy(Vulkan::BufferHandle handle);
	void end_copy(Vulkan::BufferHandle handle);

	void notify_texture_upload(Rect rect, uint16_t *vram);

	void blit_vram(const Rect &dst, const Rect &src);

	void set_vram_framebuffer_coords(unsigned xstart, unsigned ystart)
	{
		last_scanout.reset();

		render_state.display_fb_xstart = xstart;
		render_state.display_fb_ystart = ystart;
	}

	void set_horizontal_display_range(int x1, int x2)
	{
		render_state.horiz_start = x1;
		render_state.horiz_end = x2;
	}

	void set_vertical_display_range(int y1, int y2)
	{
		render_state.vert_start = y1;
		render_state.vert_end = y2;
	}

	void set_display_mode(ScanoutMode mode, bool is_pal, bool is_480i, WidthMode width_mode)
	{
		//if (rect != render_state.display_mode || render_state.scanout_mode != mode)
		//	last_scanout.reset();
		last_scanout.reset();

		//render_state.display_mode = rect;
		render_state.scanout_mode = mode;

		render_state.is_pal = is_pal;
		render_state.is_480i = is_480i;
		render_state.width_mode = width_mode;
	}

	void set_horizontal_overscan_cropping(int crop_overscan)
	{
		render_state.crop_overscan = crop_overscan;
	}

	void set_horizontal_offset_cycles(int offset_cycles)
	{
		render_state.offset_cycles = offset_cycles;
	}
	
	void set_horizontal_additional_cropping(unsigned image_crop)
	{
		render_state.image_crop = image_crop;
	}

	void set_visible_scanlines(int slstart, int slend, int slstart_pal, int slend_pal)
	{
		// May need bounds checking to reject bad inputs. Currently assume all inputs are valid.
		render_state.slstart = slstart;
		render_state.slend = slend;
		render_state.slstart_pal = slstart_pal;
		render_state.slend_pal = slend_pal;
	}

	void set_display_filter(ScanoutFilter filter)
	{
		render_state.scanout_filter = filter;
	}

	void set_mdec_filter(ScanoutFilter mdec_filter)
	{
		render_state.scanout_mdec_filter = mdec_filter;
	}

	void toggle_display(bool enable)
	{
		if (enable != render_state.display_on)
			last_scanout.reset();

		render_state.display_on = enable;
	}

	void set_dither_native_resolution(bool enable)
	{
		render_state.dither_native_resolution = enable;
	}

	Vulkan::ImageHandle scanout_vram_to_texture(bool scaled = true);
	Vulkan::ImageHandle scanout_to_texture();

	inline void set_texture_mode(TextureMode mode)
	{
		render_state.texture_mode = mode;
		atlas.set_texture_mode(mode);
	}

	inline void set_semi_transparent(SemiTransparentMode state)
	{
		render_state.semi_transparent = state;
	}

	inline void set_primitive_type(PrimitiveType primitive_type)
	{
		render_state.primitive_type = primitive_type;
	}

	inline void set_force_mask_bit(bool enable)
	{
		render_state.force_mask_bit = enable;
	}

	inline void set_mask_test(bool enable)
	{
		render_state.mask_test = enable;
	}

	inline void set_texture_color_modulate(bool enable)
	{
		render_state.texture_color_modulate = enable;
	}

	inline void set_UV_limits(uint16_t min_u, uint16_t min_v, uint16_t max_u, uint16_t max_v)
	{
		render_state.UVLimits.min_u = min_u;
		render_state.UVLimits.min_v = min_v;
		render_state.UVLimits.max_u = max_u;
		render_state.UVLimits.max_v = max_v;
	}

	// Draw commands
	void clear_rect(const Rect &rect, uint32_t fb_color);
	void draw_line(const Vertex *vertices);
	void draw_triangle(const Vertex *vertices);
	void draw_quad(const Vertex *vertices);

	SaveState save_vram_state();

	void flush()
	{
		if (cmd)
			device.submit(cmd);
		cmd.reset();
		device.flush_frame();
	}

	Vulkan::Fence flush_and_signal()
	{
		Vulkan::Fence fence;
		if (cmd)
			device.submit(cmd, &fence);
		cmd.reset();
		device.flush_frame();
		return fence;
	}

	enum
	{
		SpecConstIndex_TransMode = 0,
		SpecConstIndex_FilterMode = 1,
		SpecConstIndex_BlendMode = 2,
		SpecConstIndex_Scaling = 3,
		SpecConstIndex_Shift = 4,
		SpecConstIndex_OffsetUV = 5,
		SpecConstIndex_Samples = 0,
	};

	enum FilterExclude
	{
		FilterExcludeNone = 0,
		FilterExcludeOpaque = 1,
		FilterExcludeOpaqueAndSemiTrans = 2,
	};

	enum class FilterMode : uint32_t
	{
		NearestNeighbor = 0,
		XBR = 1,
		SABR = 2,
		Bilinear = 3,
		Bilinear3Point = 4,
		JINC2 = 5
	};

	enum class TransMode : uint32_t
	{
		Opaque = 0,
		SemiTrans = 1,
		SemiTransOpaque = 2
	};

	enum class BlendMode : uint32_t
	{
		BlendAdd = 0,
		BlendAvg = 1,
		BlendSub = 2,
		BlendAddQuarter = 3
	};

	void set_filter_mode(FilterMode mode);
	ScanoutMode get_scanout_mode() const
	{
		return render_state.scanout_mode;
	}

	void set_sprite_filter_exclude(FilterExclude exclude)
	{
		sprite_filter_exclude = exclude;
	}

	void set_polygon_2d_filter_exclude(FilterExclude exclude)
	{
		polygon_2d_filter_exclude = exclude;
	}

	void set_scaled_uv_offset(bool offset)
	{
		scaled_uv_offset = offset;
	}

	/* True iff the constructor finished successfully. The Renderer
	 * constructor does not throw; on failure (e.g. RGBA8_UNORM not
	 * supported) it leaves the object in a destroyable but otherwise
	 * unusable state. Callers must check is_valid() before use. */
	bool is_valid() const { return valid; }

private:
	Vulkan::Device &device;
	unsigned scaling;
	unsigned msaa;
	bool scaled_uv_offset = false;
	bool valid = false;
	FilterMode primitive_filter_mode = FilterMode::NearestNeighbor;
	FilterExclude sprite_filter_exclude = FilterExcludeNone;
	FilterExclude polygon_2d_filter_exclude = FilterExcludeNone;
	Vulkan::ImageHandle scaled_framebuffer;
	Vulkan::ImageHandle scaled_framebuffer_msaa;
	Vulkan::ImageHandle bias_framebuffer;
	Vulkan::ImageHandle framebuffer;
	Vulkan::ImageHandle framebuffer_ssaa;
	std::vector<Vulkan::ImageViewHandle> scaled_views;
	FBAtlas atlas;
	bool texture_tracking_enabled = false;
	TextureTracker tracker;

	Vulkan::CommandBufferHandle cmd;

public:
	// Called by FBAtlas (formerly via HazardListener interface).
	void hazard(StatusFlags flags);
	void resolve(Domain target_domain, unsigned x, unsigned y);
	void flush_render_pass(const Rect &rect);
	void discard_render_pass();
	void clear_quad(const Rect &rect, uint32_t fb_color, bool candidate);

	// Called by TextureTracker (formerly via TextureUploader interface).
	Vulkan::ImageHandle upload_texture(std::vector<LoadedImage> &image);
	Vulkan::ImageHandle create_texture(int width, int height, int levels);
	Vulkan::CommandBufferHandle &command_buffer_hack_fixme();

private:
	void hd_texture_uniforms(HdTextureHandle hd_texture_index);
	HdTextureHandle get_hd_texture_index(const Rect &uvlimits, bool &fastpath_capable_out, bool &cache_hit_out);

	struct
	{
		Vulkan::Program *copy_to_vram;
		Vulkan::Program *copy_to_vram_masked;
		Vulkan::Program *unscaled_quad_blitter;
		Vulkan::Program *scaled_quad_blitter;
		Vulkan::Program *unscaled_dither_quad_blitter;
		Vulkan::Program *scaled_dither_quad_blitter;
		Vulkan::Program *bpp24_quad_blitter;
		Vulkan::Program *bpp24_yuv_quad_blitter;
		Vulkan::Program *resolve_to_scaled;
		Vulkan::Program *resolve_to_unscaled;

		Vulkan::Program *blit_vram_scaled;
		Vulkan::Program *blit_vram_scaled_masked;

		Vulkan::Program *blit_vram_cached_scaled;
		Vulkan::Program *blit_vram_cached_scaled_masked;
		Vulkan::Program *blit_vram_msaa_cached_scaled;
		Vulkan::Program *blit_vram_msaa_cached_scaled_masked;

		Vulkan::Program *blit_vram_unscaled;
		Vulkan::Program *blit_vram_unscaled_masked;
		Vulkan::Program *blit_vram_cached_unscaled;
		Vulkan::Program *blit_vram_cached_unscaled_masked;

		Vulkan::Program *flat;
		Vulkan::Program *textured_scaled;
		Vulkan::Program *textured_unscaled;
		Vulkan::Program *flat_masked;
		Vulkan::Program *textured_masked_scaled;
		Vulkan::Program *textured_masked_unscaled;

		Vulkan::Program *mipmap_resolve;
		Vulkan::Program *mipmap_dither_resolve;
		Vulkan::Program *mipmap_energy_first;
		Vulkan::Program *mipmap_energy;
		Vulkan::Program *mipmap_energy_blur;
	} pipelines;

	Vulkan::ImageHandle dither_lut;

	void init_pipelines();
	void init_primitive_pipelines();
	void init_primitive_feedback_pipelines();
	void ensure_command_buffer();

	RenderState render_state;

	struct BufferVertex
	{
		float x, y, z, w;
		uint32_t color;
		TextureWindow window;
		int16_t pal_x, pal_y, params;
		int16_t u, v, base_uv_x, base_uv_y;
		uint16_t min_u, min_v, max_u, max_v;
	};

	struct BlitInfo
	{
		uint32_t src_offset[2];
		uint32_t dst_offset[2];
		uint32_t extent[2];
		uint32_t mask;
		uint32_t sample;
	};

	struct SemiTransparentState
	{
		int scissor_index;
		HdTextureHandle hd_texture_index;
		SemiTransparentMode semi_transparent;
		bool textured;
		bool masked;
		bool filtering;
		bool scaled_read;
		unsigned shift;
		bool offset_uv;

		bool operator==(const SemiTransparentState &other) const
		{
			return scissor_index == other.scissor_index && hd_texture_index == other.hd_texture_index &&
			       semi_transparent == other.semi_transparent && textured == other.textured && masked == other.masked &&
				   filtering == other.filtering && scaled_read == other.scaled_read && shift == other.shift &&
				   offset_uv == other.offset_uv;
		}

		bool operator!=(const SemiTransparentState &other) const
		{
			return !(*this == other);
		}
	};

	struct ClearCandidate
	{
		Rect rect;
		uint32_t color; /* fb_color */
		float z;
	};

	struct PrimitiveInfo {
		unsigned triangle_index;
		int scissor_index;
		HdTextureHandle hd_texture_index;
		bool filtering;
		bool scaled_read;
		unsigned shift;
		bool offset_uv;

		// needed for emplace_back
		PrimitiveInfo(
			unsigned triangle_index,
			int scissor_index = -1,
			HdTextureHandle hd_texture_index = HdTextureHandle::make_none(),
			bool filtering = false,
			bool scaled_read = false,
			unsigned shift = 0,
			bool offset_uv = false
		)
			: triangle_index(triangle_index), scissor_index(scissor_index), hd_texture_index(hd_texture_index),
			filtering(filtering), scaled_read(scaled_read), shift(shift), offset_uv(offset_uv)
		{}
	};

	struct OpaqueQueue
	{
		// Non-textured primitives.
		std::vector<BufferVertex> opaque;
		std::vector<PrimitiveInfo> opaque_scissor;

		// Textured primitives, no semi-transparency.
		std::vector<BufferVertex> opaque_textured;
		std::vector<PrimitiveInfo> opaque_textured_scissor;

		// Textured primitives, semi-transparency enabled.
		std::vector<BufferVertex> semi_transparent_opaque;
		std::vector<PrimitiveInfo> semi_transparent_opaque_scissor;

		std::vector<BufferVertex> semi_transparent;
		std::vector<SemiTransparentState> semi_transparent_state;

		std::vector<Vulkan::ImageHandle> textures;

		std::vector<VkRect2D> scaled_resolves;
		std::vector<VkRect2D> unscaled_resolves;
		std::vector<BlitInfo> scaled_blits;
		std::vector<BlitInfo> scaled_masked_blits;
		std::vector<BlitInfo> unscaled_blits;
		std::vector<BlitInfo> unscaled_masked_blits;

		std::vector<VkRect2D> scissors;
		std::vector<ClearCandidate> clear_candidates;
		VkRect2D default_scissor;
		bool scissor_invariant = false;
	} queue;
	unsigned primitive_index = 0;
	bool render_pass_is_feedback = false;
	float last_uv_scale_x, last_uv_scale_y;

	void dispatch(const std::vector<BufferVertex> &vertices, std::vector<PrimitiveInfo> &scissors, bool textured = false);
	static bool primitive_info_sort_gt(const PrimitiveInfo &a, const PrimitiveInfo &b);
	void render_opaque_primitives();
	void render_opaque_texture_primitives();
	void render_semi_transparent_opaque_texture_primitives();
	void render_semi_transparent_primitives();
	void semi_transparent_set_state(const SemiTransparentState &state);
	void dispatch_set_scaled_read_texture(bool scaled_read, bool textured);
	void reset_queue();

	float allocate_depth(Domain domain, const Rect &rect);

	void build_attribs(BufferVertex *verts, const Vertex *vertices, unsigned count, HdTextureHandle &hd_texture_index,
		bool &filtering, bool &scaled_read, unsigned &shift, bool &offset_uv);
	void build_line_quad(Vertex *quad, const Vertex *line);
	std::vector<BufferVertex> *select_pipeline(unsigned prims, int scissor, HdTextureHandle hd_texture,
		bool filtering, bool scaled_read, unsigned shift, bool offset_uv);
	bool get_filer_exclude(FilterExclude exclude)
	{
		if (
			render_state.primitive_type == PrimitiveType::Sprite &&
			sprite_filter_exclude >= exclude
		)
			return true;
		if (
			render_state.primitive_type == PrimitiveType::May_Be_2D_Polygon &&
			polygon_2d_filter_exclude >= exclude
		)
			return true;
		return false;
	}

	void flush_resolves();
	void flush_blits();
	void flush_blit(const std::vector<BlitInfo> &infos, Vulkan::Program &program, bool scaled);
	void reset_scissor_queue();
	const ClearCandidate *find_clear_candidate(const Rect &rect) const;

	Rect compute_window_rect(const TextureWindow &window);

	Vulkan::ImageHandle last_scanout;
	Vulkan::ImageHandle reuseable_scanout;
	DisplayRect compute_display_rect();

	Rect compute_vram_framebuffer_rect();

	void mipmap_framebuffer();
	void ssaa_framebuffer();
	Vulkan::BufferHandle quad;
};
}

/* === folded parallel-psx/renderer/renderer.cpp === */
#include <algorithm>
#include <math.h>
#include <string.h>

namespace PSX
{
static const uint32_t quad_vert[] =
#include "quad.vert.inc"
    ;
static const uint32_t scaled_quad_frag[] =
#include "scaled.quad.frag.inc"
    ;
static const uint32_t scaled_dither_quad_frag[] =
#include "scaled.dither.quad.frag.inc"
    ;
static const uint32_t bpp24_quad_frag[] =
#include "bpp24.quad.frag.inc"
    ;
static const uint32_t bpp24_yuv_quad_frag[] =
#include "bpp24.yuv.quad.frag.inc"
    ;
static const uint32_t unscaled_quad_frag[] =
#include "unscaled.quad.frag.inc"
    ;
static const uint32_t unscaled_dither_quad_frag[] =
#include "unscaled.dither.quad.frag.inc"
    ;
static const uint32_t copy_vram_comp[] =
#include "copy_vram.comp.inc"
    ;
static const uint32_t copy_vram_masked_comp[] =
#include "copy_vram.masked.comp.inc"
    ;
static const uint32_t resolve_to_scaled[] =
#include "resolve.scaled.comp.inc"
    ;
static const uint32_t resolve_to_msaa_scaled[] =
#include "resolve.msaa.scaled.comp.inc"
    ;
static const uint32_t resolve_to_unscaled[] =
#include "resolve.unscaled.comp.inc"
    ;
static const uint32_t resolve_msaa_to_unscaled[] =
#include "resolve.msaa.unscaled.comp.inc"
    ;

static const uint32_t flat_vert[] =
#include "flat.vert.inc"
    ;
static const uint32_t flat_unscaled_vert[] =
#include "flat.unscaled.vert.inc"
    ;
static const uint32_t flat_frag[] =
#include "flat.frag.inc"
    ;
static const uint32_t textured_vert[] =
#include "textured.vert.inc"
    ;
static const uint32_t textured_unscaled_vert[] =
#include "textured.unscaled.vert.inc"
    ;
static const uint32_t textured_frag[] =
#include "textured.frag.inc"
    ;
static const uint32_t textured_unscaled_frag[] =
#include "textured.unscaled.frag.inc"
    ;
static const uint32_t textured_msaa_frag[] =
#include "textured.msaa.frag.inc"
    ;
static const uint32_t textured_msaa_unscaled_frag[] =
#include "textured.msaa.unscaled.frag.inc"
    ;

static const uint32_t blit_vram_scaled_comp[] =
#include "blit_vram.scaled.comp.inc"
    ;
static const uint32_t blit_vram_scaled_masked_comp[] =
#include "blit_vram.masked.scaled.comp.inc"
    ;
static const uint32_t blit_vram_cached_scaled_comp[] =
#include "blit_vram.cached.scaled.comp.inc"
    ;
static const uint32_t blit_vram_cached_scaled_masked_comp[] =
#include "blit_vram.cached.masked.scaled.comp.inc"
    ;

static const uint32_t blit_vram_msaa_scaled_comp[] =
#include "blit_vram.msaa.scaled.comp.inc"
    ;
static const uint32_t blit_vram_msaa_scaled_masked_comp[] =
#include "blit_vram.msaa.masked.scaled.comp.inc"
    ;
static const uint32_t blit_vram_msaa_cached_scaled_comp[] =
#include "blit_vram.msaa.cached.scaled.comp.inc"
    ;
static const uint32_t blit_vram_msaa_cached_scaled_masked_comp[] =
#include "blit_vram.msaa.cached.masked.scaled.comp.inc"
    ;

static const uint32_t blit_vram_unscaled_comp[] =
#include "blit_vram.unscaled.comp.inc"
    ;
static const uint32_t blit_vram_unscaled_masked_comp[] =
#include "blit_vram.masked.unscaled.comp.inc"
    ;

static const uint32_t blit_vram_cached_unscaled_comp[] =
#include "blit_vram.cached.unscaled.comp.inc"
    ;
static const uint32_t blit_vram_cached_unscaled_masked_comp[] =
#include "blit_vram.cached.masked.unscaled.comp.inc"
    ;

static const uint32_t feedback_frag[] =
#include "feedback.frag.inc"
    ;
static const uint32_t feedback_unscaled_frag[] =
#include "feedback.unscaled.frag.inc"
    ;
static const uint32_t feedback_flat_frag[] =
#include "feedback.flat.frag.inc"
    ;

static const uint32_t feedback_msaa_frag[] =
#include "feedback.msaa.frag.inc"
    ;
static const uint32_t feedback_msaa_unscaled_frag[] =
#include "feedback.msaa.unscaled.frag.inc"
    ;
static const uint32_t feedback_msaa_flat_frag[] =
#include "feedback.msaa.flat.frag.inc"
    ;

static const uint32_t mipmap_vert[] =
#include "mipmap.vert.inc"
    ;
static const uint32_t mipmap_shifted_vert[] =
#include "mipmap.shifted.vert.inc"
    ;
static const uint32_t mipmap_energy_first_frag[] =
#include "mipmap.energy.first.frag.inc"
    ;
static const uint32_t mipmap_resolve_frag[] =
#include "mipmap.resolve.frag.inc"
    ;
static const uint32_t mipmap_dither_resolve_frag[] =
#include "mipmap.dither.resolve.frag.inc"
    ;
static const uint32_t mipmap_energy_frag[] =
#include "mipmap.energy.frag.inc"
    ;
static const uint32_t mipmap_energy_blur_frag[] =
#include "mipmap.energy.blur.frag.inc"
    ;
}

// 3 LSBs are ignored.
static inline uint32_t fbcolor_to_rgba8(uint32_t color)
{
	return color & 0xfff8f8f8u;
}

static inline void fbcolor_to_rgba32f(float *v, uint32_t color)
{
	// 3 LSBs are ignored.
	unsigned r = (color >> 0) & 0xf8;
	unsigned g = (color >> 8) & 0xf8;
	unsigned b = (color >> 16) & 0xf8;
	v[0] = r * (1.0f / 255.0f);
	v[1] = g * (1.0f / 255.0f);
	v[2] = b * (1.0f / 255.0f);
	// Mask bit is always cleared.
	v[3] = 0.0f;
}

using namespace Vulkan;

namespace PSX
{
Renderer::Renderer(Device &device, unsigned scaling_, unsigned msaa_, const SaveState *state)
    : device(device)
    , scaling(scaling_)
    , msaa(msaa_)
{
	// Sanity check settings, 16x IR with 16x MSAA will exhaust most GPUs VRAM alone.
	if (scaling == 16 && msaa > 1)
	{
		LOGI("[Vulkan]: Internal resolution scale of 16x is used, limiting MSAA to 1x for memory reasons.\n");
		msaa = 1;
	}
	else if (scaling == 8 && msaa > 4)
	{
		LOGI("[Vulkan]: Internal resolution scale of 8x is used, limiting MSAA to 4x for memory reasons.\n");
		msaa = 4;
	}

	// Verify we can actually render at our target scaling factor.
	// Some devices only support 8K textures, which means max 8x scale.
	VkImageFormatProperties props;
	if (device.get_image_format_properties(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_STORAGE_BIT |
				VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT,
				0,
				&props))
	{
		unsigned max_scaling = std::min(props.maxExtent.width / FB_WIDTH, props.maxExtent.height / FB_HEIGHT);
		unsigned new_scale = scaling;
		while (new_scale > max_scaling)
			new_scale >>= 1;

		if (new_scale != scaling)
		{
			LOGI("[Vulkan]: Internal resolution scale of %ux was chosen, but this is not supported, using %ux instead.\n",
					scaling, new_scale);
			scaling = new_scale;
		}
	}
	else
	{
		LOGE("[Vulkan]: RGBA8_UNORM is not supported. This should never happen, and something might have been corrupted.\n");
		return;
	}

	ImageCreateInfo info = ImageCreateInfo::render_target(FB_WIDTH, FB_HEIGHT, VK_FORMAT_R32_UINT);
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_SAMPLED_BIT;

	if (state)
	{
		render_state = state->state;
		atlas.set_texture_offset(render_state.texture_offset_x, render_state.texture_offset_y);
		atlas.set_texture_mode(render_state.texture_mode);
		atlas.set_draw_rect(render_state.draw_rect);
		atlas.set_palette_offset(render_state.palette_offset_x, render_state.palette_offset_y);
		atlas.set_texture_window(render_state.cached_window_rect);
		atlas.write_transfer(Domain::Unscaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	}

	ImageInitialData initial_vram = {
		state ? state->vram.data() : nullptr, 0, 0,
	};
	framebuffer = device.create_image(info, state ? &initial_vram : nullptr);
	framebuffer->set_layout(Layout::General);
	framebuffer_ssaa = device.create_image(info);
	framebuffer_ssaa->set_layout(Layout::General);

	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.format = VK_FORMAT_R8_UNORM;
	info.levels = 1;
	bias_framebuffer = device.create_image(info, nullptr);

	info.width *= scaling;
	info.height *= scaling;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.levels = trailing_zeroes(scaling) + 1;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	scaled_framebuffer = device.create_image(info);
	scaled_framebuffer->set_layout(Layout::General);

	{
		ImageViewCreateInfo view_info = scaled_framebuffer->get_view().get_create_info();
		for (unsigned i = 0; i < info.levels; i++)
		{
			view_info.base_level = i;
			view_info.levels = 1;
			scaled_views.push_back(device.create_image_view(view_info));
		}
	}

	// Check for support.
	if (msaa > 1)
	{
		if (!device.get_device_features().enabled_features.sampleRateShading)
		{
			msaa = 1;
			LOGI("[Vulkan]: sampleRateShading is not supported by this implementation. Cannot use MSAA.\n");
		}
		else if (!device.get_device_features().enabled_features.shaderStorageImageMultisample)
		{
			msaa = 1;
			LOGI("[Vulkan]: shaderStorageImageMultisample is not supported by this implementation. Cannot use MSAA.\n");
		}
		else if (!device.get_image_format_properties(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_STORAGE_BIT |
					VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_SAMPLED_BIT,
					0,
					&props))
		{
			LOGI("[Vulkan]: Cannot use multisampling with this device.\n");
			msaa = 1;
		}
		else if ((msaa & props.sampleCounts) == 0)
		{
			unsigned new_msaa = msaa >> 1;
			while (new_msaa)
			{
				if (new_msaa & props.sampleCounts)
				{
					LOGI("[Vulkan]: MSAA sample count of %u is not supported, falling back to %u.\n",
							msaa, new_msaa);
					msaa = new_msaa;
					break;
				}
			}

			if (msaa == 0)
				msaa = 1;
		}
	}

	if (msaa > 1)
	{
		info.levels = 1;
		info.samples = static_cast<VkSampleCountFlagBits>(msaa);
		scaled_framebuffer_msaa = device.create_image(info);
		scaled_framebuffer_msaa->set_layout(Layout::General);
		// General layout for MSAA is going to be brutal bandwidth-wise, but we have no real choice.
		// The expectation is that this will be used with a lower scaling factor to compensate.
	}

	atlas.set_hazard_listener(this);
	tracker.set_texture_uploader(this);

	init_pipelines();

	ensure_command_buffer();
	cmd->clear_image(*scaled_framebuffer, {});
	if (!state)
		cmd->clear_image(*framebuffer, {});
	cmd->full_barrier();

	ImageCreateInfo dither_info = ImageCreateInfo::immutable_2d_image(4, 4, VK_FORMAT_R8_UNORM);
	// This lut is biased with 4 to be able to use UNORM easily.
	static const uint8_t dither_lut_data[16] = { 0, 4, 1, 5, 6, 2, 7, 3, 1, 5, 0, 4, 7, 3, 6, 2 };

	ImageInitialData dither_initial = { dither_lut_data };
	dither_lut = device.create_image(dither_info, &dither_initial);

	static const float quad_data[] = {
		-128, -128, +127, -128, -128, +127, +127, +127,
	};

	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::Device;
	buffer_create_info.size = sizeof(quad_data);
	buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	quad = device.create_buffer(buffer_create_info, quad_data);

	flush();
	reset_scissor_queue();

	if (state) {
		tracker.load_state(state->tracker_state);
	}

	valid = true;
}

Renderer::SaveState Renderer::save_vram_state()
{
	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::CachedHost;
	buffer_create_info.size = FB_WIDTH * FB_HEIGHT * sizeof(uint32_t);
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	BufferHandle buffer = device.create_buffer(buffer_create_info, nullptr);
	atlas.read_transfer(Domain::Unscaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	ensure_command_buffer();
	cmd->copy_image_to_buffer(*buffer, *framebuffer, 0, { 0, 0, 0 }, { FB_WIDTH, FB_HEIGHT, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
	             VK_ACCESS_HOST_READ_BIT);

	flush();

	device.wait_idle();
	const uint32_t *src = static_cast<const uint32_t *>(
			device.map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT));
	/* Iterator-range construction copies straight into the vector's
	 * fresh allocation without the default-ctor's prior zero-fill;
	 * saves 2 MiB of write traffic per savestate save on a 1024x512
	 * VRAM. */
	std::vector<uint32_t> vram(src, src + FB_WIDTH * FB_HEIGHT);
	device.unmap_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
	return { std::move(vram), render_state, tracker.save_state() };
}

void Renderer::set_filter_mode(FilterMode mode)
{
	if (mode != primitive_filter_mode)
	{
		primitive_filter_mode = mode;
	}
}

void Renderer::init_primitive_pipelines()
{
	if (msaa > 1 || scaling > 1)
		pipelines.flat = device.request_program(flat_vert, sizeof(flat_vert), flat_frag, sizeof(flat_frag));
	else
		pipelines.flat = device.request_program(flat_unscaled_vert, sizeof(flat_unscaled_vert), flat_frag, sizeof(flat_frag));

	if (msaa > 1)
	{
		pipelines.textured_scaled = device.request_program(textured_vert, sizeof(textured_vert), textured_msaa_frag, sizeof(textured_msaa_frag));
		pipelines.textured_unscaled = device.request_program(textured_vert, sizeof(textured_vert), textured_msaa_unscaled_frag, sizeof(textured_msaa_unscaled_frag));
	}
	else
	{
		if (scaling > 1)
		{
			pipelines.textured_scaled = device.request_program(textured_vert, sizeof(textured_vert), textured_frag, sizeof(textured_frag));
			pipelines.textured_unscaled = device.request_program(textured_vert, sizeof(textured_vert), textured_unscaled_frag, sizeof(textured_unscaled_frag));
		}
		else
		{
			pipelines.textured_scaled = device.request_program(textured_unscaled_vert, sizeof(textured_unscaled_vert), textured_frag, sizeof(textured_frag));
			pipelines.textured_unscaled = device.request_program(textured_unscaled_vert, sizeof(textured_unscaled_vert), textured_unscaled_frag, sizeof(textured_unscaled_frag));
		}
	}
}

void Renderer::init_primitive_feedback_pipelines()
{
	// TODO: The masked pipelines do not have filter options.
	if (msaa > 1)
	{
		pipelines.textured_masked_scaled = device.request_program(textured_vert, sizeof(textured_vert),
				feedback_msaa_frag, sizeof(feedback_msaa_frag));
		pipelines.textured_masked_unscaled = device.request_program(textured_vert, sizeof(textured_vert),
				feedback_msaa_unscaled_frag, sizeof(feedback_msaa_unscaled_frag));
		pipelines.flat_masked = device.request_program(flat_vert, sizeof(flat_vert),
				feedback_msaa_flat_frag, sizeof(feedback_msaa_flat_frag));
	}
	else
	{
		if (scaling > 1)
		{
			pipelines.flat_masked = device.request_program(flat_vert, sizeof(flat_vert),
					feedback_flat_frag, sizeof(feedback_flat_frag));
			pipelines.textured_masked_scaled = device.request_program(textured_vert, sizeof(textured_vert),
					feedback_frag, sizeof(feedback_frag));
			pipelines.textured_masked_unscaled = device.request_program(textured_vert, sizeof(textured_vert),
					feedback_unscaled_frag, sizeof(feedback_unscaled_frag));
		}
		else
		{
			pipelines.flat_masked = device.request_program(flat_unscaled_vert, sizeof(flat_unscaled_vert),
					feedback_flat_frag, sizeof(feedback_flat_frag));
			pipelines.textured_masked_scaled = device.request_program(textured_unscaled_vert, sizeof(textured_unscaled_vert),
					feedback_frag, sizeof(feedback_frag));
			pipelines.textured_masked_unscaled = device.request_program(textured_unscaled_vert, sizeof(textured_unscaled_vert),
					feedback_unscaled_frag, sizeof(feedback_unscaled_frag));
		}
	}
}

void Renderer::init_pipelines()
{
	if (msaa > 1)
		pipelines.resolve_to_unscaled = device.request_program(resolve_msaa_to_unscaled, sizeof(resolve_msaa_to_unscaled));
	else
		pipelines.resolve_to_unscaled = device.request_program(resolve_to_unscaled, sizeof(resolve_to_unscaled));

	pipelines.scaled_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), scaled_quad_frag, sizeof(scaled_quad_frag));
	pipelines.scaled_dither_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), scaled_dither_quad_frag, sizeof(scaled_dither_quad_frag));
	pipelines.bpp24_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), bpp24_quad_frag, sizeof(bpp24_quad_frag));
	pipelines.bpp24_yuv_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), bpp24_yuv_quad_frag, sizeof(bpp24_yuv_quad_frag));
	pipelines.unscaled_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), unscaled_quad_frag, sizeof(unscaled_quad_frag));
	pipelines.unscaled_dither_quad_blitter =
		device.request_program(quad_vert, sizeof(quad_vert), unscaled_dither_quad_frag, sizeof(unscaled_dither_quad_frag));

	pipelines.copy_to_vram = device.request_program(copy_vram_comp, sizeof(copy_vram_comp));
	pipelines.copy_to_vram_masked = device.request_program(copy_vram_masked_comp, sizeof(copy_vram_masked_comp));

	if (msaa > 1)
	{
		pipelines.resolve_to_scaled =
			device.request_program(resolve_to_msaa_scaled, sizeof(resolve_to_msaa_scaled));

		pipelines.blit_vram_scaled =
			device.request_program(blit_vram_msaa_scaled_comp, sizeof(blit_vram_msaa_scaled_comp));
		pipelines.blit_vram_scaled_masked =
			device.request_program(blit_vram_msaa_scaled_masked_comp, sizeof(blit_vram_msaa_scaled_masked_comp));
		pipelines.blit_vram_msaa_cached_scaled =
			device.request_program(blit_vram_msaa_cached_scaled_comp, sizeof(blit_vram_msaa_cached_scaled_comp));
		pipelines.blit_vram_msaa_cached_scaled_masked =
			device.request_program(blit_vram_msaa_cached_scaled_masked_comp, sizeof(blit_vram_msaa_cached_scaled_masked_comp));
	}
	else
	{
		pipelines.resolve_to_scaled = device.request_program(resolve_to_scaled, sizeof(resolve_to_scaled));

		pipelines.blit_vram_scaled = device.request_program(blit_vram_scaled_comp, sizeof(blit_vram_scaled_comp));
		pipelines.blit_vram_scaled_masked =
			device.request_program(blit_vram_scaled_masked_comp, sizeof(blit_vram_scaled_masked_comp));
	}

	pipelines.blit_vram_cached_scaled =
		device.request_program(blit_vram_cached_scaled_comp, sizeof(blit_vram_cached_scaled_comp));
	pipelines.blit_vram_cached_scaled_masked =
		device.request_program(blit_vram_cached_scaled_masked_comp, sizeof(blit_vram_cached_scaled_masked_comp));

	pipelines.blit_vram_unscaled = device.request_program(blit_vram_unscaled_comp, sizeof(blit_vram_unscaled_comp));
	pipelines.blit_vram_unscaled_masked =
		device.request_program(blit_vram_unscaled_masked_comp, sizeof(blit_vram_unscaled_masked_comp));
	pipelines.blit_vram_cached_unscaled =
		device.request_program(blit_vram_cached_unscaled_comp, sizeof(blit_vram_cached_unscaled_comp));
	pipelines.blit_vram_cached_unscaled_masked =
		device.request_program(blit_vram_cached_unscaled_masked_comp, sizeof(blit_vram_cached_unscaled_masked_comp));

	pipelines.mipmap_resolve =
		device.request_program(mipmap_vert, sizeof(mipmap_vert), mipmap_resolve_frag, sizeof(mipmap_resolve_frag));
	pipelines.mipmap_dither_resolve =
		device.request_program(mipmap_vert, sizeof(mipmap_vert), mipmap_dither_resolve_frag, sizeof(mipmap_dither_resolve_frag));

	pipelines.mipmap_energy = device.request_program(mipmap_shifted_vert, sizeof(mipmap_shifted_vert),
			mipmap_energy_frag, sizeof(mipmap_energy_frag));
	pipelines.mipmap_energy_first = device.request_program(mipmap_shifted_vert, sizeof(mipmap_shifted_vert),
			mipmap_energy_first_frag, sizeof(mipmap_energy_first_frag));
	pipelines.mipmap_energy_blur = device.request_program(mipmap_shifted_vert, sizeof(mipmap_shifted_vert),
			mipmap_energy_blur_frag, sizeof(mipmap_energy_blur_frag));

	init_primitive_pipelines();
	init_primitive_feedback_pipelines();
}

void Renderer::set_draw_rect(const Rect &rect)
{
	atlas.set_draw_rect(rect);
	render_state.draw_rect = rect;

	const VkRect2D &last = queue.scissors.back();
	const int scaled_x = int(rect.x * scaling);
	const int scaled_y = int(rect.y * scaling);
	const unsigned scaled_w = rect.width * scaling;
	const unsigned scaled_h = rect.height * scaling;
	if (last.offset.x != scaled_x || last.offset.y != scaled_y ||
	    last.extent.width != scaled_w || last.extent.height != scaled_h)
		queue.scissors.push_back(
		    { { scaled_x, scaled_y }, { scaled_w, scaled_h } });
}

void Renderer::clear_rect(const Rect &rect, uint32_t fb_color)
{
	if (texture_tracking_enabled) {
		tracker.clearRegion(rect);
	}
	last_scanout.reset();
	atlas.clear_rect(rect, fb_color);

	VK_ASSERT(rect.x + rect.width <= FB_WIDTH);
	VK_ASSERT(rect.y + rect.height <= FB_HEIGHT);
}

Rect Renderer::compute_window_rect(const TextureWindow &window)
{
	unsigned mask_bits_x = 32 - leading_zeroes(window.mask_x);
	unsigned mask_bits_y = 32 - leading_zeroes(window.mask_y);
	unsigned x = window.or_x & ~((1u << mask_bits_x) - 1);
	unsigned y = window.or_y & ~((1u << mask_bits_y) - 1);
	return { x, y, 1u << mask_bits_x, 1u << mask_bits_y };
}

void Renderer::set_texture_window(const TextureWindow &window)
{
	render_state.texture_window = window;
	render_state.cached_window_rect = compute_window_rect(window);
}

void Renderer::copy_vram_to_cpu_synchronous(const Rect &rect, uint16_t *vram)
{
	bool wrap_x = rect.x + rect.width > FB_WIDTH;
	bool wrap_y = rect.y + rect.height > FB_HEIGHT;
	Rect copy_rect = rect;
	bool wrap = wrap_x || wrap_y;
	// We could do four separate reads but this is eaiser
	if (wrap)
	{
		copy_rect.x = 0;
		copy_rect.width = FB_WIDTH;
		copy_rect.y = 0;
		copy_rect.height = FB_HEIGHT;
	}

	atlas.read_transfer(Domain::Unscaled, copy_rect);
	ensure_command_buffer();

	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::CachedHost;
	buffer_create_info.size = copy_rect.width * copy_rect.height * 4;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	BufferHandle buffer = device.create_buffer(buffer_create_info, nullptr);
	cmd->copy_image_to_buffer(*buffer, *framebuffer, 0, { int(copy_rect.x), int(copy_rect.y), 0 },
	                          { copy_rect.width, copy_rect.height, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	Vulkan::Fence fence = flush_and_signal();
	fence->wait();

	const uint32_t *mapped = static_cast<const uint32_t *>(device.map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT));

	if (!wrap)
	{
		for (uint16_t y = 0; y < rect.height; y++)
			for (uint16_t x = 0; x < rect.width; x++)
				vram[(y + rect.y) * FB_WIDTH + (x + rect.x)] = uint16_t(mapped[y * rect.width + x]);
	}
	else
	{
		for (uint16_t y = 0; y < rect.height; y++)
			for (uint16_t x = 0; x < rect.width; x++)
				{
					uint32_t h = (x + rect.x) & (FB_WIDTH - 1);
					uint32_t v = (y + rect.y) & (FB_HEIGHT - 1);
					uint32_t p = v * FB_WIDTH + h;
					vram[p] = uint16_t(mapped[p]);
				}
	}

	if (texture_tracking_enabled) {
		tracker.notifyReadback(rect, vram);
	}

	device.unmap_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
}

void Renderer::mipmap_framebuffer()
{
	// render_state.display_fb_rect = compute_vram_framebuffer_rect();
	Rect rect = render_state.display_fb_rect;
	if (rect.x + rect.width > FB_WIDTH)
	{
		rect.x = 0;
		rect.width = FB_WIDTH;
	}
	if (rect.y + rect.height > FB_HEIGHT)
	{
		rect.y = 0;
		rect.height = FB_HEIGHT;
	}
	unsigned levels = scaled_views.size();

	ensure_command_buffer();
	for (unsigned i = 1; i <= levels; i++)
	{
		RenderPassInfo rp;
		unsigned current_scale = std::max(scaling >> i, 1u);

		if (i == levels)
			rp.color_attachments[0] = &bias_framebuffer->get_view();
		else
			rp.color_attachments[0] = scaled_views[i].get();

		rp.num_color_attachments = 1;
		rp.store_attachments = 1;
		rp.render_area = { { int(rect.x * current_scale), int(rect.y * current_scale) },
			               { rect.width * current_scale, rect.height * current_scale } };

		if (i == levels)
		{
			cmd->image_barrier(*bias_framebuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		}

		cmd->begin_render_pass(rp);

		if (i == levels)
			cmd->set_program(*pipelines.mipmap_energy_blur);
		else if (i == 1)
			cmd->set_program(*pipelines.mipmap_energy_first);
		else
			cmd->set_program(*pipelines.mipmap_energy);

		cmd->set_texture(0, 0, *scaled_views[i - 1], StockSampler::LinearClamp);

		cmd->set_quad_state();
		cmd->set_vertex_binding(0, *quad, 0, 8);
		struct Push
		{
			float offset[2];
			float range[2];
			float inv_resolution[2];
			float uv_min[2];
			float uv_max[2];
		};
		Push push = {
			{ float(rect.x) / FB_WIDTH, float(rect.y) / FB_HEIGHT },
			{ float(rect.width) / FB_WIDTH, float(rect.height) / FB_HEIGHT },
			{ 1.0f / (FB_WIDTH * current_scale), 1.0f / (FB_HEIGHT * current_scale) },
			{ (rect.x + 0.5f) / FB_WIDTH, (rect.y + 0.5f) / FB_HEIGHT },
			{ (rect.x + rect.width - 0.5f) / FB_WIDTH, (rect.y + rect.height - 0.5f) / FB_HEIGHT },
		};
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		cmd->draw(4);

		cmd->end_render_pass();

		if (i == levels)
		{
			cmd->image_barrier(*bias_framebuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                   VK_ACCESS_SHADER_READ_BIT);
		}
		else
		{
			cmd->image_barrier(*scaled_framebuffer, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                   VK_ACCESS_SHADER_READ_BIT);
		}
	}
}

void Renderer::ssaa_framebuffer()
{
	// render_state.display_fb_rect = compute_vram_framebuffer_rect();
	Rect &rect = render_state.display_fb_rect;
	unsigned left = rect.x / BLOCK_WIDTH;
	unsigned top = rect.y / BLOCK_HEIGHT;
	unsigned right = (rect.x + rect.width + BLOCK_WIDTH - 1) / BLOCK_WIDTH;
	unsigned bottom = (rect.y + rect.height + BLOCK_HEIGHT - 1) / BLOCK_HEIGHT;

	std::vector<VkRect2D> resolves_ssaa;
	for (unsigned y = top; y < bottom; y++)
		for (unsigned x = left; x < right; x++)
			resolves_ssaa.push_back({
				{ int(x * BLOCK_WIDTH % FB_WIDTH), int(y * BLOCK_HEIGHT % FB_HEIGHT) },
				{ BLOCK_WIDTH, BLOCK_HEIGHT }
			});

	ensure_command_buffer();

	cmd->set_specialization_constant(SpecConstIndex_Samples, msaa);
	cmd->set_specialization_constant(SpecConstIndex_FilterMode, 1);
	cmd->set_specialization_constant(SpecConstIndex_Scaling, scaling);
	cmd->set_program(*pipelines.resolve_to_unscaled);
	cmd->set_storage_texture(0, 0, framebuffer_ssaa->get_view());
	if (msaa > 1)
		cmd->set_texture(0, 1, scaled_framebuffer_msaa->get_view(), StockSampler::NearestClamp);
	else
		cmd->set_texture(0, 1, *scaled_views[0], StockSampler::LinearClamp);

	struct Push
	{
		float inv_size[2];
		uint32_t scale;
	};
	unsigned size = resolves_ssaa.size();
	for (unsigned i = 0; i < size; i += 1024)
	{
		unsigned to_run = std::min(size - i, 1024u);

		Push push = { { 1.0f / FB_WIDTH, 1.0f / FB_HEIGHT }, 1u };
		cmd->push_constants(&push, 0, sizeof(push));
		void *ptr = cmd->allocate_constant_data(1, 0, to_run * sizeof(VkRect2D));
		memcpy(ptr, resolves_ssaa.data() + i, to_run * sizeof(VkRect2D));
		cmd->set_specialization_constant_mask(-1);
		cmd->dispatch(1, 1, to_run);
	}
}

Rect Renderer::compute_vram_framebuffer_rect()
{
	unsigned clock_div;
	switch (render_state.width_mode)
	{
	case WidthMode::WIDTH_MODE_256:
		clock_div = 10;
		break;
	case WidthMode::WIDTH_MODE_320:
		clock_div = 8;
		break;
	case WidthMode::WIDTH_MODE_512:
		clock_div = 5;
		break;
	case WidthMode::WIDTH_MODE_640:
		clock_div = 4;
		break;
	case WidthMode::WIDTH_MODE_368:
		clock_div = 7;
		break;
	}

	unsigned fb_width = (unsigned) (render_state.horiz_end - render_state.horiz_start);
	fb_width /= clock_div;
	fb_width = (fb_width + 2) & ~3;

	unsigned fb_height = (unsigned) (render_state.vert_end - render_state.vert_start);
	fb_height *= render_state.is_480i ? 2 : 1;

	return {render_state.display_fb_xstart,
	        render_state.display_fb_ystart,
	        fb_width,
	        fb_height};
}

Renderer::DisplayRect Renderer::compute_display_rect()
{
	unsigned clock_div;
	switch (render_state.width_mode)
	{
	case WidthMode::WIDTH_MODE_256:
		clock_div = 10;
		break;
	case WidthMode::WIDTH_MODE_320:
		clock_div = 8;
		break;
	case WidthMode::WIDTH_MODE_512:
		clock_div = 5;
		break;
	case WidthMode::WIDTH_MODE_640:
		clock_div = 4;
		break;
	case WidthMode::WIDTH_MODE_368:
		clock_div = 7;
		break;
	}

	unsigned display_width;
	int left_offset;
	if (render_state.crop_overscan)
	{
		// Horizontal crop amount is currently hardcoded. Future improvement could allow adjusting this.
		// Restore old center behaviour is render_state.horiz_start is intentionally very high.
		// 938 fixes Gunbird (1008) and Mobile Light Force (EU release of Gunbird),
		// but this value should be lowerer in the future if necessary.
		display_width = (2560/clock_div) - render_state.image_crop;
		if ((render_state.horiz_start < 938) && (render_state.crop_overscan == 2))
			left_offset = floor((render_state.offset_cycles / (double) clock_div) - (render_state.image_crop / 2));
		else
			left_offset = floor(((render_state.horiz_start + render_state.offset_cycles - 608) / (double) clock_div) - (render_state.image_crop / 2));
	}
	else
	{
		display_width = 2800/clock_div;
		left_offset = floor((render_state.horiz_start + render_state.offset_cycles - 488) / (double) clock_div);
	}

	unsigned display_height;
	int upper_offset;
	if (render_state.crop_overscan == 2)
	{
		if (render_state.is_pal)
		{
			display_height = (render_state.vert_end - render_state.vert_start) - (287 - render_state.slend_pal) - render_state.slstart_pal;
			upper_offset = 0 - render_state.slstart_pal;
		}
		else
		{
			display_height = (render_state.vert_end - render_state.vert_start) - (239 - render_state.slend) - render_state.slstart;
			upper_offset = 0 - render_state.slstart;
		}
	}
	if (render_state.crop_overscan != 2 || display_height > (render_state.is_pal ? 288 : 240))
	{
		if (render_state.is_pal)
		{
			display_height = render_state.slend_pal - render_state.slstart_pal + 1;
			upper_offset = render_state.vert_start - 20 - render_state.slstart_pal;
		}
		else
		{
			display_height = render_state.slend - render_state.slstart + 1;
			upper_offset = render_state.vert_start - 16 - render_state.slstart;
		}
	}
	display_height *= (render_state.is_480i ? 2 : 1);
	upper_offset *= (render_state.is_480i ? 2 : 1);

	return DisplayRect(left_offset, upper_offset, display_width, display_height);
}

ImageHandle Renderer::scanout_vram_to_texture(bool scaled)
{
	// Like scanout_to_texture(), but synchronizes the entire
	// VRAM framebuffer atlas before scanout. Does not apply
	// any scanout filters and currently outputs at 15-bit
	// color depth. Current implementation does not reuse
	// prior scanouts.

	atlas.flush_render_pass();

	Rect vram_rect = {0, 0, FB_WIDTH, FB_HEIGHT};

	if (scaled)
		atlas.read_fragment(Domain::Scaled, vram_rect);
	else
		atlas.read_fragment(Domain::Unscaled, vram_rect);

	ensure_command_buffer();

	if (scaled && msaa > 1)
	{
		VkImageSubresourceLayers subres = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		VkOffset3D offset = { 0, 0, 0 };
		VkExtent3D extent = { FB_WIDTH * scaling, FB_HEIGHT * scaling, 1 };
		VkImageResolve region = { subres, offset, subres, offset, extent };
		vkCmdResolveImage(cmd->get_command_buffer(),
			scaled_framebuffer_msaa->get_image(), VK_IMAGE_LAYOUT_GENERAL,
			scaled_framebuffer->get_image(), VK_IMAGE_LAYOUT_GENERAL,
			1, &region);

		cmd->barrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	unsigned render_scale = scaled ? scaling : 1;

	ImageCreateInfo info = ImageCreateInfo::render_target(
			FB_WIDTH * render_scale,
			FB_HEIGHT * render_scale,
			VK_FORMAT_A1R5G5B5_UNORM_PACK16); // Default to 15bit color for now

	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	reuseable_scanout = device.create_image(info);

	RenderPassInfo rp;
	rp.color_attachments[0] = &reuseable_scanout->get_view();
	rp.num_color_attachments = 1;
	rp.store_attachments = 1;

	rp.clear_color[0] = {0, 0, 0, 0};
	rp.clear_attachments = 1;

	cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	cmd->begin_render_pass(rp);
	cmd->set_quad_state();

	if (scaled)
	{
		cmd->set_program(*pipelines.scaled_quad_blitter);
		cmd->set_texture(0, 0, *scaled_views[0], StockSampler::LinearClamp);
	}
	else
	{
		cmd->set_program(*pipelines.unscaled_quad_blitter);
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::LinearClamp);
	}

	cmd->set_vertex_binding(0, *quad, 0, 8);
	struct Push
	{
		float offset[2];
		float scale[2];
		float uv_min[2];
		float uv_max[2];
		float max_bias;
	};

	Push push = { { float(vram_rect.x) / FB_WIDTH, float(vram_rect.y) / FB_HEIGHT },
		          { float(vram_rect.width) / FB_WIDTH, float(vram_rect.height) / FB_HEIGHT },
		          { (vram_rect.x + 0.5f) / FB_WIDTH, (vram_rect.y + 0.5f) / FB_HEIGHT },
		          { (vram_rect.x + vram_rect.width - 0.5f) / FB_WIDTH, (vram_rect.y + vram_rect.height - 0.5f) / FB_HEIGHT },
		          float(scaled_views.size() - 1) };

	cmd->push_constants(&push, 0, sizeof(push));
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	cmd->draw(4);

	cmd->end_render_pass();

	cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                   VK_ACCESS_SHADER_READ_BIT);

	last_scanout = reuseable_scanout;

	return reuseable_scanout;
}

ImageHandle Renderer::scanout_to_texture()
{
	atlas.flush_render_pass();
	if (texture_tracking_enabled) {
		tracker.endFrame();
	}

	if (last_scanout)
		return last_scanout;

	render_state.display_fb_rect = compute_vram_framebuffer_rect();
	Rect &rect = render_state.display_fb_rect;

	if (rect.width == 0 || rect.height == 0 || !render_state.display_on)
	{
		// Black screen, just flush out everything.
		atlas.read_fragment(Domain::Scaled, { 0, 0, FB_WIDTH, FB_HEIGHT });

		ensure_command_buffer();

		ImageCreateInfo info = ImageCreateInfo::render_target(64u, 64u, VK_FORMAT_R8G8B8A8_UNORM);

		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage =
		    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		reuseable_scanout = device.create_image(info);

		RenderPassInfo rp;
		rp.color_attachments[0] = &reuseable_scanout->get_view();
		rp.num_color_attachments = 1;
		rp.clear_attachments = 1;
		rp.store_attachments = 1;

		cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		cmd->begin_render_pass(rp);
		cmd->end_render_pass();

		cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		                   VK_ACCESS_SHADER_READ_BIT);

		last_scanout = reuseable_scanout;
		return reuseable_scanout;
	}

	bool bpp24 = render_state.scanout_mode == ScanoutMode::BGR24;
	bool ssaa = render_state.scanout_filter == ScanoutFilter::SSAA && scaling != 1;

	Rect read_rect = rect;
	if (rect.x + rect.width > FB_WIDTH)
	{
		read_rect.x = 0;
		read_rect.width = FB_WIDTH;
	}
	if (rect.y + rect.height > FB_HEIGHT)
	{
		read_rect.y = 0;
		read_rect.height = FB_HEIGHT;
	}
	if (bpp24)
	{
		Rect tmp = read_rect;
		if (bpp24)
		{
			tmp.width = (tmp.width * 3 + 1) / 2;
			tmp.width = std::min(tmp.width, FB_WIDTH - tmp.x);
		}
		atlas.read_fragment(Domain::Unscaled, tmp);
	}
	else if (ssaa)
		atlas.read_compute(Domain::Scaled, read_rect);
	else
		atlas.read_fragment(Domain::Scaled, read_rect);

	if (!bpp24 && ssaa)
		ssaa_framebuffer();
	else if (msaa > 1)
	{
		ensure_command_buffer();
		VkImageSubresourceLayers subres = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		VkOffset3D offset = { int(rect.x * scaling), int(rect.y * scaling), 0 };
		VkExtent3D extent = { rect.width * scaling, rect.height * scaling, 1 };
		if (rect.x + rect.width > FB_WIDTH)
		{
			offset.x = 0;
			extent.width = FB_WIDTH * scaling;
		}
		if (rect.y + rect.height > FB_HEIGHT)
		{
			offset.y = 0;
			extent.height = FB_HEIGHT * scaling;
		}
		VkImageResolve region = { subres, offset, subres, offset, extent };
		vkCmdResolveImage(cmd->get_command_buffer(),
			scaled_framebuffer_msaa->get_image(), VK_IMAGE_LAYOUT_GENERAL,
			scaled_framebuffer->get_image(), VK_IMAGE_LAYOUT_GENERAL,
			1, &region);

		cmd->barrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	if (render_state.adaptive_smoothing && !bpp24 && !ssaa && scaling != 1)
		mipmap_framebuffer();

	ensure_command_buffer();

	bool scaled = !ssaa;

	unsigned render_scale = scaled ? scaling : 1;

	DisplayRect display_rect = compute_display_rect();

	ImageCreateInfo info = ImageCreateInfo::render_target(
			display_rect.width * render_scale,
			display_rect.height * render_scale,
			render_state.scanout_mode == ScanoutMode::ABGR1555_Dither ? VK_FORMAT_A1R5G5B5_UNORM_PACK16 : VK_FORMAT_R8G8B8A8_UNORM);

	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	reuseable_scanout = device.create_image(info);

	RenderPassInfo rp;
	rp.color_attachments[0] = &reuseable_scanout->get_view();
	rp.num_color_attachments = 1;
	rp.store_attachments = 1;

	rp.clear_color[0] = {0, 0, 0, 0};
	//rp.clear_color[0] = {60.0f/256.0f, 230.0f/256.0f, 60.0f/256.0f, 0};
	rp.clear_attachments = 1;

	cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	cmd->begin_render_pass(rp);
	cmd->set_quad_state();

	VkViewport old_vp = cmd->get_viewport();
	VkViewport new_vp = {display_rect.x * (float) render_scale,
	                     display_rect.y * (float) render_scale,
	                     rect.width * (float) render_scale,
	                     rect.height * (float) render_scale,
	                     old_vp.minDepth,
	                     old_vp.maxDepth};

	cmd->set_viewport(new_vp);

	bool dither = render_state.scanout_mode == ScanoutMode::ABGR1555_Dither;

	if (bpp24)
	{
		if (render_state.scanout_mdec_filter == ScanoutFilter::MDEC_YUV)
			cmd->set_program(*pipelines.bpp24_yuv_quad_blitter);
		else
			cmd->set_program(*pipelines.bpp24_quad_blitter);
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestWrap);
	}
	else if (ssaa)
	{
		if (dither)
			cmd->set_program(*pipelines.unscaled_dither_quad_blitter);
		else
			cmd->set_program(*pipelines.unscaled_quad_blitter);

		cmd->set_texture(0, 0, framebuffer_ssaa->get_view(), StockSampler::NearestWrap);
	}
	else if (!render_state.adaptive_smoothing || scaling == 1)
	{
		if (dither)
			cmd->set_program(*pipelines.scaled_dither_quad_blitter);
		else
			cmd->set_program(*pipelines.scaled_quad_blitter);

		cmd->set_texture(0, 0, *scaled_views[0], StockSampler::LinearWrap);
	}
	else
	{
		if (dither)
			cmd->set_program(*pipelines.mipmap_dither_resolve);
		else
			cmd->set_program(*pipelines.mipmap_resolve);

		cmd->set_texture(0, 0, scaled_framebuffer->get_view(), StockSampler::TrilinearWrap);
		cmd->set_texture(0, 1, bias_framebuffer->get_view(), StockSampler::LinearWrap);
	}

	if (dither)
	{
		cmd->set_texture(0, 2, dither_lut->get_view(), StockSampler::NearestWrap);
		struct DitherData
		{
			float range;
			float inv_range;
			float dither_scale;
			int32_t dither_shift;
		};
		DitherData *dither = cmd->allocate_typed_constant_data<DitherData>(0, 3, 1);
		dither->range = 31.0f;
		dither->inv_range = 1.0f / 31.0f;
		dither->dither_scale = 1.0f;

		if (render_state.dither_native_resolution && scaled)
		{
			int32_t shift = 0;
			unsigned tmp = scaling >> 1;
			while (tmp)
			{
				shift++;
				tmp >>= 1;
			}
			dither->dither_shift = shift;
		}
		else
		{
			dither->dither_shift = 0;
		}
	}

	cmd->set_vertex_binding(0, *quad, 0, 8);
	struct Push
	{
		float offset[2];
		float scale[2];
		float uv_min[2];
		float uv_max[2];
		float max_bias;
	};
	Push push = { { float(rect.x) / FB_WIDTH, float(rect.y) / FB_HEIGHT },
		          { float(rect.width) / FB_WIDTH, float(rect.height) / FB_HEIGHT },
		          { (rect.x + 0.5f) / FB_WIDTH, (rect.y + 0.5f) / FB_HEIGHT },
		          { (rect.x + rect.width - 0.5f) / FB_WIDTH, (rect.y + rect.height - 0.5f) / FB_HEIGHT },
		          float(scaled_views.size() - 1) };

	cmd->push_constants(&push, 0, sizeof(push));
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	cmd->draw(4);

	cmd->end_render_pass();

	cmd->image_barrier(*reuseable_scanout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                   VK_ACCESS_SHADER_READ_BIT);

	last_scanout = reuseable_scanout;

	return reuseable_scanout;
}

void Renderer::hazard(StatusFlags flags)
{
	VkPipelineStageFlags src_stages = 0;
	VkAccessFlags src_access = 0;
	VkPipelineStageFlags dst_stages = 0;
	VkAccessFlags dst_access = 0;

	if (flags & (STATUS_FRAGMENT_FB_READ | STATUS_FRAGMENT_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
	if (flags & (STATUS_FRAGMENT_FB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
	}

	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	if (flags & (STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		src_access |= VK_ACCESS_SHADER_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
		              VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	if (flags & (STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (flags & (STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		src_access |= VK_ACCESS_TRANSFER_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
		              VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	// Invalidate render target caches.
	if (flags & (STATUS_TRANSFER_SFB_WRITE | STATUS_COMPUTE_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dst_access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		              VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	}

	// 24-bpp scanout hazard
	if (flags & STATUS_COMPUTE_FB_WRITE)
	{
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT;
	}

	dst_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;

	// If we have out-standing jobs in the compute pipe, issue them into cmdbuffer before injecting the barrier.
	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_READ | STATUS_COMPUTE_SFB_WRITE))
	{
		flush_blits();
		flush_resolves();
	}

	VK_ASSERT(src_stages);
	VK_ASSERT(dst_stages);
	ensure_command_buffer();
	cmd->barrier(src_stages, src_access, dst_stages, dst_access);
}

void Renderer::flush_resolves()
{
	struct Push
	{
		float inv_size[2];
		uint32_t scale;
	};

	if (!queue.scaled_resolves.empty())
	{
		ensure_command_buffer();
		cmd->set_program(*pipelines.resolve_to_scaled);

		cmd->set_texture(0, 1, framebuffer->get_view(), StockSampler::NearestClamp);
		if (msaa > 1)
			cmd->set_storage_texture(0, 0, scaled_framebuffer_msaa->get_view());
		else
			cmd->set_storage_texture(0, 0, *scaled_views[0]);

		unsigned size = queue.scaled_resolves.size();
		for (unsigned i = 0; i < size; i += 1024)
		{
			unsigned to_run = std::min(size - i, 1024u);

			Push push = { { 1.0f / (scaling * FB_WIDTH), 1.0f / (scaling * FB_HEIGHT) }, scaling };
			cmd->push_constants(&push, 0, sizeof(push));
			void *ptr = cmd->allocate_constant_data(1, 0, to_run * sizeof(VkRect2D));
			memcpy(ptr, queue.scaled_resolves.data() + i, to_run * sizeof(VkRect2D));
			cmd->dispatch(scaling, scaling, to_run);
		}
	}

	if (!queue.unscaled_resolves.empty())
	{
		ensure_command_buffer();
		// Always use nearest neighbor downscaling to avoid filter artifact (e.g. unwanted black outlines in Vagrant Story)
		// Supersampling will use a separate pass for downsampling
		cmd->set_specialization_constant(SpecConstIndex_Samples, msaa);
		cmd->set_specialization_constant(SpecConstIndex_FilterMode, 0);
		cmd->set_specialization_constant(SpecConstIndex_Scaling, scaling);
		cmd->set_program(*pipelines.resolve_to_unscaled);
		cmd->set_storage_texture(0, 0, framebuffer->get_view());
		if (msaa > 1)
			cmd->set_texture(0, 1, scaled_framebuffer_msaa->get_view(), StockSampler::NearestClamp);
		else
			cmd->set_texture(0, 1, *scaled_views[0], StockSampler::NearestClamp);

		unsigned size = queue.unscaled_resolves.size();
		for (unsigned i = 0; i < size; i += 1024)
		{
			unsigned to_run = std::min(size - i, 1024u);

			Push push = { { 1.0f / FB_WIDTH, 1.0f / FB_HEIGHT }, 1u };
			cmd->push_constants(&push, 0, sizeof(push));
			void *ptr = cmd->allocate_constant_data(1, 0, to_run * sizeof(VkRect2D));
			memcpy(ptr, queue.unscaled_resolves.data() + i, to_run * sizeof(VkRect2D));
			cmd->set_specialization_constant_mask(-1);
			cmd->dispatch(1, 1, to_run);
		}
	}

	queue.scaled_resolves.clear();
	queue.unscaled_resolves.clear();
}

void Renderer::resolve(Domain target_domain, unsigned x, unsigned y)
{
	if (target_domain == Domain::Scaled)
		queue.scaled_resolves.push_back({ { int(x), int(y) }, { BLOCK_WIDTH, BLOCK_HEIGHT } });
	else
		queue.unscaled_resolves.push_back({ { int(x), int(y) }, { BLOCK_WIDTH, BLOCK_HEIGHT } });
}

void Renderer::ensure_command_buffer()
{
	if (!cmd)
		cmd = device.request_command_buffer();
}

void Renderer::discard_render_pass()
{
	reset_queue();
}

float Renderer::allocate_depth(Domain domain, const Rect &rect)
{
	atlas.write_fragment(domain, rect);
	primitive_index++;
	return 1.0f - primitive_index * (4.0f / 0xffffff); // Double the epsilon to be safe(r) when w is used.
	//iCB: Doubled again for added safety, otherwise we get Z-fighting when drawing multi-pass blended primitives.
}

HdTextureHandle Renderer::get_hd_texture_index(const Rect &vram_rect, bool &fastpath_capable_out, bool &cache_hit_out) {
	UsedMode mode = {
		render_state.texture_mode,
		render_state.palette_offset_x,
		render_state.palette_offset_y
	};
	if (mode.mode == TextureMode::ABGR1555) {
		// HACK: This mode doesn't use a palette, so this a hack to make the palette irrelevant for equality purposes
		mode.palette_offset_x = 0;
		mode.palette_offset_y = 0;
	}
	if (texture_tracking_enabled) {
		return tracker.get_hd_texture_index(vram_rect, mode, render_state.texture_offset_x, render_state.texture_offset_y, fastpath_capable_out, cache_hit_out);
	} else {
		return HdTextureHandle::make_none();
	}
}

void Renderer::build_attribs(BufferVertex *output, const Vertex *vertices, unsigned count, HdTextureHandle &hd_texture_index,
	bool &filtering, bool &scaled_read, unsigned &shift, bool &offset_uv)
{
	switch (render_state.texture_mode)
	{
	case TextureMode::Palette4bpp:
		shift = 2;
		break;
	case TextureMode::Palette8bpp:
		shift = 1;
		break;
	default:
		shift = 0;
		break;
	}

	Rect hd_texture_vram(0, 0, 0, 0);

	if (render_state.texture_mode != TextureMode::None)
	{
		if (render_state.texture_window.mask_x == 0xffu && render_state.texture_window.mask_y == 0xffu)
		{
			unsigned min_u = render_state.UVLimits.min_u;
			unsigned min_v = render_state.UVLimits.min_v;
			unsigned max_u = render_state.UVLimits.max_u;
			unsigned max_v = render_state.UVLimits.max_v;
			unsigned width = max_u - min_u + 1;
			unsigned height = max_v - min_v + 1;

			if (max_u > 255 || max_v > 255) // Wraparound behavior, assume the whole page is hit.
			{
				atlas.set_texture_window({ 0, 0, 256u >> shift, 256 });
				hd_texture_vram = {
					render_state.texture_offset_x,
					render_state.texture_offset_y,
					256u >> shift,
					256,
				};
			}
			else
			{
				min_u >>= shift;
				max_u = (max_u + (1 << shift) - 1) >> shift;
				width = max_u - min_u + 1;
				atlas.set_texture_window({ min_u, min_v, width, height });

				hd_texture_vram = {
					render_state.texture_offset_x + min_u,
					render_state.texture_offset_y + min_v,

					// HDTODO: this might be wrong because it can result in Rect's with 0 width, also notice that height has the same +1
					width - 1, // This is -1 due to boundary shenanigans above (otherwise upload.rect.contains(snoop) would return false for the right-most tiles)
					
					height,
				};
			}
		}
		else
		{
			// If we have a masked texture window, assume this is the true rect we should use.
			Rect effective_rect = render_state.cached_window_rect;
			atlas.set_texture_window(
			    { effective_rect.x >> shift, effective_rect.y, effective_rect.width >> shift, effective_rect.height });
			hd_texture_vram = {
				render_state.texture_offset_x + (effective_rect.x >> shift),
				render_state.texture_offset_y + effective_rect.y,
				effective_rect.width >> shift,
				effective_rect.height,
			};
		}
	}

	// Compute bounding box for the draw call.
	float max_x = 0.0f;
	float max_y = 0.0f;
	float min_x = float(FB_WIDTH);
	float min_y = float(FB_HEIGHT);
	float x[4];
	float y[4];
	// Bounding box for texture
	unsigned max_u = 0.0f;
	unsigned max_v = 0.0f;
	unsigned min_u = FB_WIDTH;
	unsigned min_v = FB_HEIGHT;
	for (unsigned i = 0; i < count; i++)
	{
		float tmp_x = vertices[i].x + render_state.draw_offset_x;
		float tmp_y = vertices[i].y + render_state.draw_offset_y;
		max_x = std::max(max_x, tmp_x);
		max_y = std::max(max_y, tmp_y);
		min_x = std::min(min_x, tmp_x);
		min_y = std::min(min_y, tmp_y);
		x[i] = tmp_x;
		y[i] = tmp_y;

		if (render_state.texture_mode == TextureMode::ABGR1555)
		{
			unsigned tmp_u = vertices[i].u + render_state.texture_offset_x;
			unsigned tmp_v = vertices[i].v + render_state.texture_offset_y;
			max_u = std::max(max_u, tmp_u);
			max_v = std::max(max_v, tmp_v);
			min_u = std::min(min_u, tmp_u);
			min_v = std::min(min_v, tmp_v);
		}
	}

	// Clamp the rect.
	min_x = floorf(std::max(min_x, 0.0f));
	min_y = floorf(std::max(min_y, 0.0f));
	max_x = ceilf(std::min(max_x, float(FB_WIDTH)));
	max_y = ceilf(std::min(max_y, float(FB_HEIGHT)));

	const Rect rect = {
		unsigned(min_x), unsigned(min_y), unsigned(max_x) - unsigned(min_x), unsigned(max_y) - unsigned(min_y),
	};

	if (render_state.texture_mode == TextureMode::ABGR1555)
	{
		if (render_state.draw_rect.intersects(rect))
		{
			// HACK hd_texture_vram should contains the texture we are reading from in vram coordinate
			// avoid texture filtering and enable scaled read if the texture is rendered content
			bool texture_rendered = atlas.texture_rendered(hd_texture_vram);
			filtering = !texture_rendered;
			scaled_read = texture_rendered;
		}
		else
		{
			filtering = false;
			scaled_read = false;
		}
	}
	else
	{
		filtering = render_state.texture_mode != TextureMode::None;
		scaled_read = false;
	}
	offset_uv = scaled_uv_offset && render_state.primitive_type == PrimitiveType::Polygon;

	float z = allocate_depth(scaled_read ? Domain::Scaled : Domain::Unscaled, rect);

	// Look up the hd texture index
	// This is done here at the end of the function because the `allocate_depth`
	// call above can call `reset_queue` which would invalidate the HdTextureHandle
	int16_t param = int16_t(shift);
	if (hd_texture_vram.height > 0) { // This condition is just a dumb way to check that the rect was actually set to something
		bool fastpath_capable_out = false;
		bool cache_hit = false;
		hd_texture_index = get_hd_texture_index(hd_texture_vram, fastpath_capable_out, cache_hit);
		fastpath_capable_out = false;
		if (
			fastpath_capable_out &&
			render_state.texture_window.mask_x == 0xff && render_state.texture_window.mask_y == 0xff &&
			render_state.texture_window.or_x == 0x00 && render_state.texture_window.or_y == 0x00
		) {
			// All UVs are within a single hd texture, and there are no & or | shenanigans. Tell the shader to use the fast path.
			param = param | 0x100;
		}
		if (cache_hit) {
			param = param | 0x400; // dbg cache hit
		}
	}
	if (hd_texture_index == HdTextureHandle::make_none()) {
		// This flag says skip hd textures
		param = param | 0x200;
	}

	for (unsigned i = 0; i < count; i++)
	{
		output[i] = {
			x[i],
			y[i],
			z,
			vertices[i].w,
			vertices[i].color & 0xffffffu,
			render_state.texture_window,
			int16_t(render_state.palette_offset_x),
			int16_t(render_state.palette_offset_y),
			param,
			int16_t(vertices[i].u),
			int16_t(vertices[i].v),
			int16_t(render_state.texture_offset_x),
			int16_t(render_state.texture_offset_y),
			render_state.UVLimits.min_u,
			render_state.UVLimits.min_v,
			render_state.UVLimits.max_u,
			render_state.UVLimits.max_v,
		};

		if (render_state.texture_mode != TextureMode::None && !render_state.texture_color_modulate)
			output[i].color = 0x808080;

		output[i].color |= render_state.force_mask_bit ? 0xff000000u : 0u;
	}
}

std::vector<Renderer::BufferVertex> *Renderer::select_pipeline(unsigned prims, int scissor, HdTextureHandle hd_texture,
	bool filtering, bool scaled_read, unsigned shift, bool offset_uv)
{
	// For mask testing, force primitives through the serialized blend path.
	if (render_state.mask_test)
		return nullptr;

	if (filtering)
		filtering = !get_filer_exclude(FilterExcludeOpaque);

	if (render_state.texture_mode != TextureMode::None)
	{
		if (render_state.semi_transparent != SemiTransparentMode::None)
		{
			for (unsigned i = 0; i < prims; i++)
				queue.semi_transparent_opaque_scissor.emplace_back(queue.semi_transparent_opaque_scissor.size(), scissor, hd_texture,
					filtering, scaled_read, shift, offset_uv);
			return &queue.semi_transparent_opaque;
		}
		else
		{
			for (unsigned i = 0; i < prims; i++)
				queue.opaque_textured_scissor.emplace_back(queue.opaque_textured_scissor.size(), scissor, hd_texture,
					filtering, scaled_read, shift, offset_uv);
			return &queue.opaque_textured;
		}
	}
	else if (render_state.semi_transparent != SemiTransparentMode::None)
		return nullptr;
	else
	{
		for (unsigned i = 0; i < prims; i++)
			queue.opaque_scissor.emplace_back(queue.opaque_scissor.size(), scissor, hd_texture,
				filtering, scaled_read, shift, offset_uv);
		return &queue.opaque;
	}
}

void Renderer::build_line_quad(Vertex *output, const Vertex *input)
{
	const float dx = input[1].x - input[0].x;
	const float dy = input[1].y - input[0].y;
	if (dx == 0.0f && dy == 0.0f)
	{
		// Degenerate, render a point.
		output[0].x = input[0].x;
		output[0].y = input[0].y;
		output[1].x = input[0].x + 1.0f;
		output[1].y = input[0].y;
		output[2].x = input[1].x;
		output[2].y = input[1].y + 1.0f;
		output[3].x = input[1].x + 1.0f;
		output[3].y = input[1].y + 1.0f;

		uint32_t c = input[0].color;
		output[0].w = 1.0f;
		output[0].color = c;
		output[1].w = 1.0f;
		output[1].color = c;
		output[2].w = 1.0f;
		output[2].color = c;
		output[3].w = 1.0f;
		output[3].color = c;
		return;
	}

	const float abs_dx = fabsf(dx);
	const float abs_dy = fabsf(dy);
	float fill_dx, fill_dy;
	float dxdk, dydk;

	float pad_x0 = 0.0f;
	float pad_x1 = 0.0f;
	float pad_y0 = 0.0f;
	float pad_y1 = 0.0f;

	// Check for vertical or horizontal major lines.
	// When expanding to a rect, do so in the appropriate direction.
	// FIXME: This scheme seems to kinda work, but it seems very hard to find a method
	// that looks perfect on every game.
	// Vagrant Story speech bubbles are a very good test case here!
	if (abs_dx > abs_dy)
	{
		fill_dx = 0.0f;
		fill_dy = 1.0f;
		dxdk = 1.0f;
		dydk = dy / abs_dx;

		if (dx > 0.0f)
		{
			// Right
			pad_x1 = 1.0f;
			pad_y1 = dydk;
		}
		else
		{
			// Left
			pad_x0 = 1.0f;
			pad_y0 = -dydk;
		}
	}
	else
	{
		fill_dx = 1.0f;
		fill_dy = 0.0f;
		dydk = 1.0f;
		dxdk = dx / abs_dy;

		if (dy > 0.0f)
		{
			// Down
			pad_y1 = 1.0f;
			pad_x1 = dxdk;
		}
		else
		{
			// Up
			pad_y0 = 1.0f;
			pad_x0 = -dxdk;
		}
	}

	const float x0 = input[0].x + pad_x0;
	const float y0 = input[0].y + pad_y0;
	const float c0 = input[0].color;
	const float x1 = input[1].x + pad_x1;
	const float y1 = input[1].y + pad_y1;
	const float c1 = input[1].color;

	output[0].x = x0;
	output[0].y = y0;
	output[1].x = x0 + fill_dx;
	output[1].y = y0 + fill_dy;
	output[2].x = x1;
	output[2].y = y1;
	output[3].x = x1 + fill_dx;
	output[3].y = y1 + fill_dy;

	output[0].w = 1.0f;
	output[0].color = c0;
	output[1].w = 1.0f;
	output[1].color = c0;
	output[2].w = 1.0f;
	output[2].color = c1;
	output[3].w = 1.0f;
	output[3].color = c1;
}

void Renderer::draw_line(const Vertex *vertices)
{
	// We can move this to GPU, but means more draw calls and more pipeline swapping.
	// This should be plenty fast for the quite small amount of lines games render.
	Vertex vert[4];
	build_line_quad(vert, vertices);
	draw_quad(vert);
}

void Renderer::draw_triangle(const Vertex *vertices)
{
	if (!render_state.draw_rect.width || !render_state.draw_rect.height)
		return;

	last_scanout.reset();

	BufferVertex vert[3];
	HdTextureHandle hd_texture_index = HdTextureHandle::make_none();
	bool filtering = false;
	bool scaled_read = false;
	unsigned shift = 0;
	bool offset_uv = false;
	build_attribs(vert, vertices, 3, hd_texture_index, filtering, scaled_read, shift, offset_uv);
	const int scissor_index = queue.scissor_invariant ? -1 : int(queue.scissors.size() - 1);
	std::vector<BufferVertex> *out = select_pipeline(1, scissor_index, hd_texture_index, filtering, scaled_read, shift, offset_uv);
	if (out)
	{
		for (unsigned i = 0; i < 3; i++)
			out->push_back(vert[i]);
	}

	if (render_state.mask_test || render_state.semi_transparent != SemiTransparentMode::None)
	{
		if (filtering)
			filtering = !get_filer_exclude(FilterExcludeOpaqueAndSemiTrans);

		for (unsigned i = 0; i < 3; i++)
			queue.semi_transparent.push_back(vert[i]);
		queue.semi_transparent_state.push_back({ scissor_index, hd_texture_index, render_state.semi_transparent,
		                                         render_state.texture_mode != TextureMode::None,
		                                         render_state.mask_test,
		                                         filtering,
		                                         scaled_read,
												 shift,
												 offset_uv });

		// We've hit the dragon path, we'll need programmable blending for this render pass.
		// render_pass_is_feedback enables self dependency in renderpass which is necessary for barriers between draws.
		render_pass_is_feedback = true;
	}
}

void Renderer::draw_quad(const Vertex *vertices)
{
	if (!render_state.draw_rect.width || !render_state.draw_rect.height)
		return;

	last_scanout.reset();

	BufferVertex vert[4];
	// build_attribs may flush the queues, thus calling reset_queue and invalidating any pre-existing HdTextureHandle.
	// tracker.no_hd_texture uses a special index (-1) that is the only one valid across such events.
	// If in the future one were tempted to try to cache or reuse the last used HdTextureHandle here, they would have
	// to be very careful not to let it get invalidated by build_attribs; so any such logic should happen within
	// build_attribs itself, and not out here.
	HdTextureHandle hd_texture_index = HdTextureHandle::make_none();
	bool filtering = false;
	bool scaled_read = false;
	unsigned shift = 0;
	bool offset_uv = false;
	build_attribs(vert, vertices, 4, hd_texture_index, filtering, scaled_read, shift, offset_uv);
	const int scissor_index = queue.scissor_invariant ? -1 : int(queue.scissors.size() - 1);
	std::vector<BufferVertex> *out = select_pipeline(2, scissor_index, hd_texture_index, filtering, scaled_read, shift, offset_uv);

	if (out)
	{
		out->push_back(vert[0]);
		out->push_back(vert[1]);
		out->push_back(vert[2]);
		out->push_back(vert[3]);
		out->push_back(vert[2]);
		out->push_back(vert[1]);
	}

	if (render_state.mask_test || render_state.semi_transparent != SemiTransparentMode::None)
	{
		if (filtering)
			filtering = !get_filer_exclude(FilterExcludeOpaqueAndSemiTrans);

		const SemiTransparentState state = {
			scissor_index, hd_texture_index, render_state.semi_transparent,
			render_state.texture_mode != TextureMode::None,
			render_state.mask_test,
			filtering,
			scaled_read,
			shift,
			offset_uv };
		queue.semi_transparent.push_back(vert[0]);
		queue.semi_transparent.push_back(vert[1]);
		queue.semi_transparent.push_back(vert[2]);
		queue.semi_transparent.push_back(vert[3]);
		queue.semi_transparent.push_back(vert[2]);
		queue.semi_transparent.push_back(vert[1]);
		queue.semi_transparent_state.push_back(state);
		queue.semi_transparent_state.push_back(state);

		// We've hit the dragon path, we'll need programmable blending for this render pass.
		render_pass_is_feedback = true;
	}
}

void Renderer::clear_quad(const Rect &rect, uint32_t fb_color, bool candidate)
{
	last_scanout.reset();
	TextureMode old = atlas.set_texture_mode(TextureMode::None);
	float z = allocate_depth(Domain::Unscaled, rect);
	atlas.set_texture_mode(old);

	BufferVertex pos0 = { float(rect.x), float(rect.y), z, 1.0f, fbcolor_to_rgba8(fb_color) };
	BufferVertex pos1 = { float(rect.x) + float(rect.width), float(rect.y), z, 1.0f, fbcolor_to_rgba8(fb_color) };
	BufferVertex pos2 = { float(rect.x), float(rect.y) + float(rect.height), z, 1.0f, fbcolor_to_rgba8(fb_color) };
	BufferVertex pos3 = { float(rect.x) + float(rect.width), float(rect.y) + float(rect.height), z, 1.0f,
		                  fbcolor_to_rgba8(fb_color) };
	queue.opaque.push_back(pos0);
	queue.opaque.push_back(pos1);
	queue.opaque.push_back(pos2);
	queue.opaque.push_back(pos3);
	queue.opaque.push_back(pos2);
	queue.opaque.push_back(pos1);
	queue.opaque_scissor.emplace_back(queue.opaque_scissor.size());
	queue.opaque_scissor.emplace_back(queue.opaque_scissor.size());

	if (candidate)
		queue.clear_candidates.push_back({ rect, fb_color, z });
}

const Renderer::ClearCandidate *Renderer::find_clear_candidate(const Rect &rect) const
{
	const ClearCandidate *ret = nullptr;
	for (const ClearCandidate &c : queue.clear_candidates)
	{
		if (c.rect == rect)
			ret = &c;
	}
	return ret;
}

void Renderer::flush_render_pass(const Rect &rect)
{
	ensure_command_buffer();

	RenderPassInfo info = {};

	if (msaa > 1)
		info.color_attachments[0] = &scaled_framebuffer_msaa->get_view();
	else
		info.color_attachments[0] = scaled_views.front().get();

	info.clear_depth_stencil = { 1.0f, 0 };
	info.depth_stencil =
		&device.get_transient_attachment(FB_WIDTH * scaling, FB_HEIGHT * scaling,
		                                 device.get_default_depth_format(), 0, msaa, 1);
	info.num_color_attachments = 1;
	info.store_attachments = 1 << 0;
	info.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;

	RenderPassInfo::Subpass subpass;
	info.num_subpasses = 1;
	info.subpasses = &subpass;
	subpass.num_color_attachments = 1;

	const ClearCandidate *clear_candidate = find_clear_candidate(rect);

	subpass.color_attachments[0] = 0;
	if (render_pass_is_feedback)
	{
		subpass.num_input_attachments = 1;
		subpass.input_attachments[0] = 0;
	}

	if (clear_candidate)
	{
		info.clear_depth_stencil.depth = clear_candidate->z;
		fbcolor_to_rgba32f(info.clear_color[0].float32, clear_candidate->color);
		info.clear_attachments = 1 << 0;
	}
	else
	{
		info.load_attachments = 1 << 0;
	}


	info.render_area.offset = { int(rect.x * scaling), int(rect.y * scaling) };
	info.render_area.extent = { rect.width * scaling, rect.height * scaling };

	cmd->begin_render_pass(info);
	cmd->set_scissor(info.render_area);
	queue.default_scissor = info.render_area;
	cmd->set_texture(0, 2, dither_lut->get_view(), StockSampler::NearestWrap);

	render_opaque_primitives();
	render_opaque_texture_primitives();
	render_semi_transparent_opaque_texture_primitives();
	render_semi_transparent_primitives();

	cmd->end_render_pass();

	// Render passes are implicitly synchronized.
	cmd->image_barrier(msaa > 1 ? *scaled_framebuffer_msaa : *scaled_framebuffer,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT);

	reset_queue();
}

void Renderer::dispatch_set_scaled_read_texture(bool scaled_read, bool textured)
{
	if (scaled_read)
	{
		if (msaa > 1)
			cmd->set_texture(0, 0, scaled_framebuffer_msaa->get_view(), StockSampler::NearestClamp);
		else
			cmd->set_texture(0, 0, *scaled_views[0], StockSampler::NearestClamp);
	}
	else
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);
	if (textured)
	{
		if (scaled_read)
			cmd->set_program(*pipelines.textured_scaled);
		else
			cmd->set_program(*pipelines.textured_unscaled);
	}
	else
	{
		cmd->set_program(*pipelines.flat);
	}
}

bool Renderer::primitive_info_sort_gt(const PrimitiveInfo &a, const PrimitiveInfo &b)
{
	if (a.offset_uv != b.offset_uv)
		return a.offset_uv > b.offset_uv;
	if (a.shift != b.shift)
		return a.shift > b.shift;
	if (a.scaled_read != b.scaled_read)
		return a.scaled_read > b.scaled_read;
	if (a.filtering != b.filtering)
		return a.filtering > b.filtering;
	if (a.hd_texture_index != b.hd_texture_index)
		return a.hd_texture_index > b.hd_texture_index;
	if (a.scissor_index != b.scissor_index)
		return a.scissor_index > b.scissor_index;
	return a.triangle_index > b.triangle_index;
}

void Renderer::dispatch(const std::vector<BufferVertex> &vertices, std::vector<PrimitiveInfo> &scissors, bool textured)
{
	std::sort(scissors.begin(), scissors.end(), primitive_info_sort_gt);

	// Render flat-shaded primitives.
	BufferVertex *vert = static_cast<BufferVertex *>(
	    cmd->allocate_vertex_data(0, vertices.size() * sizeof(BufferVertex), sizeof(BufferVertex)));

	int scissor = scissors.front().scissor_index;
	HdTextureHandle hd_texture = scissors.front().hd_texture_index;
	bool filtering = scissors.front().filtering;
	bool scaled_read = scissors.front().scaled_read;
	unsigned shift = scissors.front().shift;
	bool offset_uv = scissors.front().offset_uv;
	unsigned last_draw = 0;
	unsigned i = 1;
	unsigned size = scissors.size();

	hd_texture_uniforms(hd_texture);
	cmd->set_scissor(scissor < 0 ? queue.default_scissor : queue.scissors[scissor]);
	cmd->set_specialization_constant(SpecConstIndex_FilterMode, filtering ? primitive_filter_mode : FilterMode::NearestNeighbor);
	cmd->set_specialization_constant(SpecConstIndex_Shift, shift);
	cmd->set_specialization_constant(SpecConstIndex_OffsetUV, (int)offset_uv);
	dispatch_set_scaled_read_texture(scaled_read, textured);
	memcpy(vert, vertices.data() + 3 * scissors.front().triangle_index, 3 * sizeof(BufferVertex));
	vert += 3;

	for (; i < size; i++, vert += 3)
	{
		if (scissors[i].scissor_index != scissor || scissors[i].hd_texture_index != hd_texture ||
			scissors[i].filtering != filtering || scissors[i].scaled_read != scaled_read || scissors[i].shift != shift ||
			scissors[i].offset_uv != offset_uv)
		{
			unsigned to_draw = i - last_draw;
			cmd->set_specialization_constant_mask(-1);
			cmd->draw(3 * to_draw, 1, 3 * last_draw, 0);
			last_draw = i;

			if (scissors[i].scissor_index != scissor) {
				scissor = scissors[i].scissor_index;
				cmd->set_scissor(scissor < 0 ? queue.default_scissor : queue.scissors[scissor]);
			}
			if (scissors[i].hd_texture_index != hd_texture) {
				hd_texture = scissors[i].hd_texture_index;
				hd_texture_uniforms(hd_texture);
			}
			if (scissors[i].filtering != filtering) {
				filtering = scissors[i].filtering;
				cmd->set_specialization_constant(SpecConstIndex_FilterMode, filtering ? primitive_filter_mode : FilterMode::NearestNeighbor);
			}
			if (scissors[i].scaled_read != scaled_read) {
				scaled_read = scissors[i].scaled_read;
				dispatch_set_scaled_read_texture(scaled_read, textured);
			}
			if (scissors[i].shift != shift) {
				shift = scissors[i].shift;
				cmd->set_specialization_constant(SpecConstIndex_Shift, shift);
			}
			if (scissors[i].offset_uv != offset_uv) {
				offset_uv = scissors[i].offset_uv;
				cmd->set_specialization_constant(SpecConstIndex_OffsetUV, (int)offset_uv);
			}
		}
		memcpy(vert, vertices.data() + 3 * scissors[i].triangle_index, 3 * sizeof(BufferVertex));
	}

	unsigned to_draw = size - last_draw;
	cmd->set_specialization_constant_mask(-1);
	cmd->draw(3 * to_draw, 1, 3 * last_draw, 0);
}

void Renderer::render_opaque_primitives()
{
	std::vector<BufferVertex> &vertices = queue.opaque;
	std::vector<PrimitiveInfo> &scissors = queue.opaque_scissor;
	if (vertices.empty())
		return;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	dispatch(vertices, scissors);
}

void Renderer::hd_texture_uniforms(HdTextureHandle hd_texture_index) {
	HdTexture hd = tracker.get_hd_texture(hd_texture_index);
	cmd->set_texture(0, 4, hd.texture->get_view(), StockSampler::TrilinearClamp); // Type of sampler only matters for the fast path
	// ivec4, ivec4
	struct HDPush {
		int32_t vram_rect_x;
		int32_t vram_rect_y;
		int32_t vram_rect_width;
		int32_t vram_rect_height;

		int32_t texel_rect_x;
		int32_t texel_rect_y;
		int32_t texel_rect_width;
		int32_t texel_rect_height;
	};
	HDPush push = {
		hd.vram_rect.x, hd.vram_rect.y, hd.vram_rect.width, hd.vram_rect.height,
		hd.texel_rect.x, hd.texel_rect.y, hd.texel_rect.width, hd.texel_rect.height
	};
	cmd->push_constants(&push, 0, sizeof(push));
}

void Renderer::render_semi_transparent_primitives()
{
	unsigned prims = queue.semi_transparent_state.size();
	if (!prims)
		return;

	unsigned last_draw_offset = 0;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_depth_test(true, false);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	cmd->set_vertex_attrib(3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x));
	cmd->set_vertex_attrib(4, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, u));
	cmd->set_vertex_attrib(5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(BufferVertex, min_u));

	size_t size = queue.semi_transparent.size() * sizeof(BufferVertex);
	void *verts = cmd->allocate_vertex_data(0, size, sizeof(BufferVertex));
	memcpy(verts, queue.semi_transparent.data(), size);

	SemiTransparentState last_state = queue.semi_transparent_state[0];

	semi_transparent_set_state(last_state);

	// These pixels are blended, so we have to render them in-order.
	// Batch up as long as we can.
	for (unsigned i = 1; i < prims; i++)
	{
		// If we need programmable shading, we can't batch as primitives may overlap.
		// We could in theory do some fancy tests here, but probably overkill here.
		if ((last_state.masked && last_state.semi_transparent != SemiTransparentMode::None) ||
		    (last_state != queue.semi_transparent_state[i]))
		{
			unsigned to_draw = i - last_draw_offset;
			cmd->set_specialization_constant_mask(-1);

			{
				VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
				vkCmdPipelineBarrier(cmd->get_command_buffer(),
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_DEPENDENCY_BY_REGION_BIT,
					1, &barrier, 0, nullptr, 0, nullptr);
			}

			cmd->draw(to_draw * 3, 1, last_draw_offset * 3, 0);
			if (msaa > 1)
				cmd->set_multisample_state(false);
			last_draw_offset = i;

			last_state = queue.semi_transparent_state[i];
			semi_transparent_set_state(last_state);
		}
	}

	unsigned to_draw = prims - last_draw_offset;
	cmd->set_specialization_constant_mask(-1);
	cmd->draw(to_draw * 3, 1, last_draw_offset * 3, 0);
	if (msaa > 1)
		cmd->set_multisample_state(false);
}

void Renderer::render_semi_transparent_opaque_texture_primitives()
{
	std::vector<BufferVertex> &vertices = queue.semi_transparent_opaque;
	std::vector<PrimitiveInfo> &scissors = queue.semi_transparent_opaque_scissor;
	if (vertices.empty())
		return;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_specialization_constant(SpecConstIndex_TransMode, TransMode::SemiTransOpaque);
	cmd->set_specialization_constant(SpecConstIndex_Scaling, scaling);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	cmd->set_vertex_attrib(3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x));
	cmd->set_vertex_attrib(4, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, u));
	cmd->set_vertex_attrib(5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(BufferVertex, min_u));

	dispatch(vertices, scissors, true);
}

void Renderer::render_opaque_texture_primitives()
{
	std::vector<BufferVertex> &vertices = queue.opaque_textured;
	std::vector<PrimitiveInfo> &scissors = queue.opaque_textured_scissor;
	if (vertices.empty())
		return;

	cmd->set_opaque_state();
	cmd->set_cull_mode(VK_CULL_MODE_NONE);
	cmd->set_depth_compare(VK_COMPARE_OP_LESS);
	cmd->set_specialization_constant(SpecConstIndex_TransMode, TransMode::Opaque);
	cmd->set_specialization_constant(SpecConstIndex_Scaling, scaling);
	cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	cmd->set_vertex_attrib(1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	cmd->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	cmd->set_vertex_attrib(3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x)); // Pad to support AMD
	cmd->set_vertex_attrib(4, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, u));
	cmd->set_vertex_attrib(5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(BufferVertex, min_u));

	dispatch(vertices, scissors, true);
}

void Renderer::flush_blits()
{
	ensure_command_buffer();
	flush_blit(queue.scaled_blits, *pipelines.blit_vram_scaled, true);
	flush_blit(queue.scaled_masked_blits, *pipelines.blit_vram_scaled_masked, true);
	flush_blit(queue.unscaled_blits, *pipelines.blit_vram_unscaled, false);
	flush_blit(queue.unscaled_masked_blits, *pipelines.blit_vram_unscaled_masked, false);
	queue.scaled_blits.clear();
	queue.scaled_masked_blits.clear();
	queue.unscaled_blits.clear();
	queue.unscaled_masked_blits.clear();
}

void Renderer::flush_blit(const std::vector<BlitInfo> &infos, Program &program, bool scaled)
{
	if (infos.empty())
		return;

	cmd->set_program(program);

	if (scaled)
	{
		if (msaa > 1)
		{
			cmd->set_storage_texture(0, 0, scaled_framebuffer_msaa->get_view());
			cmd->set_texture(0, 1, scaled_framebuffer_msaa->get_view(), StockSampler::NearestClamp);
		}
		else
		{
			cmd->set_storage_texture(0, 0, *scaled_views[0]);
			cmd->set_texture(0, 1, *scaled_views[0], StockSampler::NearestClamp);
		}
	}
	else
	{
		cmd->set_storage_texture(0, 0, framebuffer->get_view());
		cmd->set_texture(0, 1, framebuffer->get_view(), StockSampler::NearestClamp);
	}

	unsigned size = infos.size();
	unsigned scale = scaled ? scaling : 1u;
	for (unsigned i = 0; i < size; i += 512)
	{
		unsigned to_blit = std::min(size - i, 512u);
		void *ptr = cmd->allocate_constant_data(1, 0, to_blit * sizeof(BlitInfo));
		memcpy(ptr, infos.data() + i, to_blit * sizeof(BlitInfo));
		cmd->dispatch(scale, scale, to_blit);
	}
}

void Renderer::blit_vram(const Rect &dst, const Rect &src)
{
	VK_ASSERT(dst.width == src.width);
	VK_ASSERT(dst.height == src.height);

	// Happens a lot in Square games for some reason.
	if (dst == src)
		return;

	if (dst.width == 0 || dst.height == 0)
		return;

#ifndef NDEBUG
	TT_LOG_VERBOSE(RETRO_LOG_INFO,
		"blit_vram(dst={%i, %i, %i x %i}, src={%i, %i, %i x %i}).\n", dst.x, dst.y, dst.width, dst.height, src.x, src.y, src.width, src.height
	);
#endif
	last_scanout.reset();
	Domain domain = atlas.blit_vram(dst, src);

	if (texture_tracking_enabled) {
		tracker.blit(dst, src);
	}

	if (dst.intersects(src))
	{
		// The software implementation takes texture cache into account by copying 128 horizontal pixels at a time ...
		// We can do it with compute.
		ensure_command_buffer();

		unsigned factor = domain == Domain::Scaled ? scaling : 1u;

		// Slower path where we do this in a single workgroup which steps through line by line, just like the software version.
		struct Push
		{
			uint32_t src_offset[2];
			uint32_t dst_offset[2];
			uint32_t extent[2];
			int32_t scaling;
		};
		Push push = {
			{ src.x, src.y }, { dst.x, dst.y }, { dst.width, dst.height }, int(factor),
		};
		cmd->push_constants(&push, 0, sizeof(push));

		if (domain == Domain::Scaled)
		{
			if (msaa > 1)
			{
				cmd->set_storage_texture(0, 0, scaled_framebuffer_msaa->get_view());
				cmd->set_program(render_state.mask_test ?
						*pipelines.blit_vram_msaa_cached_scaled_masked :
						*pipelines.blit_vram_msaa_cached_scaled);
				cmd->dispatch(factor, factor, msaa);
			}
			else
			{
				cmd->set_storage_texture(0, 0, *scaled_views[0]);
				cmd->set_program(render_state.mask_test ? *pipelines.blit_vram_cached_scaled_masked :
														*pipelines.blit_vram_cached_scaled);
				cmd->dispatch(factor, factor, 1);
			}
		}
		else
		{
			cmd->set_storage_texture(0, 0, framebuffer->get_view());
			cmd->set_program(render_state.mask_test ? *pipelines.blit_vram_cached_unscaled_masked :
													*pipelines.blit_vram_cached_unscaled);
			cmd->dispatch(factor, factor, 1);
		}
		//LOG("Intersecting blit_vram, hitting slow path (src: %u, %u, dst: %u, %u, size: %u, %u)\n", src.x, src.y, dst.x,
		//    dst.y, dst.width, dst.height);
	}
	else
	{
		if (domain == Domain::Scaled)
		{
			std::vector<BlitInfo> &q = render_state.mask_test ? queue.scaled_masked_blits : queue.scaled_blits;
			unsigned width = dst.width;
			unsigned height = dst.height;
			for (unsigned y = 0; y < height; y += BLOCK_HEIGHT)
				for (unsigned x = 0; x < width; x += BLOCK_WIDTH)
					for (unsigned s = 0; s < msaa; s++)
						q.push_back({
							{ (x + src.x) * scaling, (y + src.y) * scaling },
							{ (x + dst.x) * scaling, (y + dst.y) * scaling },
							{ std::min(BLOCK_WIDTH, width - x) * scaling, std::min(BLOCK_HEIGHT, height - y) * scaling },
							render_state.force_mask_bit ? 0x8000u : 0u, s,
						});
		}
		else
		{
			std::vector<BlitInfo> &q = render_state.mask_test ? queue.unscaled_masked_blits : queue.unscaled_blits;
			unsigned width = dst.width;
			unsigned height = dst.height;
			for (unsigned y = 0; y < height; y += BLOCK_HEIGHT)
				for (unsigned x = 0; x < width; x += BLOCK_WIDTH)
					q.push_back({
					    { x + src.x, y + src.y },
					    { x + dst.x, y + dst.y },
					    { std::min(BLOCK_WIDTH, width - x), std::min(BLOCK_HEIGHT, height - y) },
						render_state.force_mask_bit ? 0x8000u : 0u, 0,
					});
		}
	}
}

Vulkan::ImageHandle Renderer::upload_texture(std::vector<LoadedImage> &levels) {
	ImageCreateInfo info;
	info.width = levels[0].width;
	info.height = levels[0].height;
	info.depth = 1;
	info.levels = levels.size();
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.type = VK_IMAGE_TYPE_2D;
	info.layers = 1;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.flags = 0;
	info.misc = 0u;
	info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	std::vector<ImageInitialData> initial;
	for (int i = 0; i < levels.size(); i++) {
		initial.push_back({ levels[i].owned_data.data() });
	}

	ImageHandle image = device.create_image(info, initial.data());
	return image;
}
Vulkan::ImageHandle Renderer::create_texture(int width, int height, int levels) {
	ImageCreateInfo info = ImageCreateInfo::immutable_2d_image(width, height, VK_FORMAT_R8G8B8A8_UNORM, false);
	info.levels = levels;
	info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	ImageHandle image = device.create_image(info, nullptr);
	return image;
}
Vulkan::CommandBufferHandle &Renderer::command_buffer_hack_fixme() {
	ensure_command_buffer();
	return cmd;
}

void Renderer::notify_texture_upload(Rect uploadRect, uint16_t *vram) {
	if (texture_tracking_enabled) {
		tracker.upload(uploadRect, vram);
	}
}
void Renderer::set_track_textures(bool enable) {
	texture_tracking_enabled = enable;
}
void Renderer::set_dump_textures(bool enable) {
	tracker.dump_enabled = enable;
}
void Renderer::set_replace_textures(bool enable) {
	tracker.hd_textures_enabled = enable;
}

uint16_t *Renderer::begin_copy(BufferHandle handle)
{
	return static_cast<uint16_t *>(device.map_host_buffer(*handle, MEMORY_ACCESS_WRITE_BIT));
}

void Renderer::end_copy(BufferHandle handle)
{
	device.unmap_host_buffer(*handle, MEMORY_ACCESS_WRITE_BIT);
}

BufferHandle Renderer::copy_cpu_to_vram(const Rect &rect)
{
	last_scanout.reset();
	atlas.load_image(rect);
	VkDeviceSize size = rect.width * rect.height * sizeof(uint16_t);

	// TODO: Chain allocate this.
	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain::Host;
	buffer_create_info.size = size;
	buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	BufferHandle buffer = device.create_buffer(buffer_create_info, nullptr);

	BufferViewCreateInfo view_info = {};
	view_info.buffer = buffer.get();

	struct Push
	{
		Rect rect;
		uint32_t offset;
		uint32_t mask_or;
	};

	ensure_command_buffer();
	cmd->set_program(render_state.mask_test ? *pipelines.copy_to_vram_masked : *pipelines.copy_to_vram);
	cmd->set_storage_texture(0, 0, framebuffer->get_view());

	// Vulkan minimum limit, for large buffer views, split up the work.
	if (rect.width * rect.height > device.get_gpu_properties().limits.maxTexelBufferElements)
	{
		for (unsigned y = 0; y < rect.height; y += BLOCK_HEIGHT)
		{
			unsigned y_size = std::min(rect.height - y, BLOCK_HEIGHT);
			view_info.offset = y * rect.width * sizeof(uint16_t);
			view_info.range = y_size * rect.width * sizeof(uint16_t);
			view_info.format = VK_FORMAT_R16_UINT;
			BufferViewHandle view = device.create_buffer_view(view_info);

			Rect small_rect = { rect.x, rect.y + y, rect.width, y_size };

			cmd->set_buffer_view(0, 1, *view);
			Push push = { small_rect, 0, render_state.force_mask_bit ? 0x8000u : 0u };
			cmd->push_constants(&push, 0, sizeof(push));
			cmd->dispatch((small_rect.width + 7) >> 3, (small_rect.height + 7) >> 3, 1);
		}
	}
	else
	{
		view_info.offset = 0;
		view_info.range = size;
		view_info.format = VK_FORMAT_R16_UINT;
		BufferViewHandle view = device.create_buffer_view(view_info);

		cmd->set_buffer_view(0, 1, *view);

		Push push = { rect, 0, render_state.force_mask_bit ? 0x8000u : 0u };
		cmd->push_constants(&push, 0, sizeof(push));

		// TODO: Batch up work.
		cmd->dispatch((rect.width + 7) >> 3, (rect.height + 7) >> 3, 1);
	}

	return buffer;
}

Renderer::~Renderer()
{
	flush();
}

void Renderer::reset_scissor_queue()
{
	queue.scissors.clear();
	Rect &rect = render_state.draw_rect;
	queue.scissors.push_back(
	    { { int(rect.x * scaling), int(rect.y * scaling) }, { rect.width * scaling, rect.height * scaling } });
}

void Renderer::reset_queue()
{
	queue.opaque.clear();
	queue.opaque_scissor.clear();
	queue.opaque_textured.clear();
	queue.opaque_textured_scissor.clear();
	queue.textures.clear();
	queue.semi_transparent.clear();
	queue.semi_transparent_state.clear();
	queue.semi_transparent_opaque.clear();
	queue.semi_transparent_opaque_scissor.clear();
	queue.clear_candidates.clear();
	primitive_index = 0;
	render_pass_is_feedback = false;

	reset_scissor_queue();

	if (texture_tracking_enabled) {
		tracker.on_queues_reset();
	}
}

void Renderer::semi_transparent_set_state(const SemiTransparentState &state)
{
	if (state.scaled_read)
	{
		if (msaa > 1)
			cmd->set_texture(0, 0, scaled_framebuffer_msaa->get_view(), StockSampler::NearestClamp);
		else
			cmd->set_texture(0, 0, *scaled_views[0], StockSampler::NearestClamp);
	}
	else
		cmd->set_texture(0, 0, framebuffer->get_view(), StockSampler::NearestClamp);
	hd_texture_uniforms(state.hd_texture_index);
	cmd->set_specialization_constant(SpecConstIndex_FilterMode, state.filtering ? primitive_filter_mode : FilterMode::NearestNeighbor);
	cmd->set_specialization_constant(SpecConstIndex_Scaling, scaling);
	cmd->set_specialization_constant(SpecConstIndex_Shift, state.shift);
	cmd->set_specialization_constant(SpecConstIndex_OffsetUV, (int)state.offset_uv);

	if (state.scissor_index < 0)
		cmd->set_scissor(queue.default_scissor);
	else
		cmd->set_scissor(queue.scissors[state.scissor_index]);

	Program &textured = state.textured ? state.scaled_read ?
		*pipelines.textured_scaled : *pipelines.textured_unscaled : *pipelines.flat;
	Program &textured_masked = state.textured ? state.scaled_read ?
		*pipelines.textured_masked_scaled : *pipelines.textured_masked_unscaled : *pipelines.flat_masked;

	switch (state.semi_transparent)
	{
	case SemiTransparentMode::None:
	{
		// For opaque primitives which are just masked, we can make use of fixed function blending.
		cmd->set_blend_enable(true);
		cmd->set_specialization_constant(SpecConstIndex_TransMode, TransMode::Opaque);
		cmd->set_program(textured);
		cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
		cmd->set_blend_factors(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
		                       VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA);
		break;
	}
	case SemiTransparentMode::Add:
	{
		if (state.masked)
		{
			cmd->set_specialization_constant(SpecConstIndex_BlendMode, BlendMode::BlendAdd);
			cmd->set_program(textured_masked);
			cmd->pixel_barrier();
			cmd->set_input_attachments(0, 3);
			cmd->set_blend_enable(false);
			if (msaa > 1)
			{
				// Need to blend per-sample.
				cmd->set_multisample_state(false, false, true);
			}
			cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ONE);
		}
		else
		{
			cmd->set_specialization_constant(SpecConstIndex_TransMode, TransMode::SemiTrans);
			cmd->set_program(textured);
			cmd->set_blend_enable(true);
			cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ZERO);
		}
		break;
	}
	case SemiTransparentMode::Average:
	{
		if (state.masked)
		{
			cmd->set_specialization_constant(SpecConstIndex_BlendMode, BlendMode::BlendAvg);
			cmd->set_program(textured_masked);
			cmd->set_input_attachments(0, 3);
			cmd->pixel_barrier();
			cmd->set_blend_enable(false);
			if (msaa > 1)
			{
				// Need to blend per-sample.
				cmd->set_multisample_state(false, false, true);
			}
			cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ONE);
		}
		else
		{
			static const float rgba[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
			cmd->set_specialization_constant(SpecConstIndex_TransMode, TransMode::SemiTrans);
			cmd->set_program(textured);
			cmd->set_blend_enable(true);
			cmd->set_blend_constants(rgba);
			cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_FACTOR_ZERO);
		}
		break;
	}
	case SemiTransparentMode::Sub:
	{
		if (state.masked)
		{
			cmd->set_specialization_constant(SpecConstIndex_BlendMode, BlendMode::BlendSub);
			cmd->set_program(textured_masked);
			cmd->set_input_attachments(0, 3);
			cmd->pixel_barrier();
			cmd->set_blend_enable(false);
			if (msaa > 1)
			{
				// Need to blend per-sample.
				cmd->set_multisample_state(false, false, true);
			}
			cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ONE);
		}
		else
		{
			cmd->set_specialization_constant(SpecConstIndex_TransMode, TransMode::SemiTrans);
			cmd->set_program(textured);
			cmd->set_blend_enable(true);
			cmd->set_blend_op(VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ZERO);
		}
		break;
	}
	case SemiTransparentMode::AddQuarter:
	{
		if (state.masked)
		{
			cmd->set_specialization_constant(SpecConstIndex_BlendMode, BlendMode::BlendAddQuarter);
			cmd->set_program(textured_masked);
			cmd->set_input_attachments(0, 3);
			cmd->pixel_barrier();
			cmd->set_blend_enable(false);
			if (msaa > 1)
			{
				// Need to blend per-sample.
				cmd->set_multisample_state(false, false, true);
			}
			cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ONE);
		}
		else
		{
			static const float rgba[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
			cmd->set_specialization_constant(SpecConstIndex_TransMode, TransMode::SemiTrans);
			cmd->set_program(textured);
			cmd->set_blend_enable(true);
			cmd->set_blend_constants(rgba);
			cmd->set_blend_op(VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			cmd->set_blend_factors(VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ZERO);
		}
		break;
	}
	}
}
}

/* ============================================================
 *
 * Folded content from the parallel-psx/vulkan/ and
 * parallel-psx/atlas/ source files, in dependency order.
 *
 * ============================================================ */


/* === cookie.cpp === */


namespace Vulkan
{
Cookie::Cookie(Device *device)
    : cookie(device->allocate_cookie())
{
}
}

/* === texture_format.cpp === */


namespace Vulkan
{
uint32_t TextureFormatLayout::num_miplevels(uint32_t width, uint32_t height, uint32_t depth)
{
	uint32_t wh = width > height ? width : height;
	uint32_t size = wh > depth ? wh : depth;
	uint32_t levels = 0;
	while (size)
	{
		levels++;
		size >>= 1;
	}
	return levels;
}

void TextureFormatLayout::format_block_dim(VkFormat format, uint32_t &width, uint32_t &height)
{
#define fmt(x, w, h)     \
    case VK_FORMAT_##x: \
        width = w; \
        height = h; \
        break

	switch (format)
	{
	fmt(ETC2_R8G8B8A8_UNORM_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8A8_SRGB_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8A1_UNORM_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8A1_SRGB_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8_UNORM_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8_SRGB_BLOCK, 4, 4);
	fmt(EAC_R11_UNORM_BLOCK, 4, 4);
	fmt(EAC_R11_SNORM_BLOCK, 4, 4);
	fmt(EAC_R11G11_UNORM_BLOCK, 4, 4);
	fmt(EAC_R11G11_SNORM_BLOCK, 4, 4);

	fmt(BC1_RGB_UNORM_BLOCK, 4, 4);
	fmt(BC1_RGB_SRGB_BLOCK, 4, 4);
	fmt(BC1_RGBA_UNORM_BLOCK, 4, 4);
	fmt(BC1_RGBA_SRGB_BLOCK, 4, 4);
	fmt(BC2_UNORM_BLOCK, 4, 4);
	fmt(BC2_SRGB_BLOCK, 4, 4);
	fmt(BC3_UNORM_BLOCK, 4, 4);
	fmt(BC3_SRGB_BLOCK, 4, 4);
	fmt(BC4_UNORM_BLOCK, 4, 4);
	fmt(BC4_SNORM_BLOCK, 4, 4);
	fmt(BC5_UNORM_BLOCK, 4, 4);
	fmt(BC5_SNORM_BLOCK, 4, 4);
	fmt(BC6H_UFLOAT_BLOCK, 4, 4);
	fmt(BC6H_SFLOAT_BLOCK, 4, 4);
	fmt(BC7_SRGB_BLOCK, 4, 4);
	fmt(BC7_UNORM_BLOCK, 4, 4);

	fmt(ASTC_4x4_SRGB_BLOCK, 4, 4);
	fmt(ASTC_5x4_SRGB_BLOCK, 5, 4);
	fmt(ASTC_5x5_SRGB_BLOCK, 5, 5);
	fmt(ASTC_6x5_SRGB_BLOCK, 6, 5);
	fmt(ASTC_6x6_SRGB_BLOCK, 6, 6);
	fmt(ASTC_8x5_SRGB_BLOCK, 8, 5);
	fmt(ASTC_8x6_SRGB_BLOCK, 8, 6);
	fmt(ASTC_8x8_SRGB_BLOCK, 8, 8);
	fmt(ASTC_10x5_SRGB_BLOCK, 10, 5);
	fmt(ASTC_10x6_SRGB_BLOCK, 10, 6);
	fmt(ASTC_10x8_SRGB_BLOCK, 10, 8);
	fmt(ASTC_10x10_SRGB_BLOCK, 10, 10);
	fmt(ASTC_12x10_SRGB_BLOCK, 12, 10);
	fmt(ASTC_12x12_SRGB_BLOCK, 12, 12);
	fmt(ASTC_4x4_UNORM_BLOCK, 4, 4);
	fmt(ASTC_5x4_UNORM_BLOCK, 5, 4);
	fmt(ASTC_5x5_UNORM_BLOCK, 5, 5);
	fmt(ASTC_6x5_UNORM_BLOCK, 6, 5);
	fmt(ASTC_6x6_UNORM_BLOCK, 6, 6);
	fmt(ASTC_8x5_UNORM_BLOCK, 8, 5);
	fmt(ASTC_8x6_UNORM_BLOCK, 8, 6);
	fmt(ASTC_8x8_UNORM_BLOCK, 8, 8);
	fmt(ASTC_10x5_UNORM_BLOCK, 10, 5);
	fmt(ASTC_10x6_UNORM_BLOCK, 10, 6);
	fmt(ASTC_10x8_UNORM_BLOCK, 10, 8);
	fmt(ASTC_10x10_UNORM_BLOCK, 10, 10);
	fmt(ASTC_12x10_UNORM_BLOCK, 12, 10);
	fmt(ASTC_12x12_UNORM_BLOCK, 12, 12);

	default:
		width = 1;
		height = 1;
		break;
	}

#undef fmt
}

uint32_t TextureFormatLayout::format_block_size(VkFormat format)
{
#define fmt(x, bpp)     \
    case VK_FORMAT_##x: \
        return bpp
	switch (format)
	{
	fmt(R4G4_UNORM_PACK8, 1);
	fmt(R4G4B4A4_UNORM_PACK16, 2);
	fmt(B4G4R4A4_UNORM_PACK16, 2);
	fmt(R5G6B5_UNORM_PACK16, 2);
	fmt(B5G6R5_UNORM_PACK16, 2);
	fmt(R5G5B5A1_UNORM_PACK16, 2);
	fmt(B5G5R5A1_UNORM_PACK16, 2);
	fmt(A1R5G5B5_UNORM_PACK16, 2);
	fmt(R8_UNORM, 1);
	fmt(R8_SNORM, 1);
	fmt(R8_USCALED, 1);
	fmt(R8_SSCALED, 1);
	fmt(R8_UINT, 1);
	fmt(R8_SINT, 1);
	fmt(R8_SRGB, 1);
	fmt(R8G8_UNORM, 2);
	fmt(R8G8_SNORM, 2);
	fmt(R8G8_USCALED, 2);
	fmt(R8G8_SSCALED, 2);
	fmt(R8G8_UINT, 2);
	fmt(R8G8_SINT, 2);
	fmt(R8G8_SRGB, 2);
	fmt(R8G8B8_UNORM, 3);
	fmt(R8G8B8_SNORM, 3);
	fmt(R8G8B8_USCALED, 3);
	fmt(R8G8B8_SSCALED, 3);
	fmt(R8G8B8_UINT, 3);
	fmt(R8G8B8_SINT, 3);
	fmt(R8G8B8_SRGB, 3);
	fmt(R8G8B8A8_UNORM, 4);
	fmt(R8G8B8A8_SNORM, 4);
	fmt(R8G8B8A8_USCALED, 4);
	fmt(R8G8B8A8_SSCALED, 4);
	fmt(R8G8B8A8_UINT, 4);
	fmt(R8G8B8A8_SINT, 4);
	fmt(R8G8B8A8_SRGB, 4);
	fmt(B8G8R8A8_UNORM, 4);
	fmt(B8G8R8A8_SNORM, 4);
	fmt(B8G8R8A8_USCALED, 4);
	fmt(B8G8R8A8_SSCALED, 4);
	fmt(B8G8R8A8_UINT, 4);
	fmt(B8G8R8A8_SINT, 4);
	fmt(B8G8R8A8_SRGB, 4);
	fmt(A8B8G8R8_UNORM_PACK32, 4);
	fmt(A8B8G8R8_SNORM_PACK32, 4);
	fmt(A8B8G8R8_USCALED_PACK32, 4);
	fmt(A8B8G8R8_SSCALED_PACK32, 4);
	fmt(A8B8G8R8_UINT_PACK32, 4);
	fmt(A8B8G8R8_SINT_PACK32, 4);
	fmt(A8B8G8R8_SRGB_PACK32, 4);
	fmt(A2B10G10R10_UNORM_PACK32, 4);
	fmt(A2B10G10R10_SNORM_PACK32, 4);
	fmt(A2B10G10R10_USCALED_PACK32, 4);
	fmt(A2B10G10R10_SSCALED_PACK32, 4);
	fmt(A2B10G10R10_UINT_PACK32, 4);
	fmt(A2B10G10R10_SINT_PACK32, 4);
	fmt(A2R10G10B10_UNORM_PACK32, 4);
	fmt(A2R10G10B10_SNORM_PACK32, 4);
	fmt(A2R10G10B10_USCALED_PACK32, 4);
	fmt(A2R10G10B10_SSCALED_PACK32, 4);
	fmt(A2R10G10B10_UINT_PACK32, 4);
	fmt(A2R10G10B10_SINT_PACK32, 4);
	fmt(R16_UNORM, 2);
	fmt(R16_SNORM, 2);
	fmt(R16_USCALED, 2);
	fmt(R16_SSCALED, 2);
	fmt(R16_UINT, 2);
	fmt(R16_SINT, 2);
	fmt(R16_SFLOAT, 2);
	fmt(R16G16_UNORM, 4);
	fmt(R16G16_SNORM, 4);
	fmt(R16G16_USCALED, 4);
	fmt(R16G16_SSCALED, 4);
	fmt(R16G16_UINT, 4);
	fmt(R16G16_SINT, 4);
	fmt(R16G16_SFLOAT, 4);
	fmt(R16G16B16_UNORM, 6);
	fmt(R16G16B16_SNORM, 6);
	fmt(R16G16B16_USCALED, 6);
	fmt(R16G16B16_SSCALED, 6);
	fmt(R16G16B16_UINT, 6);
	fmt(R16G16B16_SINT, 6);
	fmt(R16G16B16_SFLOAT, 6);
	fmt(R16G16B16A16_UNORM, 8);
	fmt(R16G16B16A16_SNORM, 8);
	fmt(R16G16B16A16_USCALED, 8);
	fmt(R16G16B16A16_SSCALED, 8);
	fmt(R16G16B16A16_UINT, 8);
	fmt(R16G16B16A16_SINT, 8);
	fmt(R16G16B16A16_SFLOAT, 8);
	fmt(R32_UINT, 4);
	fmt(R32_SINT, 4);
	fmt(R32_SFLOAT, 4);
	fmt(R32G32_UINT, 8);
	fmt(R32G32_SINT, 8);
	fmt(R32G32_SFLOAT, 8);
	fmt(R32G32B32_UINT, 12);
	fmt(R32G32B32_SINT, 12);
	fmt(R32G32B32_SFLOAT, 12);
	fmt(R32G32B32A32_UINT, 16);
	fmt(R32G32B32A32_SINT, 16);
	fmt(R32G32B32A32_SFLOAT, 16);
	fmt(R64_UINT, 8);
	fmt(R64_SINT, 8);
	fmt(R64_SFLOAT, 8);
	fmt(R64G64_UINT, 16);
	fmt(R64G64_SINT, 16);
	fmt(R64G64_SFLOAT, 16);
	fmt(R64G64B64_UINT, 24);
	fmt(R64G64B64_SINT, 24);
	fmt(R64G64B64_SFLOAT, 24);
	fmt(R64G64B64A64_UINT, 32);
	fmt(R64G64B64A64_SINT, 32);
	fmt(R64G64B64A64_SFLOAT, 32);
	fmt(B10G11R11_UFLOAT_PACK32, 4);
	fmt(E5B9G9R9_UFLOAT_PACK32, 4);
	fmt(D16_UNORM, 2);
	fmt(X8_D24_UNORM_PACK32, 4);
	fmt(D32_SFLOAT, 4);
	fmt(S8_UINT, 1);
	fmt(D16_UNORM_S8_UINT, 3); // Doesn't make sense.
	fmt(D24_UNORM_S8_UINT, 4);
	fmt(D32_SFLOAT_S8_UINT, 5); // Doesn't make sense.

		// ETC2
	fmt(ETC2_R8G8B8A8_UNORM_BLOCK, 16);
	fmt(ETC2_R8G8B8A8_SRGB_BLOCK, 16);
	fmt(ETC2_R8G8B8A1_UNORM_BLOCK, 8);
	fmt(ETC2_R8G8B8A1_SRGB_BLOCK, 8);
	fmt(ETC2_R8G8B8_UNORM_BLOCK, 8);
	fmt(ETC2_R8G8B8_SRGB_BLOCK, 8);
	fmt(EAC_R11_UNORM_BLOCK, 8);
	fmt(EAC_R11_SNORM_BLOCK, 8);
	fmt(EAC_R11G11_UNORM_BLOCK, 16);
	fmt(EAC_R11G11_SNORM_BLOCK, 16);

		// BC
	fmt(BC1_RGB_UNORM_BLOCK, 8);
	fmt(BC1_RGB_SRGB_BLOCK, 8);
	fmt(BC1_RGBA_UNORM_BLOCK, 8);
	fmt(BC1_RGBA_SRGB_BLOCK, 8);
	fmt(BC2_UNORM_BLOCK, 16);
	fmt(BC2_SRGB_BLOCK, 16);
	fmt(BC3_UNORM_BLOCK, 16);
	fmt(BC3_SRGB_BLOCK, 16);
	fmt(BC4_UNORM_BLOCK, 8);
	fmt(BC4_SNORM_BLOCK, 8);
	fmt(BC5_UNORM_BLOCK, 16);
	fmt(BC5_SNORM_BLOCK, 16);
	fmt(BC6H_UFLOAT_BLOCK, 16);
	fmt(BC6H_SFLOAT_BLOCK, 16);
	fmt(BC7_SRGB_BLOCK, 16);
	fmt(BC7_UNORM_BLOCK, 16);

		// ASTC
	fmt(ASTC_4x4_SRGB_BLOCK, 16);
	fmt(ASTC_5x4_SRGB_BLOCK, 16);
	fmt(ASTC_5x5_SRGB_BLOCK, 16);
	fmt(ASTC_6x5_SRGB_BLOCK, 16);
	fmt(ASTC_6x6_SRGB_BLOCK, 16);
	fmt(ASTC_8x5_SRGB_BLOCK, 16);
	fmt(ASTC_8x6_SRGB_BLOCK, 16);
	fmt(ASTC_8x8_SRGB_BLOCK, 16);
	fmt(ASTC_10x5_SRGB_BLOCK, 16);
	fmt(ASTC_10x6_SRGB_BLOCK, 16);
	fmt(ASTC_10x8_SRGB_BLOCK, 16);
	fmt(ASTC_10x10_SRGB_BLOCK, 16);
	fmt(ASTC_12x10_SRGB_BLOCK, 16);
	fmt(ASTC_12x12_SRGB_BLOCK, 16);
	fmt(ASTC_4x4_UNORM_BLOCK, 16);
	fmt(ASTC_5x4_UNORM_BLOCK, 16);
	fmt(ASTC_5x5_UNORM_BLOCK, 16);
	fmt(ASTC_6x5_UNORM_BLOCK, 16);
	fmt(ASTC_6x6_UNORM_BLOCK, 16);
	fmt(ASTC_8x5_UNORM_BLOCK, 16);
	fmt(ASTC_8x6_UNORM_BLOCK, 16);
	fmt(ASTC_8x8_UNORM_BLOCK, 16);
	fmt(ASTC_10x5_UNORM_BLOCK, 16);
	fmt(ASTC_10x6_UNORM_BLOCK, 16);
	fmt(ASTC_10x8_UNORM_BLOCK, 16);
	fmt(ASTC_10x10_UNORM_BLOCK, 16);
	fmt(ASTC_12x10_UNORM_BLOCK, 16);
	fmt(ASTC_12x12_UNORM_BLOCK, 16);

	default:
		assert(0 && "Unknown format.");
		return 0;
	}
#undef fmt
}

void TextureFormatLayout::fill_mipinfo(uint32_t width, uint32_t height, uint32_t depth)
{
	block_stride = format_block_size(format);
	format_block_dim(format, block_dim_x, block_dim_y);

	if (mip_levels == 0)
		mip_levels = num_miplevels(width, height, depth);

	size_t offset = 0;

	for (uint32_t mip = 0; mip < mip_levels; mip++)
	{
		offset = (offset + 15) & ~15;

		uint32_t blocks_x = (width + block_dim_x - 1) / block_dim_x;
		uint32_t blocks_y = (height + block_dim_y - 1) / block_dim_y;
		size_t mip_size = blocks_x * blocks_y * array_layers * depth * block_stride;

		mips[mip].offset = offset;

		mips[mip].block_row_length = blocks_x;
		mips[mip].block_image_height = blocks_y;

		mips[mip].row_length = blocks_x * block_dim_x;
		mips[mip].image_height = blocks_y * block_dim_y;

		mips[mip].width = width;
		mips[mip].height = height;
		mips[mip].depth = depth;

		offset += mip_size;

		uint32_t next_w = width >> 1u;
		uint32_t next_h = height >> 1u;
		uint32_t next_d = depth >> 1u;
		width = next_w > 1u ? next_w : 1u;
		height = next_h > 1u ? next_h : 1u;
		depth = next_d > 1u ? next_d : 1u;
	}

	required_size = offset;
}

void TextureFormatLayout::set_1d(VkFormat format, uint32_t width, uint32_t array_layers, uint32_t mip_levels)
{
	this->image_type = VK_IMAGE_TYPE_1D;
	this->format = format;
	this->array_layers = array_layers;
	this->mip_levels = mip_levels;

	fill_mipinfo(width, 1, 1);
}

void TextureFormatLayout::set_2d(VkFormat format, uint32_t width, uint32_t height, uint32_t array_layers, uint32_t mip_levels)
{
	this->image_type = VK_IMAGE_TYPE_2D;
	this->format = format;
	this->array_layers = array_layers;
	this->mip_levels = mip_levels;

	fill_mipinfo(width, height, 1);
}

void TextureFormatLayout::set_3d(VkFormat format, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_levels)
{
	this->image_type = VK_IMAGE_TYPE_3D;
	this->format = format;
	this->array_layers = 1;
	this->mip_levels = mip_levels;

	fill_mipinfo(width, height, depth);
}

void TextureFormatLayout::set_buffer(void *buffer, size_t size)
{
	this->buffer = static_cast<uint8_t *>(buffer);
	buffer_size = size;
}

uint32_t TextureFormatLayout::get_width(uint32_t mip) const
{
	return mips[mip].width;
}

uint32_t TextureFormatLayout::get_height(uint32_t mip) const
{
	return mips[mip].height;
}

uint32_t TextureFormatLayout::get_depth(uint32_t mip) const
{
	return mips[mip].depth;
}

VkFormat TextureFormatLayout::get_format() const
{
	return format;
}

size_t TextureFormatLayout::get_required_size() const
{
	return required_size;
}

const TextureFormatLayout::MipInfo &TextureFormatLayout::get_mip_info(uint32_t mip) const
{
	return mips[mip];
}

size_t TextureFormatLayout::row_byte_stride(uint32_t row_length) const
{
	return ((row_length + block_dim_x - 1) / block_dim_x) * block_stride;
}

size_t TextureFormatLayout::layer_byte_stride(uint32_t image_height, size_t row_byte_stride) const
{
	return ((image_height + block_dim_y - 1) / block_dim_y) * row_byte_stride;
}

void TextureFormatLayout::build_buffer_image_copies(VkBufferImageCopy *copies, unsigned &num_copies) const
{
	assert(mip_levels <= 16);
	num_copies = mip_levels;
	for (unsigned level = 0; level < mip_levels; level++)
	{
		const MipInfo &mip_info = mips[level];

		VkBufferImageCopy &blit = copies[level];
		blit = {};
		blit.bufferOffset = mip_info.offset;
		blit.bufferRowLength = mip_info.row_length;
		blit.bufferImageHeight = mip_info.image_height;
		blit.imageSubresource.aspectMask = format_to_aspect_mask(format);
		blit.imageSubresource.mipLevel = level;
		blit.imageSubresource.baseArrayLayer = 0;
		blit.imageSubresource.layerCount = array_layers;
		blit.imageExtent.width = mip_info.width;
		blit.imageExtent.height = mip_info.height;
		blit.imageExtent.depth = mip_info.depth;
	}
}

}

/* === sampler.cpp === */


namespace Vulkan
{
Sampler::Sampler(Device *device, VkSampler sampler)
    : Cookie(device)
    , device(device)
    , sampler(sampler)
{
}

Sampler::~Sampler()
{
	if (sampler)
		device->destroy_sampler_nolock(sampler);
}

void SamplerDeleter::operator()(Vulkan::Sampler *sampler)
{
	sampler->device->handle_pool.samplers.free(sampler);
}
}

/* === buffer.cpp === */


namespace Vulkan
{
Buffer::Buffer(Device *device, VkBuffer buffer, const DeviceAllocation &alloc, const BufferCreateInfo &info)
    : Cookie(device)
    , device(device)
    , buffer(buffer)
    , alloc(alloc)
    , info(info)
{
}

Buffer::~Buffer()
{
	device->destroy_buffer_nolock(buffer);
	device->free_memory_nolock(alloc);
}

void BufferDeleter::operator()(Buffer *buffer)
{
	buffer->device->handle_pool.buffers.free(buffer);
}

BufferView::BufferView(Device *device, VkBufferView view, const BufferViewCreateInfo &create_info)
    : Cookie(device)
    , device(device)
    , view(view)
    , info(create_info)
{
}

BufferView::~BufferView()
{
	if (view != VK_NULL_HANDLE)
		device->destroy_buffer_view_nolock(view);
}

void BufferViewDeleter::operator()(BufferView *view)
{
	view->device->handle_pool.buffer_views.free(view);
}

}

/* === image.cpp === */


namespace Vulkan
{

ImageView::ImageView(Device *device, VkImageView view, const ImageViewCreateInfo &info)
    : Cookie(device)
    , device(device)
    , view(view)
    , info(info)
{
}

VkImageView ImageView::get_render_target_view(unsigned layer) const
{
	// Transient images just have one layer.
	if (info.image->get_create_info().domain == ImageDomain::Transient)
		return view;

	VK_ASSERT(layer < get_create_info().layers);

	if (render_target_views.empty())
		return view;
	else
	{
		VK_ASSERT(layer < render_target_views.size());
		return render_target_views[layer];
	}
}

ImageView::~ImageView()
{
	device->destroy_image_view_nolock(view);
	if (depth_view != VK_NULL_HANDLE)
		device->destroy_image_view_nolock(depth_view);
	if (stencil_view != VK_NULL_HANDLE)
		device->destroy_image_view_nolock(stencil_view);

	for (VkImageView &view : render_target_views)
		device->destroy_image_view_nolock(view);
}

Image::Image(Device *device, VkImage image, VkImageView default_view, const DeviceAllocation &alloc,
             const ImageCreateInfo &create_info)
    : Cookie(device)
    , device(device)
    , image(image)
    , alloc(alloc)
    , create_info(create_info)
{
	if (default_view != VK_NULL_HANDLE)
	{
		ImageViewCreateInfo info;
		info.image = this;
		info.format = create_info.format;
		info.base_level = 0;
		info.levels = create_info.levels;
		info.base_layer = 0;
		info.layers = create_info.layers;
		view = ImageViewHandle(device->handle_pool.image_views.allocate(device, default_view, info));
	}
}

Image::~Image()
{
	if (alloc.get_memory())
	{
		device->destroy_image_nolock(image);
		device->free_memory_nolock(alloc);
	}
}

void ImageViewDeleter::operator()(ImageView *view)
{
	view->device->handle_pool.image_views.free(view);
}

void ImageDeleter::operator()(Image *image)
{
	image->device->handle_pool.images.free(image);
}
}

/* === fence.cpp === */


namespace Vulkan
{
FenceHolder::~FenceHolder()
{
	if (fence != VK_NULL_HANDLE)
		device->reset_fence(fence);
}

void FenceHolder::wait()
{
	if (vkWaitForFences(device->get_device(), 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		LOGE("Failed to wait for fence!\n");
}

void FenceHolderDeleter::operator()(Vulkan::FenceHolder *fence)
{
	fence->device->handle_pool.fences.free(fence);
}
}

/* === fence_manager.cpp === */


namespace Vulkan
{
void FenceManager::init(VkDevice device)
{
	this->device = device;
}

VkFence FenceManager::request_cleared_fence()
{
	if (!fences.empty())
	{
		VkFence ret = fences.back();
		fences.pop_back();
		return ret;
	}
	else
	{
		VkFence fence;
		VkFenceCreateInfo info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		vkCreateFence(device, &info, nullptr, &fence);
		return fence;
	}
}

void FenceManager::recycle_fence(VkFence fence)
{
	fences.push_back(fence);
}

FenceManager::~FenceManager()
{
	for (VkFence &fence : fences)
		vkDestroyFence(device, fence, nullptr);
}
}

/* === semaphore.cpp === */


namespace Vulkan
{
SemaphoreHolder::~SemaphoreHolder()
{
	if (semaphore)
	{
		if (is_signalled())
			device->destroy_semaphore_nolock(semaphore);
		else
			device->recycle_semaphore_nolock(semaphore);
	}
}

void SemaphoreHolderDeleter::operator()(Vulkan::SemaphoreHolder *semaphore)
{
	semaphore->device->handle_pool.semaphores.free(semaphore);
}
}

/* === semaphore_manager.cpp === */


namespace Vulkan
{
void SemaphoreManager::init(VkDevice device)
{
	this->device = device;
}

SemaphoreManager::~SemaphoreManager()
{
	for (VkSemaphore &sem : semaphores)
		vkDestroySemaphore(device, sem, nullptr);
}

void SemaphoreManager::recycle(VkSemaphore sem)
{
	if (sem != VK_NULL_HANDLE)
		semaphores.push_back(sem);
}

VkSemaphore SemaphoreManager::request_cleared_semaphore()
{
	if (semaphores.empty())
	{
		VkSemaphore semaphore;
		VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		vkCreateSemaphore(device, &info, nullptr, &semaphore);
		return semaphore;
	}
	else
	{
		VkSemaphore sem = semaphores.back();
		semaphores.pop_back();
		return sem;
	}
}
}

/* === buffer_pool.cpp === */


namespace Vulkan
{
void BufferPool::init(Device *device, VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage)
{
	this->device = device;
	this->block_size = block_size;
	this->alignment = alignment;
	this->usage = usage;
}

BufferBlock::~BufferBlock()
{
}

void BufferPool::reset()
{
	blocks.clear();
}

BufferBlock BufferPool::allocate_block(VkDeviceSize size)
{
	BufferBlock block;

	BufferCreateInfo info;
	info.domain = BufferDomain::Host;
	info.size = size;
	info.usage = usage;

	block.gpu = device->create_buffer(info, nullptr);
	device->set_name(*block.gpu, "chain-allocated-block-gpu");

	// Try to map it, will fail unless the memory is host visible.
	block.mapped = static_cast<uint8_t *>(device->map_host_buffer(*block.gpu, MEMORY_ACCESS_WRITE_BIT));
	if (!block.mapped)
	{
		// Fall back to host memory, and remember to sync to gpu on submission time using DMA queue. :)
		BufferCreateInfo cpu_info;
		cpu_info.domain = BufferDomain::Host;
		cpu_info.size = size;
		cpu_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		block.cpu = device->create_buffer(cpu_info, nullptr);
		device->set_name(*block.cpu, "chain-allocated-block-cpu");
		block.mapped = static_cast<uint8_t *>(device->map_host_buffer(*block.cpu, MEMORY_ACCESS_WRITE_BIT));
	}
	else
		block.cpu = block.gpu;

	block.offset = 0;
	block.alignment = alignment;
	block.size = size;
	return block;
}

BufferBlock BufferPool::request_block(VkDeviceSize minimum_size)
{
	if ((minimum_size > block_size) || blocks.empty())
	{
		VkDeviceSize alloc_size = block_size > minimum_size ? block_size : minimum_size;
		return allocate_block(alloc_size);
	}
	else
	{
		BufferBlock back = std::move(blocks.back());
		blocks.pop_back();

		back.mapped = static_cast<uint8_t *>(device->map_host_buffer(*back.cpu, MEMORY_ACCESS_WRITE_BIT));
		back.offset = 0;
		return back;
	}
}

void BufferPool::recycle_block(BufferBlock &&block)
{
	VK_ASSERT(block.size == block_size);
	blocks.push_back(std::move(block));
}

BufferPool::~BufferPool()
{
	VK_ASSERT(blocks.empty());
}

}

/* === command_pool.cpp === */


namespace Vulkan
{
CommandPool::CommandPool(VkDevice device, uint32_t queue_family_index)
    : device(device)
{
	VkCommandPoolCreateInfo info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	info.queueFamilyIndex = queue_family_index;
	vkCreateCommandPool(device, &info, nullptr, &pool);
}

CommandPool::CommandPool(CommandPool &&other) noexcept
{
	*this = std::move(other);
}

CommandPool &CommandPool::operator=(CommandPool &&other) noexcept
{
	if (this != &other)
	{
		device = other.device;

		// Free our owned resources first.
		if (!buffers.empty())
			vkFreeCommandBuffers(device, pool, buffers.size(), buffers.data());
		if (pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, pool, nullptr);

		// Adopt other's resources.
		pool = other.pool;
		buffers = std::move(other.buffers);
		index = other.index;
#ifdef VULKAN_DEBUG
		in_flight = std::move(other.in_flight);
#endif

		// Leave other in a destructor-safe state (no double-free).
		other.pool = VK_NULL_HANDLE;
		other.index = 0;
		other.buffers.clear();
#ifdef VULKAN_DEBUG
		other.in_flight.clear();
#endif
	}
	return *this;
}

CommandPool::~CommandPool()
{
	if (!buffers.empty())
		vkFreeCommandBuffers(device, pool, buffers.size(), buffers.data());
	if (pool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, pool, nullptr);
}

void CommandPool::signal_submitted(VkCommandBuffer cmd)
{
#ifdef VULKAN_DEBUG
	VK_ASSERT(in_flight.find(cmd) != end(in_flight));
	in_flight.erase(cmd);
#else
	(void)cmd;
#endif
}

VkCommandBuffer CommandPool::request_command_buffer()
{
	if (index < buffers.size())
	{
		VkCommandBuffer ret = buffers[index++];
#ifdef VULKAN_DEBUG
		VK_ASSERT(in_flight.find(ret) == end(in_flight));
		in_flight.insert(ret);
#endif
		return ret;
	}
	else
	{
		VkCommandBuffer cmd;
		VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		info.commandPool = pool;
		info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		info.commandBufferCount = 1;

		vkAllocateCommandBuffers(device, &info, &cmd);
#ifdef VULKAN_DEBUG
		VK_ASSERT(in_flight.find(cmd) == end(in_flight));
		in_flight.insert(cmd);
#endif
		buffers.push_back(cmd);
		index++;
		return cmd;
	}
}

void CommandPool::begin()
{
#ifdef VULKAN_DEBUG
	VK_ASSERT(in_flight.empty());
#endif
	if (index > 0)
		vkResetCommandPool(device, pool, 0);
	index = 0;
}
}

/* === memory_allocator.cpp === */


#define ALLOCATOR_LOCK()

namespace Vulkan
{

void DeviceAllocation::free_immediate()
{
	if (!alloc)
		return;

	alloc->free(this);
	alloc = nullptr;
	base = VK_NULL_HANDLE;
	mask = 0;
	offset = 0;
}

void DeviceAllocation::free_immediate(DeviceAllocator &allocator)
{
	if (alloc)
		free_immediate();
	else if (base)
	{
		allocator.free_no_recycle(size, memory_type, base, host_base);
		base = VK_NULL_HANDLE;
	}
}

void DeviceAllocation::free_global(DeviceAllocator &allocator, uint32_t size, uint32_t memory_type)
{
	if (base)
	{
		allocator.free(size, memory_type, base, host_base);
		base = VK_NULL_HANDLE;
		mask = 0;
		offset = 0;
	}
}

void Block::allocate(uint32_t num_blocks, DeviceAllocation *block)
{
	VK_ASSERT(NumSubBlocks >= num_blocks);
	VK_ASSERT(num_blocks != 0);

	uint32_t block_mask;
	if (num_blocks == NumSubBlocks)
		block_mask = ~0u;
	else
		block_mask = ((1u << num_blocks) - 1u);

	uint32_t mask = free_blocks[num_blocks - 1];
	uint32_t b = trailing_zeroes(mask);

	VK_ASSERT(((free_blocks[0] >> b) & block_mask) == block_mask);

	uint32_t sb = block_mask << b;
	free_blocks[0] &= ~sb;
	update_longest_run();

	block->mask = sb;
	block->offset = b;
}

void Block::free(uint32_t mask)
{
	VK_ASSERT((free_blocks[0] & mask) == 0);
	free_blocks[0] |= mask;
	update_longest_run();
}

void ClassAllocator::suballocate(uint32_t num_blocks, uint32_t tiling, uint32_t memory_type, MiniHeap &heap,
                                 DeviceAllocation *alloc)
{
	heap.heap.allocate(num_blocks, alloc);
	alloc->base = heap.allocation.base;
	alloc->offset <<= sub_block_size_log2;

	if (heap.allocation.host_base)
		alloc->host_base = heap.allocation.host_base + alloc->offset;

	alloc->offset += heap.allocation.offset;
	alloc->tiling = tiling;
	alloc->memory_type = memory_type;
	alloc->alloc = this;
	alloc->size = num_blocks << sub_block_size_log2;
}

bool ClassAllocator::allocate(uint32_t size, AllocationTiling tiling, DeviceAllocation *alloc, bool hierarchical)
{
	ALLOCATOR_LOCK();
	unsigned num_blocks = (size + sub_block_size - 1) >> sub_block_size_log2;
	uint32_t size_mask = (1u << (num_blocks - 1)) - 1;
	uint32_t masked_tiling_mode = tiling_mask & tiling;
	AllocationTilingHeaps &m = tiling_modes[masked_tiling_mode];

	uint32_t index = trailing_zeroes(m.heap_availability_mask & ~size_mask);

	if (index < Block::NumSubBlocks)
	{
		Util::IntrusiveList<MiniHeap>::Iterator itr = m.heaps[index].begin();
		VK_ASSERT(itr);
		VK_ASSERT(index >= (num_blocks - 1));

		MiniHeap &heap = *itr;
		suballocate(num_blocks, masked_tiling_mode, memory_type, heap, alloc);
		unsigned new_index = heap.heap.get_longest_run() - 1;

		if (heap.heap.full())
		{
			m.full_heaps.move_to_front(m.heaps[index], itr);
			if (!m.heaps[index].begin())
				m.heap_availability_mask &= ~(1u << index);
		}
		else if (new_index != index)
		{
			Util::IntrusiveList<MiniHeap> &new_heap = m.heaps[new_index];
			new_heap.move_to_front(m.heaps[index], itr);
			m.heap_availability_mask |= 1u << new_index;
			if (!m.heaps[index].begin())
				m.heap_availability_mask &= ~(1u << index);
		}

		alloc->heap = itr;
		alloc->hierarchical = hierarchical;

		return true;
	}

	// We didn't find a vacant heap, make a new one.
	MiniHeap *node = object_pool.allocate();
	if (!node)
		return false;

	MiniHeap &heap = *node;
	uint32_t alloc_size = sub_block_size * Block::NumSubBlocks;

	if (parent)
	{
		// We cannot allocate a new block from parent ... This is fatal.
		if (!parent->allocate(alloc_size, tiling, &heap.allocation, true))
		{
			object_pool.free(node);
			return false;
		}
	}
	else
	{
		heap.allocation.offset = 0;
		if (!global_allocator->allocate(alloc_size, memory_type, &heap.allocation.base, &heap.allocation.host_base,
		                                VK_NULL_HANDLE))
		{
			object_pool.free(node);
			return false;
		}
	}

	// This cannot fail.
	suballocate(num_blocks, masked_tiling_mode, memory_type, heap, alloc);

	alloc->heap = node;
	if (heap.heap.full())
	{
		m.full_heaps.insert_front(node);
	}
	else
	{
		unsigned new_index = heap.heap.get_longest_run() - 1;
		m.heaps[new_index].insert_front(node);
		m.heap_availability_mask |= 1u << new_index;
	}

	alloc->hierarchical = hierarchical;

	return true;
}

ClassAllocator::~ClassAllocator()
{
	bool error = false;
	for (AllocationTilingHeaps &m : tiling_modes)
	{
		if (m.full_heaps.begin())
			error = true;

		for (Util::IntrusiveList<MiniHeap> &h : m.heaps)
			if (h.begin())
				error = true;
	}

	if (error)
		LOGE("Memory leaked in class allocator!\n");
}

void ClassAllocator::free(DeviceAllocation *alloc)
{
	ALLOCATOR_LOCK();
	MiniHeap *heap = &*alloc->heap;
	Block &block = heap->heap;
	bool was_full = block.full();
	AllocationTilingHeaps &m = tiling_modes[alloc->tiling];

	unsigned index = block.get_longest_run() - 1;
	block.free(alloc->mask);
	unsigned new_index = block.get_longest_run() - 1;

	if (block.empty())
	{
		// Our mini-heap is completely freed, free to higher level allocator.
		if (parent)
			heap->allocation.free_immediate();
		else
			heap->allocation.free_global(*global_allocator, sub_block_size * Block::NumSubBlocks, memory_type);

		if (was_full)
			m.full_heaps.erase(heap);
		else
		{
			m.heaps[index].erase(heap);
			if (!m.heaps[index].begin())
				m.heap_availability_mask &= ~(1u << index);
		}

		object_pool.free(heap);
	}
	else if (was_full)
	{
		m.heaps[new_index].move_to_front(m.full_heaps, heap);
		m.heap_availability_mask |= 1u << new_index;
	}
	else if (index != new_index)
	{
		m.heaps[new_index].move_to_front(m.heaps[index], heap);
		m.heap_availability_mask |= 1u << new_index;
		if (!m.heaps[index].begin())
			m.heap_availability_mask &= ~(1u << index);
	}
}

bool Allocator::allocate_global(uint32_t size, DeviceAllocation *alloc)
{
	// Fall back to global allocation, do not recycle.
	if (!global_allocator->allocate(size, memory_type, &alloc->base, &alloc->host_base, VK_NULL_HANDLE))
		return false;
	alloc->alloc = nullptr;
	alloc->memory_type = memory_type;
	alloc->size = size;
	return true;
}

bool Allocator::allocate_dedicated(uint32_t size, DeviceAllocation *alloc, VkImage dedicated_image)
{
	// Fall back to global allocation, do not recycle.
	if (!global_allocator->allocate(size, memory_type, &alloc->base, &alloc->host_base, dedicated_image))
		return false;
	alloc->alloc = nullptr;
	alloc->memory_type = memory_type;
	alloc->size = size;
	return true;
}

bool Allocator::allocate(uint32_t size, uint32_t alignment, AllocationTiling mode, DeviceAllocation *alloc)
{
	for (ClassAllocator &c : classes)
	{
		// Find a suitable class to allocate from.
		if (size <= c.sub_block_size * Block::NumSubBlocks)
		{
			if (alignment > c.sub_block_size)
			{
				size_t padded_size = size + (alignment - c.sub_block_size);
				if (padded_size <= c.sub_block_size * Block::NumSubBlocks)
					size = padded_size;
				else
					continue;
			}

			bool ret = c.allocate(size, mode, alloc, false);
			if (ret)
			{
				uint32_t aligned_offset = (alloc->offset + alignment - 1) & ~(alignment - 1);
				if (alloc->host_base)
					alloc->host_base += aligned_offset - alloc->offset;
				alloc->offset = aligned_offset;
			}
			return ret;
		}
	}

	return allocate_global(size, alloc);
}

Allocator::Allocator()
{
	for (unsigned i = 0; i < MEMORY_CLASS_COUNT - 1; i++)
		classes[i].set_parent(&classes[i + 1]);

	get_class_allocator(MEMORY_CLASS_SMALL).set_tiling_mask(~0u);
	get_class_allocator(MEMORY_CLASS_MEDIUM).set_tiling_mask(~0u);
	get_class_allocator(MEMORY_CLASS_LARGE).set_tiling_mask(0);
	get_class_allocator(MEMORY_CLASS_HUGE).set_tiling_mask(0);

	get_class_allocator(MEMORY_CLASS_SMALL).set_sub_block_size(128);
	get_class_allocator(MEMORY_CLASS_MEDIUM).set_sub_block_size(128 * Block::NumSubBlocks); // 4K

	// 128K, this is the largest bufferImageGranularity a Vulkan implementation may have.
	get_class_allocator(MEMORY_CLASS_LARGE).set_sub_block_size(128 * Block::NumSubBlocks * Block::NumSubBlocks);
	get_class_allocator(MEMORY_CLASS_HUGE)
	    .set_sub_block_size(64 * Block::NumSubBlocks * Block::NumSubBlocks * Block::NumSubBlocks); // 2M
}

void DeviceAllocator::init(VkPhysicalDevice gpu, VkDevice vkdevice)
{
	device = vkdevice;
	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(gpu, &props);
	atom_alignment = props.limits.nonCoherentAtomSize;

	heaps.clear();
	allocators.clear();

	heaps.resize(mem_props.memoryHeapCount);
	for (unsigned i = 0; i < mem_props.memoryTypeCount; i++)
	{
		allocators.emplace_back(new Allocator);
		allocators.back()->set_memory_type(i);
		allocators.back()->set_global_allocator(this);
	}
}

bool DeviceAllocator::allocate(uint32_t size, uint32_t alignment, uint32_t memory_type, AllocationTiling mode,
                               DeviceAllocation *alloc)
{
	return allocators[memory_type]->allocate(size, alignment, mode, alloc);
}

bool DeviceAllocator::allocate_image_memory(uint32_t size, uint32_t alignment, uint32_t memory_type,
                                            AllocationTiling tiling, DeviceAllocation *alloc, VkImage image)
{
	if (!use_dedicated)
		return allocate(size, alignment, memory_type, tiling, alloc);

	VkImageMemoryRequirementsInfo2KHR info = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR };
	info.image = image;

	VkMemoryDedicatedRequirementsKHR dedicated_req = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2KHR mem_req = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR };
	mem_req.pNext = &dedicated_req;
	vkGetImageMemoryRequirements2KHR(device, &info, &mem_req);

	if (dedicated_req.prefersDedicatedAllocation || dedicated_req.requiresDedicatedAllocation)
		return allocators[memory_type]->allocate_dedicated(size, alloc, image);
	else
		return allocate(size, alignment, memory_type, tiling, alloc);
}

bool DeviceAllocator::allocate_global(uint32_t size, uint32_t memory_type, DeviceAllocation *alloc)
{
	return allocators[memory_type]->allocate_global(size, alloc);
}

void DeviceAllocator::Heap::garbage_collect(VkDevice device)
{
	for (Allocation &block : blocks)
	{
		if (block.host_memory)
			vkUnmapMemory(device, block.memory);
		vkFreeMemory(device, block.memory, nullptr);
		size -= block.size;
	}
}

DeviceAllocator::~DeviceAllocator()
{
	for (Heap &heap : heaps)
		heap.garbage_collect(device);
}

void DeviceAllocator::free(uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory)
{
	ALLOCATOR_LOCK();
	Heap &heap = heaps[mem_props.memoryTypes[memory_type].heapIndex];
	heap.blocks.push_back({ memory, host_memory, size, memory_type });
}

void DeviceAllocator::free_no_recycle(uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory)
{
	ALLOCATOR_LOCK();
	Heap &heap = heaps[mem_props.memoryTypes[memory_type].heapIndex];
	if (host_memory)
		vkUnmapMemory(device, memory);
	vkFreeMemory(device, memory, nullptr);
	heap.size -= size;
}

void DeviceAllocator::garbage_collect()
{
	ALLOCATOR_LOCK();
	for (Heap &heap : heaps)
		heap.garbage_collect(device);
}

void *DeviceAllocator::map_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags)
{
	// This will only happen if the memory type is device local only, which we cannot possibly map.
	if (!alloc.host_base)
		return nullptr;

	if ((flags & MEMORY_ACCESS_READ_BIT) &&
	    !(mem_props.memoryTypes[alloc.memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VkDeviceSize offset = alloc.offset & ~(atom_alignment - 1);
		VkDeviceSize size = (alloc.offset + alloc.get_size() - offset + atom_alignment - 1) & ~(atom_alignment - 1);

		// Have to invalidate cache here.
		const VkMappedMemoryRange range = {
			VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, alloc.base, offset, size,
		};
		vkInvalidateMappedMemoryRanges(device, 1, &range);
	}

	return alloc.host_base;
}

void DeviceAllocator::unmap_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags)
{
	// This will only happen if the memory type is device local only, which we cannot possibly map.
	if (!alloc.host_base)
		return;

	if ((flags & MEMORY_ACCESS_WRITE_BIT) &&
	    !(mem_props.memoryTypes[alloc.memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VkDeviceSize offset = alloc.offset & ~(atom_alignment - 1);
		VkDeviceSize size = (alloc.offset + alloc.get_size() - offset + atom_alignment - 1) & ~(atom_alignment - 1);

		// Have to flush caches here.
		const VkMappedMemoryRange range = {
			VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, alloc.base, offset, size,
		};
		vkFlushMappedMemoryRanges(device, 1, &range);
	}
}

bool DeviceAllocator::allocate(uint32_t size, uint32_t memory_type, VkDeviceMemory *memory, uint8_t **host_memory,
                               VkImage dedicated_image)
{
	ALLOCATOR_LOCK();
	Heap &heap = heaps[mem_props.memoryTypes[memory_type].heapIndex];

	// Naive searching is fine here as vkAllocate blocks are *huge* and we won't have many of them.
	size_t found_idx = heap.blocks.size();
	for (size_t i = 0; i < heap.blocks.size(); i++)
	{
		if (heap.blocks[i].size == size && heap.blocks[i].type == memory_type)
		{
			found_idx = i;
			break;
		}
	}

	bool host_visible = (mem_props.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

	// Found previously used block.
	if (found_idx < heap.blocks.size())
	{
		Allocation &block = heap.blocks[found_idx];
		*memory = block.memory;
		*host_memory = block.host_memory;
		heap.blocks.erase(heap.blocks.begin() + found_idx);
		return true;
	}

	VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, size, memory_type };
	VkMemoryDedicatedAllocateInfoKHR dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR };
	if (dedicated_image != VK_NULL_HANDLE)
	{
		dedicated.image = dedicated_image;
		info.pNext = &dedicated;
	}

	VkDeviceMemory device_memory;
	VkResult res = vkAllocateMemory(device, &info, nullptr, &device_memory);

	if (res == VK_SUCCESS)
	{
		heap.size += size;
		*memory = device_memory;

		if (host_visible)
		{
			if (vkMapMemory(device, device_memory, 0, size, 0, reinterpret_cast<void **>(host_memory)) != VK_SUCCESS)
				return false;
		}

		return true;
	}
	else
	{
		// Look through our heap and see if there are blocks of other types we can free.
		std::vector<Allocation>::iterator itr = heap.blocks.begin();
		while (res != VK_SUCCESS && itr != heap.blocks.end())
		{
			if (itr->host_memory)
				vkUnmapMemory(device, itr->memory);
			vkFreeMemory(device, itr->memory, nullptr);
			heap.size -= itr->size;
			res = vkAllocateMemory(device, &info, nullptr, &device_memory);
			++itr;
		}

		heap.blocks.erase(heap.blocks.begin(), itr);

		if (res == VK_SUCCESS)
		{
			heap.size += size;
			*memory = device_memory;

			if (host_visible)
			{
				if (vkMapMemory(device, device_memory, 0, size, 0, reinterpret_cast<void **>(host_memory)) !=
				    VK_SUCCESS)
				{
					vkFreeMemory(device, device_memory, nullptr);
					return false;
				}
			}

			return true;
		}
		else
			return false;
	}
}
}

/* === shader.cpp === */

#include <spirv_cross.hpp>

#ifdef GRANITE_SPIRV_DUMP
#include "filesystem.hpp"
#endif

using spirv_cross::Compiler;
using spirv_cross::Resource;
using spirv_cross::ShaderResources;
using spirv_cross::SpecializationConstant;
using spirv_cross::SPIRType;
using namespace Util;

namespace Vulkan
{
PipelineLayout::PipelineLayout(Hash hash, Device *device, const CombinedResourceLayout &layout)
	: IntrusiveHashMapEnabled<PipelineLayout>(hash)
	, device(device)
	, layout(layout)
{
	VkDescriptorSetLayout layouts[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	unsigned num_sets = 0;
	for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
	{
		set_allocators[i] = device->request_descriptor_set_allocator(layout.sets[i], layout.stages_for_bindings[i]);
		layouts[i] = set_allocators[i]->get_layout();
		if (layout.descriptor_set_mask & (1u << i))
			num_sets = i + 1;
	}

	VkPipelineLayoutCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	if (num_sets)
	{
		info.setLayoutCount = num_sets;
		info.pSetLayouts = layouts;
	}

	if (layout.push_constant_range.stageFlags != 0)
	{
		info.pushConstantRangeCount = 1;
		info.pPushConstantRanges = &layout.push_constant_range;
	}

	LOGI("Creating pipeline layout.\n");
	if (vkCreatePipelineLayout(device->get_device(), &info, nullptr, &pipe_layout) != VK_SUCCESS)
		LOGE("Failed to create pipeline layout.\n");
}

PipelineLayout::~PipelineLayout()
{
	if (pipe_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(device->get_device(), pipe_layout, nullptr);
}

const char *Shader::stage_to_name(ShaderStage stage)
{
	switch (stage)
	{
	case ShaderStage::Compute:
		return "compute";
	case ShaderStage::Vertex:
		return "vertex";
	case ShaderStage::Fragment:
		return "fragment";
	case ShaderStage::Geometry:
		return "geometry";
	case ShaderStage::TessControl:
		return "tess_control";
	case ShaderStage::TessEvaluation:
		return "tess_evaluation";
	default:
		return "unknown";
	}
}

static bool get_stock_sampler(StockSampler &sampler, const std::string &name)
{
	if (name.find("NearestClamp") != std::string::npos)
		sampler = StockSampler::NearestClamp;
	else if (name.find("LinearClamp") != std::string::npos)
		sampler = StockSampler::LinearClamp;
	else if (name.find("TrilinearClamp") != std::string::npos)
		sampler = StockSampler::TrilinearClamp;
	else if (name.find("NearestWrap") != std::string::npos)
		sampler = StockSampler::NearestWrap;
	else if (name.find("LinearWrap") != std::string::npos)
		sampler = StockSampler::LinearWrap;
	else if (name.find("TrilinearWrap") != std::string::npos)
		sampler = StockSampler::TrilinearWrap;
	else if (name.find("NearestShadow") != std::string::npos)
		sampler = StockSampler::NearestShadow;
	else if (name.find("LinearShadow") != std::string::npos)
		sampler = StockSampler::LinearShadow;
	else
		return false;

	return true;
}

Shader::Shader(Hash hash, Device *device, const uint32_t *data, size_t size)
	: IntrusiveHashMapEnabled<Shader>(hash)
	, device(device)
{
#ifdef GRANITE_SPIRV_DUMP
	if (!Granite::Filesystem::get().write_buffer_to_file(std::string("cache://spirv/") + std::to_string(hash) + ".spv", data, size))
		LOGE("Failed to dump shader to file.\n");
#endif

	VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	info.codeSize = size;
	info.pCode = data;

	LOGI("Creating shader module.\n");
	if (vkCreateShaderModule(device->get_device(), &info, nullptr, &module) != VK_SUCCESS)
		LOGE("Failed to create shader module.\n");

	Compiler compiler(data, size / sizeof(uint32_t));

	ShaderResources resources = compiler.get_shader_resources();
	for (Resource &image : resources.sampled_images)
	{
		uint32_t set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		const SPIRType &type = compiler.get_type(image.base_type_id);
		if (type.image.dim == spv::DimBuffer)
			layout.sets[set].sampled_buffer_mask |= 1u << binding;
		else
			layout.sets[set].sampled_image_mask |= 1u << binding;

		if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
			layout.sets[set].fp_mask |= 1u << binding;

		const std::string &name = image.name;
		StockSampler sampler;
		if (type.image.dim != spv::DimBuffer && get_stock_sampler(sampler, name))
		{
			if (has_immutable_sampler(layout.sets[set], binding))
			{
				if (sampler != get_immutable_sampler(layout.sets[set], binding))
					LOGE("Immutable sampler mismatch detected!\n");
			}
			else
				set_immutable_sampler(layout.sets[set], binding, sampler);
		}
	}

	for (Resource &image : resources.subpass_inputs)
	{
		uint32_t set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		layout.sets[set].input_attachment_mask |= 1u << binding;

		const SPIRType &type = compiler.get_type(image.base_type_id);
		if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
			layout.sets[set].fp_mask |= 1u << binding;
	}

	for (Resource &image : resources.separate_images)
	{
		uint32_t set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);

		const SPIRType &type = compiler.get_type(image.base_type_id);
		if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
			layout.sets[set].fp_mask |= 1u << binding;

		if (type.image.dim == spv::DimBuffer)
			layout.sets[set].sampled_buffer_mask |= 1u << binding;
		else
			layout.sets[set].separate_image_mask |= 1u << binding;
	}

	for (Resource &image : resources.separate_samplers)
	{
		uint32_t set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		layout.sets[set].sampler_mask |= 1u << binding;

		const std::string &name = image.name;
		StockSampler sampler;
		if (get_stock_sampler(sampler, name))
		{
			if (has_immutable_sampler(layout.sets[set], binding))
			{
				if (sampler != get_immutable_sampler(layout.sets[set], binding))
					LOGE("Immutable sampler mismatch detected!\n");
			}
			else
				set_immutable_sampler(layout.sets[set], binding, sampler);
		}
	}

	for (Resource &image : resources.storage_images)
	{
		uint32_t set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		layout.sets[set].storage_image_mask |= 1u << binding;

		const SPIRType &type = compiler.get_type(image.base_type_id);
		if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
			layout.sets[set].fp_mask |= 1u << binding;
	}

	for (Resource &buffer : resources.uniform_buffers)
	{
		uint32_t set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		layout.sets[set].uniform_buffer_mask |= 1u << binding;
	}

	for (Resource &buffer : resources.storage_buffers)
	{
		uint32_t set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		uint32_t binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		layout.sets[set].storage_buffer_mask |= 1u << binding;
	}

	for (Resource &attrib : resources.stage_inputs)
	{
		uint32_t location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
		layout.input_mask |= 1u << location;
	}

	for (Resource &attrib : resources.stage_outputs)
	{
		uint32_t location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
		layout.output_mask |= 1u << location;
	}

	if (!resources.push_constant_buffers.empty())
	{
		// Don't bother trying to extract which part of a push constant block we're using.
		// Just assume we're accessing everything. At least on older validation layers,
		// it did not do a static analysis to determine similar information, so we got a lot
		// of false positives.
		layout.push_constant_size =
		    compiler.get_declared_struct_size(compiler.get_type(resources.push_constant_buffers.front().base_type_id));
	}

	std::vector<SpecializationConstant> spec_constants = compiler.get_specialization_constants();
	for (SpecializationConstant &c : spec_constants)
	{
		if (c.constant_id >= VULKAN_NUM_SPEC_CONSTANTS)
		{
			LOGE("Spec constant ID: %u is out of range, will be ignored.\n", c.constant_id);
			continue;
		}

		layout.spec_constant_mask |= 1u << c.constant_id;
	}
}

Shader::~Shader()
{
	if (module)
		vkDestroyShaderModule(device->get_device(), module, nullptr);
}

void Program::set_shader(ShaderStage stage, Shader *handle)
{
	shaders[(unsigned)stage] = handle;
}

Program::Program(Device *device, Shader *vertex, Shader *fragment)
    : device(device)
{
	set_shader(ShaderStage::Vertex, vertex);
	set_shader(ShaderStage::Fragment, fragment);
	device->bake_program(*this);
}

Program::Program(Device *device, Shader *compute)
    : device(device)
{
	set_shader(ShaderStage::Compute, compute);
	device->bake_program(*this);
}

VkPipeline Program::get_pipeline(Hash hash) const
{
	IntrusivePODWrapper<VkPipeline> *ret = pipelines.find(hash);
	return ret ? ret->get() : VK_NULL_HANDLE;
}

VkPipeline Program::add_pipeline(Hash hash, VkPipeline pipeline)
{
	return pipelines.emplace_yield(hash, pipeline)->get();
}

Program::~Program()
{
	for (IntrusivePODWrapper<VkPipeline> &pipe : pipelines)
		device->destroy_pipeline_nolock(pipe.get());
}
}

/* === descriptor_set.cpp === */


using namespace Util;

namespace Vulkan
{
DescriptorSetAllocator::DescriptorSetAllocator(Hash hash, Device *device, const DescriptorSetLayout &layout, const uint32_t *stages_for_binds)
	: IntrusiveHashMapEnabled<DescriptorSetAllocator>(hash)
	, device(device)
{
	VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	for (unsigned i = 0; i < VULKAN_NUM_BINDINGS; i++)
	{
		uint32_t stages = stages_for_binds[i];
		if (stages == 0)
			continue;

		unsigned types = 0;
		if (layout.sampled_image_mask & (1u << i))
		{
			VkSampler sampler = VK_NULL_HANDLE;
			if (has_immutable_sampler(layout, i))
				sampler = device->get_stock_sampler(get_immutable_sampler(layout, i)).get_sampler();

			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, sampler != VK_NULL_HANDLE ? &sampler : nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.sampled_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.storage_image_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.uniform_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.storage_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.input_attachment_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.separate_image_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.sampler_mask & (1u << i))
		{
			VkSampler sampler = VK_NULL_HANDLE;
			if (has_immutable_sampler(layout, i))
				sampler = device->get_stock_sampler(get_immutable_sampler(layout, i)).get_sampler();

			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stages, sampler != VK_NULL_HANDLE ? &sampler : nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_SAMPLER, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		(void)types;
		VK_ASSERT(types <= 1 && "Descriptor set aliasing!");
	}

	if (!bindings.empty())
	{
		info.bindingCount = bindings.size();
		info.pBindings = bindings.data();
	}

	LOGI("Creating descriptor set layout.\n");
	if (vkCreateDescriptorSetLayout(device->get_device(), &info, nullptr, &set_layout) != VK_SUCCESS)
		LOGE("Failed to create descriptor set layout.");
}

void DescriptorSetAllocator::begin_frame()
{
	per_thread.should_begin = true;
}

std::pair<VkDescriptorSet, bool> DescriptorSetAllocator::find(Hash hash)
{
	PerThread &state = per_thread;
	if (state.should_begin)
	{
		state.set_nodes.begin_frame();
		state.should_begin = false;
	}

	DescriptorSetNode *node = state.set_nodes.request(hash);
	if (node)
		return { node->set, true };

	node = state.set_nodes.request_vacant(hash);
	if (node)
		return { node->set, false };

	VkDescriptorPool pool;
	VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	info.maxSets = VULKAN_NUM_SETS_PER_POOL;
	if (!pool_size.empty())
	{
		info.poolSizeCount = pool_size.size();
		info.pPoolSizes = pool_size.data();
	}

	if (vkCreateDescriptorPool(device->get_device(), &info, nullptr, &pool) != VK_SUCCESS)
		LOGE("Failed to create descriptor pool.\n");

	VkDescriptorSet sets[VULKAN_NUM_SETS_PER_POOL];
	VkDescriptorSetLayout layouts[VULKAN_NUM_SETS_PER_POOL];
	for (unsigned i = 0; i < VULKAN_NUM_SETS_PER_POOL; i++)
		layouts[i] = set_layout;

	VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	alloc.descriptorPool = pool;
	alloc.descriptorSetCount = VULKAN_NUM_SETS_PER_POOL;
	alloc.pSetLayouts = layouts;

	if (vkAllocateDescriptorSets(device->get_device(), &alloc, sets) != VK_SUCCESS)
		LOGE("Failed to allocate descriptor sets.\n");
	state.pools.push_back(pool);

	for (VkDescriptorSet set : sets)
		state.set_nodes.make_vacant(set);

	return { state.set_nodes.request_vacant(hash)->set, false };
}

void DescriptorSetAllocator::clear()
{
	per_thread.set_nodes.clear();
	for (VkDescriptorPool &pool : per_thread.pools)
	{
		vkResetDescriptorPool(device->get_device(), pool, 0);
		vkDestroyDescriptorPool(device->get_device(), pool, nullptr);
	}
	per_thread.pools.clear();
}

DescriptorSetAllocator::~DescriptorSetAllocator()
{
	if (set_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(device->get_device(), set_layout, nullptr);
	clear();
}
}

/* === render_pass.cpp === */


using namespace std;
using namespace Util;

namespace Util
{
template <typename T, size_t N>
class StackAllocator
{
public:
	T *allocate(size_t count)
	{
		if (count == 0)
			return nullptr;
		if (offset + count > N)
			return nullptr;

		T *ret = buffer + offset;
		offset += count;
		return ret;
	}

	T *allocate_cleared(size_t count)
	{
		T *ret = allocate(count);
		if (ret)
		{
			T defval = T();
			for (size_t i = 0; i < count; i++)
				ret[i] = defval;
		}
		return ret;
	}

	void reset()
	{
		offset = 0;
	}

private:
	T buffer[N];
	size_t offset = 0;
};
}

#define LOCK() ((void)0)

namespace Vulkan
{
static VkAttachmentLoadOp rp_color_load_op(const RenderPassInfo &info, unsigned index)
{
	if ((info.clear_attachments & (1u << index)) != 0)
		return VK_ATTACHMENT_LOAD_OP_CLEAR;
	else if ((info.load_attachments & (1u << index)) != 0)
		return VK_ATTACHMENT_LOAD_OP_LOAD;
	else
		return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

static VkAttachmentStoreOp rp_color_store_op(const RenderPassInfo &info, unsigned index)
{
	if ((info.store_attachments & (1u << index)) != 0)
		return VK_ATTACHMENT_STORE_OP_STORE;
	else
		return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static VkAttachmentReference *rp_find_color(std::vector<VkSubpassDescription> &subpasses,
                                            unsigned subpass, unsigned attachment)
{
	const VkAttachmentReference *colors = subpasses[subpass].pColorAttachments;
	for (unsigned i = 0; i < subpasses[subpass].colorAttachmentCount; i++)
		if (colors[i].attachment == attachment)
			return const_cast<VkAttachmentReference *>(&colors[i]);
	return nullptr;
}

static VkAttachmentReference *rp_find_resolve(std::vector<VkSubpassDescription> &subpasses,
                                              unsigned subpass, unsigned attachment)
{
	if (!subpasses[subpass].pResolveAttachments)
		return nullptr;

	const VkAttachmentReference *resolves = subpasses[subpass].pResolveAttachments;
	for (unsigned i = 0; i < subpasses[subpass].colorAttachmentCount; i++)
		if (resolves[i].attachment == attachment)
			return const_cast<VkAttachmentReference *>(&resolves[i]);
	return nullptr;
}

static VkAttachmentReference *rp_find_input(std::vector<VkSubpassDescription> &subpasses,
                                            unsigned subpass, unsigned attachment)
{
	const VkAttachmentReference *inputs = subpasses[subpass].pInputAttachments;
	for (unsigned i = 0; i < subpasses[subpass].inputAttachmentCount; i++)
		if (inputs[i].attachment == attachment)
			return const_cast<VkAttachmentReference *>(&inputs[i]);
	return nullptr;
}

static VkAttachmentReference *rp_find_depth_stencil(std::vector<VkSubpassDescription> &subpasses,
                                                    unsigned subpass, unsigned attachment)
{
	if (subpasses[subpass].pDepthStencilAttachment->attachment == attachment)
		return const_cast<VkAttachmentReference *>(subpasses[subpass].pDepthStencilAttachment);
	else
		return nullptr;
}

RenderPass::RenderPass(Hash hash, Device *device, const RenderPassInfo &info)
	: IntrusiveHashMapEnabled<RenderPass>(hash)
	, device(device)
{
	fill(begin(color_attachments), end(color_attachments), VK_FORMAT_UNDEFINED);

	VK_ASSERT(info.num_color_attachments || info.depth_stencil);

	// Want to make load/store to transient a very explicit thing to do, since it will kill performance.
	bool enable_transient_store = (info.op_flags & RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT) != 0;
	bool enable_transient_load = (info.op_flags & RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT) != 0;

	// Set up default subpass info structure if we don't have it.
	const RenderPassInfo::Subpass *subpass_infos = info.subpasses;
	unsigned num_subpasses = info.num_subpasses;
	RenderPassInfo::Subpass default_subpass_info;
	if (!info.subpasses)
	{
		default_subpass_info.num_color_attachments = info.num_color_attachments;
		if (info.op_flags & RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT)
			default_subpass_info.depth_stencil_mode = RenderPassInfo::DepthStencil::ReadOnly;
		else
			default_subpass_info.depth_stencil_mode = RenderPassInfo::DepthStencil::ReadWrite;

		for (unsigned i = 0; i < info.num_color_attachments; i++)
			default_subpass_info.color_attachments[i] = i;
		num_subpasses = 1;
		subpass_infos = &default_subpass_info;
	}

	// First, set up attachment descriptions.
	const unsigned num_attachments = info.num_color_attachments + (info.depth_stencil ? 1 : 0);
	VkAttachmentDescription attachments[VULKAN_NUM_ATTACHMENTS + 1];
	uint32_t implicit_transitions = 0;

	VkAttachmentLoadOp ds_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	VkAttachmentStoreOp ds_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	VK_ASSERT(!(info.clear_attachments & info.load_attachments));

	if (info.op_flags & RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT)
		ds_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	else if (info.op_flags & RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT)
		ds_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;

	if (info.op_flags & RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT)
		ds_store_op = VK_ATTACHMENT_STORE_OP_STORE;

	bool ds_read_only = (info.op_flags & RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT) != 0;
	VkImageLayout depth_stencil_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (info.depth_stencil)
	{
		depth_stencil_layout = info.depth_stencil->get_image().get_layout(
				ds_read_only ?
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL :
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		VK_ASSERT(info.color_attachments[i]);
		color_attachments[i] = info.color_attachments[i]->get_format();
		Image &image = info.color_attachments[i]->get_image();
		VkAttachmentDescription &att = attachments[i];
		att.flags = 0;
		att.format = color_attachments[i];
		att.samples = image.get_create_info().samples;
		att.loadOp = rp_color_load_op(info, i);
		att.storeOp = rp_color_store_op(info, i);
		att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		// Undefined final layout here for now means that we will just use the layout of the last
		// subpass which uses this attachment to avoid any dummy transition at the end.
		att.finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (image.get_create_info().domain == ImageDomain::Transient)
		{
			if (enable_transient_load)
			{
				// The transient will behave like a normal image.
				att.initialLayout = info.color_attachments[i]->get_image().get_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			}
			else
			{
				// Force a clean discard.
				VK_ASSERT(att.loadOp != VK_ATTACHMENT_LOAD_OP_LOAD);
				att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			}

			if (!enable_transient_store)
				att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			implicit_transitions |= 1u << i;
		}
		else
			att.initialLayout = info.color_attachments[i]->get_image().get_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}

	depth_stencil = info.depth_stencil ? info.depth_stencil->get_format() : VK_FORMAT_UNDEFINED;
	if (info.depth_stencil)
	{
		Image &image = info.depth_stencil->get_image();
		VkAttachmentDescription &att = attachments[info.num_color_attachments];
		att.flags = 0;
		att.format = depth_stencil;
		att.samples = image.get_create_info().samples;
		att.loadOp = ds_load_op;
		att.storeOp = ds_store_op;
		// Undefined final layout here for now means that we will just use the layout of the last
		// subpass which uses this attachment to avoid any dummy transition at the end.
		att.finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (format_to_aspect_mask(depth_stencil) & VK_IMAGE_ASPECT_STENCIL_BIT)
		{
			att.stencilLoadOp = ds_load_op;
			att.stencilStoreOp = ds_store_op;
		}
		else
		{
			att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}

		if (image.get_create_info().domain == ImageDomain::Transient)
		{
			if (enable_transient_load)
			{
				// The transient will behave like a normal image.
				att.initialLayout = depth_stencil_layout;
			}
			else
			{
				if (att.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
					att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				if (att.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
					att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;

				// For transient attachments we force the layouts.
				att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			}

			if (!enable_transient_store)
			{
				att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			}

			implicit_transitions |= 1u << info.num_color_attachments;
		}
		else
			att.initialLayout = depth_stencil_layout;
	}

	Util::StackAllocator<VkAttachmentReference, 1024> reference_allocator;
	Util::StackAllocator<uint32_t, 1024> preserve_allocator;

	vector<VkSubpassDescription> subpasses(num_subpasses);
	vector<VkSubpassDependency> external_dependencies;
	for (unsigned i = 0; i < num_subpasses; i++)
	{
		VkAttachmentReference *colors = reference_allocator.allocate_cleared(subpass_infos[i].num_color_attachments);
		VkAttachmentReference *inputs = reference_allocator.allocate_cleared(subpass_infos[i].num_input_attachments);
		VkAttachmentReference *resolves = reference_allocator.allocate_cleared(subpass_infos[i].num_color_attachments);
		VkAttachmentReference *depth = reference_allocator.allocate_cleared(1);

		VkSubpassDescription &subpass = subpasses[i];
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = subpass_infos[i].num_color_attachments;
		subpass.pColorAttachments = colors;
		subpass.inputAttachmentCount = subpass_infos[i].num_input_attachments;
		subpass.pInputAttachments = inputs;
		subpass.pDepthStencilAttachment = depth;

		if (subpass_infos[i].num_resolve_attachments)
		{
			VK_ASSERT(subpass_infos[i].num_color_attachments == subpass_infos[i].num_resolve_attachments);
			subpass.pResolveAttachments = resolves;
		}

		for (unsigned j = 0; j < subpass.colorAttachmentCount; j++)
		{
			uint32_t att = subpass_infos[i].color_attachments[j];
			VK_ASSERT(att == VK_ATTACHMENT_UNUSED || (att < num_attachments));
			colors[j].attachment = att;
			// Fill in later.
			colors[j].layout = VK_IMAGE_LAYOUT_UNDEFINED;
		}

		for (unsigned j = 0; j < subpass.inputAttachmentCount; j++)
		{
			uint32_t att = subpass_infos[i].input_attachments[j];
			VK_ASSERT(att == VK_ATTACHMENT_UNUSED || (att < num_attachments));
			inputs[j].attachment = att;
			// Fill in later.
			inputs[j].layout = VK_IMAGE_LAYOUT_UNDEFINED;
		}

		if (subpass.pResolveAttachments)
		{
			for (unsigned j = 0; j < subpass.colorAttachmentCount; j++)
			{
				uint32_t att = subpass_infos[i].resolve_attachments[j];
				VK_ASSERT(att == VK_ATTACHMENT_UNUSED || (att < num_attachments));
				resolves[j].attachment = att;
				// Fill in later.
				resolves[j].layout = VK_IMAGE_LAYOUT_UNDEFINED;
			}
		}

		if (info.depth_stencil && subpass_infos[i].depth_stencil_mode != RenderPassInfo::DepthStencil::None)
		{
			depth->attachment = info.num_color_attachments;
			// Fill in later.
			depth->layout = VK_IMAGE_LAYOUT_UNDEFINED;
		}
		else
		{
			depth->attachment = VK_ATTACHMENT_UNUSED;
			depth->layout = VK_IMAGE_LAYOUT_UNDEFINED;
		}
	}

	// Now, figure out how each attachment is used throughout the subpasses.
	// Either we don't care (inherit previous pass), or we need something specific.
	// Start with initial layouts.
	uint32_t preserve_masks[VULKAN_NUM_ATTACHMENTS + 1] = {};

	// Last subpass which makes use of an attachment.
	unsigned last_subpass_for_attachment[VULKAN_NUM_ATTACHMENTS + 1] = {};

	VK_ASSERT(num_subpasses <= 32);

	// 1 << subpass bit set if there are color attachment self-dependencies in the subpass.
	uint32_t color_self_dependencies = 0;
	// 1 << subpass bit set if there are depth-stencil attachment self-dependencies in the subpass.
	uint32_t depth_self_dependencies = 0;

	// 1 << subpass bit set if any input attachment is read in the subpass.
	uint32_t input_attachment_read = 0;
	uint32_t color_attachment_read_write = 0;
	uint32_t depth_stencil_attachment_write = 0;
	uint32_t depth_stencil_attachment_read = 0;

	uint32_t external_color_dependencies = 0;
	uint32_t external_depth_dependencies = 0;
	uint32_t external_input_dependencies = 0;

	for (unsigned attachment = 0; attachment < num_attachments; attachment++)
	{
		bool used = false;
		VkImageLayout current_layout = attachments[attachment].initialLayout;
		for (unsigned subpass = 0; subpass < num_subpasses; subpass++)
		{
			VkAttachmentReference *color = rp_find_color(subpasses, subpass, attachment);
			VkAttachmentReference *resolve = rp_find_resolve(subpasses, subpass, attachment);
			VkAttachmentReference *input = rp_find_input(subpasses, subpass, attachment);
			VkAttachmentReference *depth = rp_find_depth_stencil(subpasses, subpass, attachment);

			// Sanity check.
			if (color || resolve)
				VK_ASSERT(!depth);
			if (depth)
				VK_ASSERT(!color && !resolve);
			if (resolve)
				VK_ASSERT(!color && !depth);

			if (!color && !input && !depth && !resolve)
			{
				if (used)
					preserve_masks[attachment] |= 1u << subpass;
				continue;
			}

			if (!used && (implicit_transitions & (1u << attachment)))
			{
				// This is the first subpass we need implicit transitions.
				if (color)
					external_color_dependencies |= 1u << subpass;
				if (depth)
					external_depth_dependencies |= 1u << subpass;
				if (input)
					external_input_dependencies |= 1u << subpass;
			}

			if (resolve && input) // If used as both resolve attachment and input attachment in same subpass, need GENERAL.
			{
				current_layout = VK_IMAGE_LAYOUT_GENERAL;
				resolve->layout = current_layout;
				input->layout = current_layout;

				// If the attachment is first used as a feedback attachment, the initial layout should actually be GENERAL.
				if (!used && attachments[attachment].initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
					attachments[attachment].initialLayout = current_layout;

				// If first subpass changes the layout, we'll need to inject an external subpass dependency.
				if (!used && attachments[attachment].initialLayout != current_layout)
				{
					external_color_dependencies |= 1u << subpass;
					external_input_dependencies |= 1u << subpass;
				}

				used = true;
				last_subpass_for_attachment[attachment] = subpass;

				color_attachment_read_write |= 1u << subpass;
				input_attachment_read |= 1u << subpass;
			}
			else if (resolve)
			{
				if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
					current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				// If first subpass changes the layout, we'll need to inject an external subpass dependency.
				if (!used && attachments[attachment].initialLayout != current_layout)
					external_color_dependencies |= 1u << subpass;

				resolve->layout = current_layout;
				used = true;
				last_subpass_for_attachment[attachment] = subpass;
				color_attachment_read_write |= 1u << subpass;
			}
			else if (color && input) // If used as both input attachment and color attachment in same subpass, need GENERAL.
			{
				current_layout = VK_IMAGE_LAYOUT_GENERAL;
				color->layout = current_layout;
				input->layout = current_layout;

				// If the attachment is first used as a feedback attachment, the initial layout should actually be GENERAL.
				if (!used && attachments[attachment].initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
					attachments[attachment].initialLayout = current_layout;

				// If first subpass changes the layout, we'll need to inject an external subpass dependency.
				if (!used && attachments[attachment].initialLayout != current_layout)
				{
					external_color_dependencies |= 1u << subpass;
					external_input_dependencies |= 1u << subpass;
				}

				used = true;
				last_subpass_for_attachment[attachment] = subpass;
				color_self_dependencies |= 1u << subpass;

				color_attachment_read_write |= 1u << subpass;
				input_attachment_read |= 1u << subpass;
			}
			else if (color) // No particular preference
			{
				if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
					current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				color->layout = current_layout;

				// If first subpass changes the layout, we'll need to inject an external subpass dependency.
				if (!used && attachments[attachment].initialLayout != current_layout)
					external_color_dependencies |= 1u << subpass;

				used = true;
				last_subpass_for_attachment[attachment] = subpass;
				color_attachment_read_write |= 1u << subpass;
			}
			else if (depth && input) // Depends on the depth mode
			{
				VK_ASSERT(subpass_infos[subpass].depth_stencil_mode != RenderPassInfo::DepthStencil::None);
				if (subpass_infos[subpass].depth_stencil_mode == RenderPassInfo::DepthStencil::ReadWrite)
				{
					depth_self_dependencies |= 1u << subpass;
					current_layout = VK_IMAGE_LAYOUT_GENERAL;
					depth_stencil_attachment_write |= 1u << subpass;

					// If the attachment is first used as a feedback attachment, the initial layout should actually be GENERAL.
					if (!used && attachments[attachment].initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
						attachments[attachment].initialLayout = current_layout;
				}
				else
				{
					if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
						current_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
				}

				// If first subpass changes the layout, we'll need to inject an external subpass dependency.
				if (!used && attachments[attachment].initialLayout != current_layout)
				{
					external_input_dependencies |= 1u << subpass;
					external_depth_dependencies |= 1u << subpass;
				}

				depth_stencil_attachment_read |= 1u << subpass;
				input_attachment_read |= 1u << subpass;
				depth->layout = current_layout;
				input->layout = current_layout;
				used = true;
				last_subpass_for_attachment[attachment] = subpass;
			}
			else if (depth)
			{
				if (subpass_infos[subpass].depth_stencil_mode == RenderPassInfo::DepthStencil::ReadWrite)
				{
					depth_stencil_attachment_write |= 1u << subpass;
					if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
						current_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				}
				else
				{
					if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
						current_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
				}

				// If first subpass changes the layout, we'll need to inject an external subpass dependency.
				if (!used && attachments[attachment].initialLayout != current_layout)
					external_depth_dependencies |= 1u << subpass;

				depth_stencil_attachment_read |= 1u << subpass;
				depth->layout = current_layout;
				used = true;
				last_subpass_for_attachment[attachment] = subpass;
			}
			else if (input)
			{
				if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
					current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				// If the attachment is first used as an input attachment, the initial layout should actually be
				// SHADER_READ_ONLY_OPTIMAL.
				if (!used && attachments[attachment].initialLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
					attachments[attachment].initialLayout = current_layout;

				// If first subpass changes the layout, we'll need to inject an external subpass dependency.
				if (!used && attachments[attachment].initialLayout != current_layout)
					external_input_dependencies |= 1u << subpass;

				input->layout = current_layout;
				used = true;
				last_subpass_for_attachment[attachment] = subpass;
			}
			else
			{
				VK_ASSERT(0 && "Unhandled attachment usage.");
			}
		}

		// If we don't have a specific layout we need to end up in, just
		// use the last one.
		// Assert that we actually use all the attachments we have ...
		VK_ASSERT(used);
		if (attachments[attachment].finalLayout == VK_IMAGE_LAYOUT_UNDEFINED)
		{
			VK_ASSERT(current_layout != VK_IMAGE_LAYOUT_UNDEFINED);
			attachments[attachment].finalLayout = current_layout;
		}
	}

	// Only consider preserve masks before last subpass which uses an attachment.
	for (unsigned attachment = 0; attachment < num_attachments; attachment++)
		preserve_masks[attachment] &= (1u << last_subpass_for_attachment[attachment]) - 1;

	// Add preserve attachments as needed.
	for (unsigned subpass = 0; subpass < num_subpasses; subpass++)
	{
		VkSubpassDescription &pass = subpasses[subpass];
		unsigned preserve_count = 0;
		for (unsigned attachment = 0; attachment < num_attachments; attachment++)
			if (preserve_masks[attachment] & (1u << subpass))
				preserve_count++;

		uint32_t *preserve = preserve_allocator.allocate_cleared(preserve_count);
		pass.pPreserveAttachments = preserve;
		pass.preserveAttachmentCount = preserve_count;
		for (unsigned attachment = 0; attachment < num_attachments; attachment++)
			if (preserve_masks[attachment] & (1u << subpass))
				*preserve++ = attachment;
	}

	VK_ASSERT(num_subpasses > 0);
	VkRenderPassCreateInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rp_info.subpassCount = num_subpasses;
	rp_info.pSubpasses = subpasses.data();
	rp_info.pAttachments = attachments;
	rp_info.attachmentCount = num_attachments;

	// Add external subpass dependencies.
	FOR_EACH_BIT(external_color_dependencies | external_depth_dependencies | external_input_dependencies, subpass)
	{
		             external_dependencies.emplace_back();
		             VkSubpassDependency &dep = external_dependencies.back();
		             dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		             dep.dstSubpass = subpass;

		             if (external_color_dependencies & (1u << subpass))
		             {
			             dep.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			             dep.dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			             dep.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			             dep.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		             }

		             if (external_depth_dependencies & (1u << subpass))
		             {
			             dep.srcStageMask |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			             dep.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			             dep.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			             dep.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		             }

		             if (external_input_dependencies & (1u << subpass))
		             {
			             dep.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			             dep.dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			             dep.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			             dep.dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		             }
	             	}

	// Queue up self-dependencies (COLOR | DEPTH) -> INPUT.
	FOR_EACH_BIT(color_self_dependencies | depth_self_dependencies, subpass)
	{
		external_dependencies.emplace_back();
		VkSubpassDependency &dep = external_dependencies.back();
		dep.srcSubpass = subpass;
		dep.dstSubpass = subpass;
		dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		if (color_self_dependencies & (1u << subpass))
		{
			dep.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		}

		if (depth_self_dependencies & (1u << subpass))
		{
			dep.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dep.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}

		dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		}

	// Flush and invalidate caches between each subpass.
	for (unsigned subpass = 1; subpass < num_subpasses; subpass++)
	{
		external_dependencies.emplace_back();
		VkSubpassDependency &dep = external_dependencies.back();
		dep.srcSubpass = subpass - 1;
		dep.dstSubpass = subpass;
		dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		if (color_attachment_read_write & (1u << (subpass - 1)))
		{
			dep.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		}

		if (depth_stencil_attachment_write & (1u << (subpass - 1)))
		{
			dep.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dep.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}

		if (color_attachment_read_write & (1u << subpass))
		{
			dep.dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		}

		if (depth_stencil_attachment_read & (1u << subpass))
		{
			dep.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dep.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}

		if (depth_stencil_attachment_write & (1u << subpass))
		{
			dep.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dep.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}

		if (input_attachment_read & (1u << subpass))
		{
			dep.dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		}
	}

	if (!external_dependencies.empty())
	{
		rp_info.dependencyCount = external_dependencies.size();
		rp_info.pDependencies = external_dependencies.data();
	}

	// Store the important subpass information for later.
	for (uint32_t subpass_idx = 0; subpass_idx < rp_info.subpassCount; subpass_idx++)
	{
		const VkSubpassDescription &subpass = rp_info.pSubpasses[subpass_idx];

		SubpassInfo subpass_info = {};
		subpass_info.num_color_attachments = subpass.colorAttachmentCount;
		subpass_info.num_input_attachments = subpass.inputAttachmentCount;
		subpass_info.depth_stencil_attachment = *subpass.pDepthStencilAttachment;
		memcpy(subpass_info.color_attachments, subpass.pColorAttachments,
		       subpass.colorAttachmentCount * sizeof(*subpass.pColorAttachments));
		memcpy(subpass_info.input_attachments, subpass.pInputAttachments,
		       subpass.inputAttachmentCount * sizeof(*subpass.pInputAttachments));

		unsigned samples = 0;
		for (unsigned i = 0; i < subpass_info.num_color_attachments; i++)
		{
			if (subpass_info.color_attachments[i].attachment == VK_ATTACHMENT_UNUSED)
				continue;

			unsigned samp = attachments[subpass_info.color_attachments[i].attachment].samples;
			VK_ASSERT(!samples || samp == samples);
			samples = samp;
		}

		if (subpass_info.depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED)
		{
			unsigned samp = attachments[subpass_info.depth_stencil_attachment.attachment].samples;
			VK_ASSERT(!samples || samp == samples);
			samples = samp;
		}

		VK_ASSERT(samples > 0);
		subpass_info.samples = samples;
		this->subpasses.push_back(subpass_info);
	}


	// Fixup after, we want the underlying render pass to be generic.
	VkAttachmentDescription fixup_attachments[VULKAN_NUM_ATTACHMENTS + 1];
	fixup_render_pass_nvidia(rp_info, fixup_attachments);

	LOGI("Creating render pass.\n");
	if (vkCreateRenderPass(device->get_device(), &rp_info, nullptr, &render_pass) != VK_SUCCESS)
		LOGE("Failed to create render pass.");
}

void RenderPass::fixup_render_pass_nvidia(VkRenderPassCreateInfo &create_info, VkAttachmentDescription *attachments)
{
	if (device->get_gpu_properties().vendorID == VENDOR_ID_NVIDIA &&
#ifdef _WIN32
	    VK_VERSION_MAJOR(device->get_gpu_properties().driverVersion) < 417)
#else
	    VK_VERSION_MAJOR(device->get_gpu_properties().driverVersion) < 415)
#endif
	{
		// Workaround a bug on NV where depth-stencil input attachments break if we have STORE_OP_DONT_CARE.
		// Force STORE_OP_STORE for all attachments.
		if (attachments != create_info.pAttachments)
		{
			memcpy(attachments, create_info.pAttachments, create_info.attachmentCount * sizeof(attachments[0]));
			create_info.pAttachments = attachments;
		}

		for (uint32_t i = 0; i < create_info.attachmentCount; i++)
		{
			VkFormat format = attachments[i].format;
			VkImageAspectFlags aspect = format_to_aspect_mask(format);
			if ((aspect & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT)) != 0)
				attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			if ((aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
				attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		}
	}
}

RenderPass::~RenderPass()
{
	if (render_pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(device->get_device(), render_pass, nullptr);
}

Framebuffer::Framebuffer(Device *device, const RenderPass &rp, const RenderPassInfo &info)
    : Cookie(device)
    , device(device)
    , render_pass(rp)
    , info(info)
{
	width = UINT32_MAX;
	height = UINT32_MAX;
	VkImageView views[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_views = 0;

	VK_ASSERT(info.num_color_attachments || info.depth_stencil);

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		VK_ASSERT(info.color_attachments[i]);
		auto *att = info.color_attachments[i];
		unsigned lod = att->get_create_info().base_level;
		unsigned aw  = att->get_image().get_width(lod);
		unsigned ah  = att->get_image().get_height(lod);
		if (aw < width)  width  = aw;
		if (ah < height) height = ah;
		views[num_views++] = att->get_render_target_view(info.layer);
		attachments[num_attachments++] = att;
	}

	if (info.depth_stencil)
	{
		auto *att = info.depth_stencil;
		unsigned lod = att->get_create_info().base_level;
		unsigned aw  = att->get_image().get_width(lod);
		unsigned ah  = att->get_image().get_height(lod);
		if (aw < width)  width  = aw;
		if (ah < height) height = ah;
		views[num_views++] = att->get_render_target_view(info.layer);
		attachments[num_attachments++] = att;
	}

	VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fb_info.renderPass = rp.get_render_pass();
	fb_info.attachmentCount = num_views;
	fb_info.pAttachments = views;
	fb_info.width = width;
	fb_info.height = height;
	fb_info.layers = 1;

	if (vkCreateFramebuffer(device->get_device(), &fb_info, nullptr, &framebuffer) != VK_SUCCESS)
		LOGE("Failed to create framebuffer.");
}

Framebuffer::~Framebuffer()
{
	if (framebuffer != VK_NULL_HANDLE)
		device->destroy_framebuffer_nolock(framebuffer);
}

FramebufferAllocator::FramebufferAllocator(Device *device)
    : device(device)
{
}

void FramebufferAllocator::clear()
{
	framebuffers.clear();
}

void FramebufferAllocator::begin_frame()
{
	framebuffers.begin_frame();
}

Framebuffer &FramebufferAllocator::request_framebuffer(const RenderPassInfo &info)
{
	const RenderPass &rp = device->request_render_pass(info, true);
	Hasher h;
	h.u64(rp.get_hash());

	for (unsigned i = 0; i < info.num_color_attachments; i++)
		if (info.color_attachments[i])
			h.u64(info.color_attachments[i]->get_cookie());

	if (info.depth_stencil)
		h.u64(info.depth_stencil->get_cookie());

	h.u32(info.layer);

	Hash hash = h.get();

	LOCK();
	FramebufferNode *node = framebuffers.request(hash);
	if (node)
		return *node;

	return *framebuffers.emplace(hash, device, rp, info);
}

void AttachmentAllocator::clear()
{
	attachments.clear();
}

void AttachmentAllocator::begin_frame()
{
	attachments.begin_frame();
}

ImageView &AttachmentAllocator::request_attachment(unsigned width, unsigned height, VkFormat format,
                                                   unsigned index, unsigned samples, unsigned layers)
{
	Hasher h;
	h.u32(width);
	h.u32(height);
	h.u32(format);
	h.u32(index);
	h.u32(samples);
	h.u32(layers);

	Hash hash = h.get();

	LOCK();
	TransientNode *node = attachments.request(hash);
	if (node)
		return node->handle->get_view();

	ImageCreateInfo image_info = ImageCreateInfo::transient_render_target(width, height, format);

	image_info.samples = static_cast<VkSampleCountFlagBits>(samples);
	image_info.layers = layers;
	node = attachments.emplace(hash, device->create_image(image_info, nullptr));
	device->set_name(*node->handle, "AttachmentAllocator");
	return node->handle->get_view();
}
}

/* === command_buffer.cpp === */


using namespace std;
using namespace Util;

namespace Vulkan
{
static inline VkOffset3D cb_add_offset(const VkOffset3D &a, const VkOffset3D &b)
{
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}

static inline bool cb_needs_blend_constant(VkBlendFactor factor)
{
	return factor == VK_BLEND_FACTOR_CONSTANT_COLOR || factor == VK_BLEND_FACTOR_CONSTANT_ALPHA;
}

CommandBuffer::CommandBuffer(Device *device, VkCommandBuffer cmd, Type type)
    : device(device)
    , cmd(cmd)
    , type(type)
{
	begin_compute();
	set_opaque_state();
	memset(&static_state, 0, sizeof(static_state));
	memset(&bindings, 0, sizeof(bindings));
}

CommandBuffer::~CommandBuffer()
{
	VK_ASSERT(vbo_block.mapped == nullptr);
	VK_ASSERT(ubo_block.mapped == nullptr);
}

void CommandBuffer::copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
                                VkDeviceSize size)
{
	const VkBufferCopy region = {
		src_offset, dst_offset, size,
	};
	vkCmdCopyBuffer(cmd, src.get_buffer(), dst.get_buffer(), 1, &region);
}

void CommandBuffer::copy_buffer(const Buffer &dst, const Buffer &src)
{
	VK_ASSERT(dst.get_create_info().size == src.get_create_info().size);
	copy_buffer(dst, 0, src, 0, dst.get_create_info().size);
}

void CommandBuffer::copy_buffer_to_image(const Image &image, const Buffer &buffer, unsigned num_blits,
                                         const VkBufferImageCopy *blits)
{
	vkCmdCopyBufferToImage(cmd, buffer.get_buffer(),
	                       image.get_image(), image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), num_blits, blits);
}

void CommandBuffer::copy_buffer_to_image(const Image &image, const Buffer &src, VkDeviceSize buffer_offset,
                                         const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
                                         unsigned slice_height, const VkImageSubresourceLayers &subresource)
{
	const VkBufferImageCopy region = {
		buffer_offset,
		row_length != extent.width ? row_length : 0, slice_height != extent.height ? slice_height : 0,
		subresource, offset, extent,
	};
	vkCmdCopyBufferToImage(cmd, src.get_buffer(), image.get_image(), image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
	                       1, &region);
}

void CommandBuffer::copy_image_to_buffer(const Buffer &buffer, const Image &image, VkDeviceSize buffer_offset,
                                         const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
                                         unsigned slice_height, const VkImageSubresourceLayers &subresource)
{
	const VkBufferImageCopy region = {
		buffer_offset,
		row_length != extent.width ? row_length : 0, slice_height != extent.height ? slice_height : 0,
		subresource, offset, extent,
	};
	vkCmdCopyImageToBuffer(cmd, image.get_image(), image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
	                       buffer.get_buffer(), 1, &region);
}

void CommandBuffer::clear_image(const Image &image, const VkClearValue &value)
{
	VK_ASSERT(!framebuffer);
	VK_ASSERT(!actual_render_pass);

	VkImageAspectFlags aspect = format_to_aspect_mask(image.get_format());
	VkImageSubresourceRange range = {};
	range.aspectMask = aspect;
	range.baseArrayLayer = 0;
	range.baseMipLevel = 0;
	range.levelCount = image.get_create_info().levels;
	range.layerCount = image.get_create_info().layers;
	if (aspect & VK_IMAGE_ASPECT_COLOR_BIT)
	{
		vkCmdClearColorImage(cmd, image.get_image(), image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
		                     &value.color, 1, &range);
	}
	else
	{
		vkCmdClearDepthStencilImage(cmd, image.get_image(), image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
		                            &value.depthStencil, 1, &range);
	}
}

void CommandBuffer::full_barrier()
{
	VK_ASSERT(!actual_render_pass);
	VK_ASSERT(!framebuffer);
	barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT |
	            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
	        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
	            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
	            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
}

void CommandBuffer::pixel_barrier()
{
	VK_ASSERT(actual_render_pass);
	VK_ASSERT(framebuffer);
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                     VK_DEPENDENCY_BY_REGION_BIT, 1, &barrier, 0, nullptr, 0, nullptr);
}

static inline void fixup_src_stage(VkPipelineStageFlags &src_stages, bool fixup)
{
	// ALL_GRAPHICS_BIT waits for vertex as well which causes performance issues on some drivers.
	// It shouldn't matter, but hey.
	//
	// We aren't using vertex with side-effects on relevant hardware so dropping VERTEX_SHADER_BIT is fine.
	if ((src_stages & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) != 0 && fixup)
	{
		src_stages &= ~VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		src_stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
		              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	}
}

void CommandBuffer::barrier(VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages,
                            VkAccessFlags dst_access)
{
	VK_ASSERT(!actual_render_pass);
	VK_ASSERT(!framebuffer);
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	fixup_src_stage(src_stages, device->get_workarounds().optimize_all_graphics_barrier);
	vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void CommandBuffer::barrier(VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages, unsigned barriers,
                            const VkMemoryBarrier *globals, unsigned buffer_barriers,
                            const VkBufferMemoryBarrier *buffers, unsigned image_barriers,
                            const VkImageMemoryBarrier *images)
{
	VK_ASSERT(!actual_render_pass);
	VK_ASSERT(!framebuffer);
	fixup_src_stage(src_stages, device->get_workarounds().optimize_all_graphics_barrier);
	vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, barriers, globals, buffer_barriers, buffers, image_barriers, images);
}

void CommandBuffer::image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
                                  VkPipelineStageFlags src_stages, VkAccessFlags src_access,
                                  VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	VK_ASSERT(!actual_render_pass);
	VK_ASSERT(!framebuffer);
	VK_ASSERT(image.get_create_info().domain != ImageDomain::Transient);

	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.image = image.get_image();
	barrier.subresourceRange.aspectMask = format_to_aspect_mask(image.get_create_info().format);
	barrier.subresourceRange.levelCount = image.get_create_info().levels;
	barrier.subresourceRange.layerCount = image.get_create_info().layers;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	fixup_src_stage(src_stages, device->get_workarounds().optimize_all_graphics_barrier);
	vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CommandBuffer::barrier_prepare_generate_mipmap(const Image &image, VkImageLayout base_level_layout,
                                                    VkPipelineStageFlags src_stage, VkAccessFlags src_access,
                                                    bool need_top_level_barrier)
{
	const ImageCreateInfo &create_info = image.get_create_info();
	VkImageMemoryBarrier barriers[2] = {};
	VK_ASSERT(create_info.levels > 1);
	(void)create_info;

	for (unsigned i = 0; i < 2; i++)
	{
		barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[i].image = image.get_image();
		barriers[i].subresourceRange.aspectMask = format_to_aspect_mask(image.get_format());
		barriers[i].subresourceRange.layerCount = image.get_create_info().layers;
		barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		if (i == 0)
		{
			barriers[i].oldLayout = base_level_layout;
			barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barriers[i].srcAccessMask = src_access;
			barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barriers[i].subresourceRange.baseMipLevel = 0;
			barriers[i].subresourceRange.levelCount = 1;
		}
		else
		{
			barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barriers[i].srcAccessMask = 0;
			barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barriers[i].subresourceRange.baseMipLevel = 1;
			barriers[i].subresourceRange.levelCount = image.get_create_info().levels - 1;
		}
	}

	barrier(src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr,
	        need_top_level_barrier ? 2 : 1,
	        need_top_level_barrier ? barriers : barriers + 1);
}

void CommandBuffer::generate_mipmap(const Image &image)
{
	const ImageCreateInfo &create_info = image.get_create_info();
	VkOffset3D size = { int(create_info.width), int(create_info.height), int(create_info.depth) };
	const VkOffset3D origin = { 0, 0, 0 };

	VK_ASSERT(image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	b.image = image.get_image();
	b.subresourceRange.levelCount = 1;
	b.subresourceRange.layerCount = image.get_create_info().layers;
	b.subresourceRange.aspectMask = format_to_aspect_mask(image.get_format());
	b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	for (unsigned i = 1; i < create_info.levels; i++)
	{
		VkOffset3D src_size = size;
		size.x >>= 1;
		size.y >>= 1;
		size.z >>= 1;
		if (size.x < 1) size.x = 1;
		if (size.y < 1) size.y = 1;
		if (size.z < 1) size.z = 1;

		blit_image(image, image,
		           origin, size, origin, src_size, i, i - 1, 0, 0, create_info.layers, VK_FILTER_LINEAR);

		b.subresourceRange.baseMipLevel = i;
		barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		        0, nullptr, 0, nullptr, 1, &b);
	}
}

void CommandBuffer::blit_image(const Image &dst, const Image &src,
                               const VkOffset3D &dst_offset,
                               const VkOffset3D &dst_extent, const VkOffset3D &src_offset, const VkOffset3D &src_extent,
                               unsigned dst_level, unsigned src_level, unsigned dst_base_layer, unsigned src_base_layer,
                               unsigned num_layers, VkFilter filter)
{
	// RADV workaround: blit one layer at a time.
	for (unsigned i = 0; i < num_layers; i++)
	{
		const VkImageBlit blit = {
				{ format_to_aspect_mask(src.get_create_info().format), src_level, src_base_layer + i, 1 },
				{ src_offset,                                          cb_add_offset(src_offset, src_extent) },
				{ format_to_aspect_mask(dst.get_create_info().format), dst_level, dst_base_layer + i, 1 },
				{ dst_offset,                                          cb_add_offset(dst_offset, dst_extent) },
		};

		vkCmdBlitImage(cmd,
		               src.get_image(), src.get_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
		               dst.get_image(), dst.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
		               1, &blit, filter);
	}
}

void CommandBuffer::begin_context()
{
	dirty = ~0u;
	dirty_sets = ~0u;
	dirty_vbos = ~0u;
	current_pipeline = VK_NULL_HANDLE;
	current_pipeline_layout = VK_NULL_HANDLE;
	current_layout = nullptr;
	current_program = nullptr;
	memset(bindings.cookies, 0, sizeof(bindings.cookies));
	memset(bindings.secondary_cookies, 0, sizeof(bindings.secondary_cookies));
	memset(vbo.buffers, 0, sizeof(vbo.buffers));
}

void CommandBuffer::begin_compute()
{
	is_compute = true;
	begin_context();
}

void CommandBuffer::begin_graphics()
{
	is_compute = false;
	begin_context();
}

void CommandBuffer::init_viewport_scissor(const RenderPassInfo &info, const Framebuffer *framebuffer)
{
	const uint32_t fb_w = framebuffer->get_width();
	const uint32_t fb_h = framebuffer->get_height();
	VkRect2D rect = info.render_area;
	if (uint32_t(rect.offset.x) > fb_w) rect.offset.x = int32_t(fb_w);
	if (uint32_t(rect.offset.y) > fb_h) rect.offset.y = int32_t(fb_h);
	{
		uint32_t w_avail = fb_w - uint32_t(rect.offset.x);
		uint32_t h_avail = fb_h - uint32_t(rect.offset.y);
		if (w_avail < rect.extent.width)  rect.extent.width  = w_avail;
		if (h_avail < rect.extent.height) rect.extent.height = h_avail;
	}

	viewport = { 0.0f, 0.0f, float(fb_w), float(fb_h), 0.0f, 1.0f };
	scissor = rect;
}

void CommandBuffer::begin_render_pass(const RenderPassInfo &info, VkSubpassContents contents)
{
	VK_ASSERT(!framebuffer);
	VK_ASSERT(!compatible_render_pass);
	VK_ASSERT(!actual_render_pass);

	framebuffer = &device->request_framebuffer(info);
	compatible_render_pass = &framebuffer->get_compatible_render_pass();
	actual_render_pass = &device->request_render_pass(info, false);

	init_viewport_scissor(info, framebuffer);

	VkClearValue clear_values[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_clear_values = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		VK_ASSERT(info.color_attachments[i]);
		if (info.clear_attachments & (1u << i))
		{
			clear_values[i].color = info.clear_color[i];
			num_clear_values = i + 1;
		}
	}

	if (info.depth_stencil && (info.op_flags & RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT) != 0)
	{
		clear_values[info.num_color_attachments].depthStencil = info.clear_depth_stencil;
		num_clear_values = info.num_color_attachments + 1;
	}

	VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	begin_info.renderPass = actual_render_pass->get_render_pass();
	begin_info.framebuffer = framebuffer->get_framebuffer();
	begin_info.renderArea = scissor;
	begin_info.clearValueCount = num_clear_values;
	begin_info.pClearValues = clear_values;

	vkCmdBeginRenderPass(cmd, &begin_info, contents);

	current_contents = contents;
	begin_graphics();
}

void CommandBuffer::end_render_pass()
{
	VK_ASSERT(framebuffer);
	VK_ASSERT(actual_render_pass);
	VK_ASSERT(compatible_render_pass);

	vkCmdEndRenderPass(cmd);

	framebuffer = nullptr;
	actual_render_pass = nullptr;
	compatible_render_pass = nullptr;
	begin_compute();
}

VkPipeline CommandBuffer::build_compute_pipeline(Hash hash)
{
	const Shader &shader = *current_program->get_shader(ShaderStage::Compute);
	VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	info.layout = current_program->get_pipeline_layout()->get_layout();
	info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.stage.module = shader.get_module();
	info.stage.pName = "main";
	info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

#ifdef GRANITE_SPIRV_DUMP
	LOGI("Compiling SPIR-V file: (%s) %s\n",
		     Shader::stage_to_name(ShaderStage::Compute),
		     (to_string(shader.get_hash()) + ".spv").c_str());
#endif

	VkSpecializationInfo spec_info = {};
	VkSpecializationMapEntry spec_entries[VULKAN_NUM_SPEC_CONSTANTS];
	uint32_t mask = current_layout->get_resource_layout().combined_spec_constant_mask &
	            static_state.state.spec_constant_mask;

	if (mask)
	{
		info.stage.pSpecializationInfo = &spec_info;
		spec_info.pData = potential_static_state.spec_constants;
		spec_info.dataSize = sizeof(potential_static_state.spec_constants);
		spec_info.pMapEntries = spec_entries;

		FOR_EACH_BIT(mask, bit)
		{
			VkSpecializationMapEntry &entry = spec_entries[spec_info.mapEntryCount++];
			entry.offset = sizeof(uint32_t) * bit;
			entry.size = sizeof(uint32_t);
			entry.constantID = bit;
				}
	}

	VkPipeline compute_pipeline;

	LOGI("Creating compute pipeline.\n");
	if (vkCreateComputePipelines(device->get_device(), VK_NULL_HANDLE, 1, &info, nullptr, &compute_pipeline) != VK_SUCCESS)
		LOGE("Failed to create compute pipeline!\n");

	return current_program->add_pipeline(hash, compute_pipeline);
}

VkPipeline CommandBuffer::build_graphics_pipeline(Hash hash)
{
	// Viewport state
	VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	vp.viewportCount = 1;
	vp.scissorCount = 1;

	// Dynamic state
	VkPipelineDynamicStateCreateInfo dyn = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dyn.dynamicStateCount = 2;
	static const VkDynamicState states[2] = {
		VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT,
	};
	dyn.pDynamicStates = states;

	// Blend state
	VkPipelineColorBlendAttachmentState blend_attachments[VULKAN_NUM_ATTACHMENTS];
	VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	blend.attachmentCount = compatible_render_pass->get_num_color_attachments(current_subpass);
	blend.pAttachments = blend_attachments;
	for (unsigned i = 0; i < blend.attachmentCount; i++)
	{
		VkPipelineColorBlendAttachmentState &att = blend_attachments[i];
		att = {};

		if (compatible_render_pass->get_color_attachment(current_subpass, i).attachment != VK_ATTACHMENT_UNUSED &&
			(current_layout->get_resource_layout().render_target_mask & (1u << i)))
		{
			att.colorWriteMask = 0xf;
			att.blendEnable = static_state.state.blend_enable;
			if (att.blendEnable)
			{
				att.alphaBlendOp = static_cast<VkBlendOp>(static_state.state.alpha_blend_op);
				att.colorBlendOp = static_cast<VkBlendOp>(static_state.state.color_blend_op);
				att.dstAlphaBlendFactor = static_cast<VkBlendFactor>(static_state.state.dst_alpha_blend);
				att.srcAlphaBlendFactor = static_cast<VkBlendFactor>(static_state.state.src_alpha_blend);
				att.dstColorBlendFactor = static_cast<VkBlendFactor>(static_state.state.dst_color_blend);
				att.srcColorBlendFactor = static_cast<VkBlendFactor>(static_state.state.src_color_blend);
			}
		}
	}
	memcpy(blend.blendConstants, potential_static_state.blend_constants, sizeof(blend.blendConstants));

	// Depth state
	VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	ds.depthTestEnable = compatible_render_pass->has_depth(current_subpass) && static_state.state.depth_test;
	ds.depthWriteEnable = compatible_render_pass->has_depth(current_subpass) && static_state.state.depth_write;

	if (ds.depthTestEnable)
		ds.depthCompareOp = static_cast<VkCompareOp>(static_state.state.depth_compare);

	// Vertex input
	VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkVertexInputAttributeDescription vi_attribs[VULKAN_NUM_VERTEX_ATTRIBS];
	vi.pVertexAttributeDescriptions = vi_attribs;
	uint32_t attr_mask = current_layout->get_resource_layout().attribute_mask;
	uint32_t binding_mask = 0;
	FOR_EACH_BIT(attr_mask, bit)
	{
		VkVertexInputAttributeDescription &attr = vi_attribs[vi.vertexAttributeDescriptionCount++];
		attr.location = bit;
		attr.binding = attribs[bit].binding;
		attr.format = attribs[bit].format;
		attr.offset = attribs[bit].offset;
		binding_mask |= 1u << attr.binding;
		}

	VkVertexInputBindingDescription vi_bindings[VULKAN_NUM_VERTEX_BUFFERS];
	vi.pVertexBindingDescriptions = vi_bindings;
	FOR_EACH_BIT(binding_mask, bit)
	{
		VkVertexInputBindingDescription &bind = vi_bindings[vi.vertexBindingDescriptionCount++];
		bind.binding = bit;
		bind.inputRate = vbo.input_rates[bit];
		bind.stride = vbo.strides[bit];
		}

	// Input assembly
	VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	ia.topology = static_cast<VkPrimitiveTopology>(static_state.state.topology);

	// Multisample
	VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	ms.rasterizationSamples = static_cast<VkSampleCountFlagBits>(compatible_render_pass->get_sample_count(current_subpass));

	if (compatible_render_pass->get_sample_count(current_subpass) > 1)
	{
		ms.alphaToCoverageEnable = static_state.state.alpha_to_coverage;
		ms.alphaToOneEnable = static_state.state.alpha_to_one;
		ms.sampleShadingEnable = static_state.state.sample_shading;
		ms.minSampleShading = 1.0f;
	}

	// Raster
	VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	raster.cullMode = static_cast<VkCullModeFlags>(static_state.state.cull_mode);
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	raster.polygonMode = VK_POLYGON_MODE_FILL;

	// Stages
	VkPipelineShaderStageCreateInfo stages[static_cast<unsigned>(ShaderStage::Count)];
	unsigned num_stages = 0;

	VkSpecializationInfo spec_info[(unsigned)ShaderStage::Count] = {};
	VkSpecializationMapEntry spec_entries[(unsigned)ShaderStage::Count][VULKAN_NUM_SPEC_CONSTANTS];

	for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
	{
		ShaderStage stage = static_cast<ShaderStage>(i);
		if (current_program->get_shader(stage))
		{
			VkPipelineShaderStageCreateInfo &s = stages[num_stages++];
			s = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			s.module = current_program->get_shader(stage)->get_module();
#ifdef GRANITE_SPIRV_DUMP
			LOGI("Compiling SPIR-V file: (%s) %s\n",
			     Shader::stage_to_name(stage),
			     (to_string(current_program->get_shader(stage)->get_hash()) + ".spv").c_str());
#endif
			s.pName = "main";
			s.stage = static_cast<VkShaderStageFlagBits>(1u << i);

			uint32_t mask = current_layout->get_resource_layout().spec_constant_mask[i] &
			            static_state.state.spec_constant_mask;

			if (mask)
			{
				s.pSpecializationInfo = &spec_info[i];
				spec_info[i].pData = potential_static_state.spec_constants;
				spec_info[i].dataSize = sizeof(potential_static_state.spec_constants);
				spec_info[i].pMapEntries = spec_entries[i];

				FOR_EACH_BIT(mask, bit)
				{
					VkSpecializationMapEntry &entry = spec_entries[i][spec_info[i].mapEntryCount++];
					entry.offset = sizeof(uint32_t) * bit;
					entry.size = sizeof(uint32_t);
					entry.constantID = bit;
								}
			}
		}
	}

	VkGraphicsPipelineCreateInfo pipe = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipe.layout = current_pipeline_layout;
	pipe.renderPass = compatible_render_pass->get_render_pass();
	pipe.subpass = current_subpass;

	pipe.pViewportState = &vp;
	pipe.pDynamicState = &dyn;
	pipe.pColorBlendState = &blend;
	pipe.pDepthStencilState = &ds;
	pipe.pVertexInputState = &vi;
	pipe.pInputAssemblyState = &ia;
	pipe.pMultisampleState = &ms;
	pipe.pRasterizationState = &raster;
	pipe.pStages = stages;
	pipe.stageCount = num_stages;

	VkPipeline pipeline;

	LOGI("Creating graphics pipeline.\n");
	VkResult res = vkCreateGraphicsPipelines(device->get_device(), VK_NULL_HANDLE, 1, &pipe, nullptr, &pipeline);
	if (res != VK_SUCCESS)
		LOGE("Failed to create graphics pipeline!\n");

	return current_program->add_pipeline(hash, pipeline);
}

void CommandBuffer::flush_compute_pipeline()
{
	Hasher h;
	h.u64(current_program->get_hash());

	// Spec constants.
	const CombinedResourceLayout &layout = current_layout->get_resource_layout();
	uint32_t combined_spec_constant = layout.combined_spec_constant_mask;
	combined_spec_constant &= static_state.state.spec_constant_mask;
	h.u32(combined_spec_constant);
	FOR_EACH_BIT(combined_spec_constant, bit)
	{
		h.u32(potential_static_state.spec_constants[bit]);
		}

	Hash hash = h.get();
	current_pipeline = current_program->get_pipeline(hash);
	if (current_pipeline == VK_NULL_HANDLE)
		current_pipeline = build_compute_pipeline(hash);
}

void CommandBuffer::flush_graphics_pipeline()
{
	Hasher h;
	active_vbos = 0;
	const CombinedResourceLayout &layout = current_layout->get_resource_layout();
	FOR_EACH_BIT(layout.attribute_mask, bit)
	{
		h.u32(bit);
		active_vbos |= 1u << attribs[bit].binding;
		h.u32(attribs[bit].binding);
		h.u32(attribs[bit].format);
		h.u32(attribs[bit].offset);
		}

	FOR_EACH_BIT(active_vbos, bit)
	{
		h.u32(vbo.input_rates[bit]);
		h.u32(vbo.strides[bit]);
		}

	h.u64(compatible_render_pass->get_hash());
	h.u32(current_subpass);
	h.u64(current_program->get_hash());
	h.data(static_state.words, sizeof(static_state.words));

	if (static_state.state.blend_enable)
	{
		bool b0 = cb_needs_blend_constant(static_cast<VkBlendFactor>(static_state.state.src_color_blend));
		bool b1 = cb_needs_blend_constant(static_cast<VkBlendFactor>(static_state.state.src_alpha_blend));
		bool b2 = cb_needs_blend_constant(static_cast<VkBlendFactor>(static_state.state.dst_color_blend));
		bool b3 = cb_needs_blend_constant(static_cast<VkBlendFactor>(static_state.state.dst_alpha_blend));
		if (b0 || b1 || b2 || b3)
			h.data(reinterpret_cast<uint32_t *>(potential_static_state.blend_constants),
			       sizeof(potential_static_state.blend_constants));
	}

	// Spec constants.
	uint32_t combined_spec_constant = layout.combined_spec_constant_mask;
	combined_spec_constant &= static_state.state.spec_constant_mask;
	h.u32(combined_spec_constant);
	FOR_EACH_BIT(combined_spec_constant, bit)
	{
		h.u32(potential_static_state.spec_constants[bit]);
		}

	Hash hash = h.get();
	current_pipeline = current_program->get_pipeline(hash);
	if (current_pipeline == VK_NULL_HANDLE)
		current_pipeline = build_graphics_pipeline(hash);
}

void CommandBuffer::flush_compute_state()
{
	VK_ASSERT(current_layout);
	VK_ASSERT(current_program);

	if (get_and_clear(COMMAND_BUFFER_DIRTY_PIPELINE_BIT))
	{
		VkPipeline old_pipe = current_pipeline;
		flush_compute_pipeline();
		if (old_pipe != current_pipeline)
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, current_pipeline);
	}

	flush_descriptor_sets();

	if (get_and_clear(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
	{
		const VkPushConstantRange &range = current_layout->get_resource_layout().push_constant_range;
		if (range.stageFlags != 0)
		{
			VK_ASSERT(range.offset == 0);
			vkCmdPushConstants(cmd, current_pipeline_layout, range.stageFlags,
			                   0, range.size,
			                   bindings.push_constant_data);
		}
	}
}

void CommandBuffer::flush_render_state()
{
	VK_ASSERT(current_layout);
	VK_ASSERT(current_program);

	// We've invalidated pipeline state, update the VkPipeline.
	if (get_and_clear(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT | COMMAND_BUFFER_DIRTY_PIPELINE_BIT |
	                  COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT))
	{
		VkPipeline old_pipe = current_pipeline;
		flush_graphics_pipeline();
		if (old_pipe != current_pipeline)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline);
			set_dirty(COMMAND_BUFFER_DYNAMIC_BITS);
		}
	}

	flush_descriptor_sets();

	if (get_and_clear(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
	{
		const VkPushConstantRange &range = current_layout->get_resource_layout().push_constant_range;
		if (range.stageFlags != 0)
		{
			VK_ASSERT(range.offset == 0);
			vkCmdPushConstants(cmd, current_pipeline_layout, range.stageFlags,
			                   0, range.size,
			                   bindings.push_constant_data);
		}
	}

	if (get_and_clear(COMMAND_BUFFER_DIRTY_VIEWPORT_BIT))
		vkCmdSetViewport(cmd, 0, 1, &viewport);
	if (get_and_clear(COMMAND_BUFFER_DIRTY_SCISSOR_BIT))
		vkCmdSetScissor(cmd, 0, 1, &scissor);

	uint32_t update_vbo_mask = dirty_vbos & active_vbos;
	FOR_EACH_BIT_RANGE(update_vbo_mask, binding, binding_count)
	{
#ifdef VULKAN_DEBUG
		for (unsigned i = binding; i < binding + binding_count; i++)
			VK_ASSERT(vbo.buffers[i] != VK_NULL_HANDLE);
#endif
		vkCmdBindVertexBuffers(cmd, binding, binding_count, vbo.buffers + binding, vbo.offsets + binding);
		}
	dirty_vbos &= ~update_vbo_mask;
}

void CommandBuffer::set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset)
{
	VK_ASSERT(attrib < VULKAN_NUM_VERTEX_ATTRIBS);
	VK_ASSERT(framebuffer);

	VertexAttribState &attr = attribs[attrib];

	if (attr.binding != binding || attr.format != format || attr.offset != offset)
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT);

	VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);

	attr.binding = binding;
	attr.format = format;
	attr.offset = offset;
}

void CommandBuffer::set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
                                       VkVertexInputRate step_rate)
{
	VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);
	VK_ASSERT(framebuffer);

	VkBuffer vkbuffer = buffer.get_buffer();
	if (vbo.buffers[binding] != vkbuffer || vbo.offsets[binding] != offset)
		dirty_vbos |= 1u << binding;
	if (vbo.strides[binding] != stride || vbo.input_rates[binding] != step_rate)
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT);

	vbo.buffers[binding] = vkbuffer;
	vbo.offsets[binding] = offset;
	vbo.strides[binding] = stride;
	vbo.input_rates[binding] = step_rate;
}

void CommandBuffer::set_viewport(const VkViewport &viewport)
{
	VK_ASSERT(framebuffer);
	this->viewport = viewport;
	set_dirty(COMMAND_BUFFER_DIRTY_VIEWPORT_BIT);
}

const VkViewport &CommandBuffer::get_viewport() const
{
	return this->viewport;
}

void CommandBuffer::set_scissor(const VkRect2D &rect)
{
	VK_ASSERT(framebuffer);
	VK_ASSERT(rect.offset.x >= 0);
	VK_ASSERT(rect.offset.y >= 0);
	scissor = rect;
	set_dirty(COMMAND_BUFFER_DIRTY_SCISSOR_BIT);
}

void CommandBuffer::push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range)
{
	VK_ASSERT(offset + range <= VULKAN_PUSH_CONSTANT_SIZE);
	memcpy(bindings.push_constant_data + offset, data, range);
	set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
}


void CommandBuffer::set_program(Program &program)
{
	if (current_program == &program)
		return;

	current_program = &program;
	current_pipeline = VK_NULL_HANDLE;

	VK_ASSERT((framebuffer && current_program->get_shader(ShaderStage::Vertex)) ||
	          (!framebuffer && current_program->get_shader(ShaderStage::Compute)));

	set_dirty(COMMAND_BUFFER_DIRTY_PIPELINE_BIT | COMMAND_BUFFER_DYNAMIC_BITS);

	if (!current_layout)
	{
		dirty_sets = ~0u;
		set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);

		current_layout = program.get_pipeline_layout();
		current_pipeline_layout = current_layout->get_layout();
	}
	else if (program.get_pipeline_layout()->get_hash() != current_layout->get_hash())
	{
		const CombinedResourceLayout &new_layout = program.get_pipeline_layout()->get_resource_layout();
		const CombinedResourceLayout &old_layout = current_layout->get_resource_layout();

		// If the push constant layout changes, all descriptor sets
		// are invalidated.
		if (new_layout.push_constant_layout_hash != old_layout.push_constant_layout_hash)
		{
			dirty_sets = ~0u;
			set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
		}
		else
		{
			// Find the first set whose descriptor set layout differs.
			PipelineLayout *new_pipe_layout = program.get_pipeline_layout();
			for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
			{
				if (new_pipe_layout->get_allocator(set) != current_layout->get_allocator(set))
				{
					dirty_sets |= ~((1u << set) - 1);
					break;
				}
			}
		}
		current_layout = program.get_pipeline_layout();
		current_pipeline_layout = current_layout->get_layout();
	}
}

void *CommandBuffer::allocate_constant_data(unsigned set, unsigned binding, VkDeviceSize size)
{
	BufferBlockAllocation data = ubo_block.allocate(size);
	if (!data.host)
	{
		device->request_uniform_block(ubo_block, size);
		data = ubo_block.allocate(size);
	}
	set_uniform_buffer(set, binding, *ubo_block.gpu, data.offset, size);
	return data.host;
}

void *CommandBuffer::allocate_vertex_data(unsigned binding, VkDeviceSize size, VkDeviceSize stride,
                                          VkVertexInputRate step_rate)
{
	BufferBlockAllocation data = vbo_block.allocate(size);
	if (!data.host)
	{
		device->request_vertex_block(vbo_block, size);
		data = vbo_block.allocate(size);
	}

	set_vertex_binding(binding, *vbo_block.gpu, data.offset, stride, step_rate);
	return data.host;
}

void CommandBuffer::set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
                                       VkDeviceSize range)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(buffer.get_create_info().usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	ResourceBinding &b = bindings.bindings[set][binding];

	if (buffer.get_cookie() == bindings.cookies[set][binding] && b.buffer.offset == offset && b.buffer.range == range)
		return;

	b.buffer = { buffer.get_buffer(), offset, range };
	bindings.cookies[set][binding] = buffer.get_cookie();
	bindings.secondary_cookies[set][binding] = 0;
	dirty_sets |= 1u << set;
}

void CommandBuffer::set_sampler(unsigned set, unsigned binding, const Sampler &sampler)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	if (sampler.get_cookie() == bindings.secondary_cookies[set][binding])
		return;

	ResourceBinding &b = bindings.bindings[set][binding];
	b.image.fp.sampler = sampler.get_sampler();
	b.image.integer.sampler = sampler.get_sampler();
	dirty_sets |= 1u << set;
	bindings.secondary_cookies[set][binding] = sampler.get_cookie();
}

void CommandBuffer::set_buffer_view(unsigned set, unsigned binding, const BufferView &view)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_buffer().get_create_info().usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
	if (view.get_cookie() == bindings.cookies[set][binding])
		return;
	ResourceBinding &b = bindings.bindings[set][binding];
	b.buffer_view = view.get_view();
	bindings.cookies[set][binding] = view.get_cookie();
	bindings.secondary_cookies[set][binding] = 0;
	dirty_sets |= 1u << set;
}

void CommandBuffer::set_input_attachments(unsigned set, unsigned start_binding)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(start_binding + actual_render_pass->get_num_input_attachments(current_subpass) <= VULKAN_NUM_BINDINGS);
	unsigned num_input_attachments = actual_render_pass->get_num_input_attachments(current_subpass);
	for (unsigned i = 0; i < num_input_attachments; i++)
	{
		const VkAttachmentReference &ref = actual_render_pass->get_input_attachment(current_subpass, i);
		if (ref.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		ImageView *view = framebuffer->get_attachment(ref.attachment);
		VK_ASSERT(view);
		VK_ASSERT(view->get_image().get_create_info().usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);

		if (view->get_cookie() == bindings.cookies[set][start_binding + i] &&
		    bindings.bindings[set][start_binding + i].image.fp.imageLayout == ref.layout)
		{
			continue;
		}

		ResourceBinding &b = bindings.bindings[set][start_binding + i];
		b.image.fp.imageLayout = ref.layout;
		b.image.integer.imageLayout = ref.layout;
		b.image.fp.imageView = view->get_float_view();
		b.image.integer.imageView = view->get_integer_view();
		bindings.cookies[set][start_binding + i] = view->get_cookie();
		dirty_sets |= 1u << set;
	}
}

void CommandBuffer::set_texture(unsigned set, unsigned binding,
                                VkImageView float_view, VkImageView integer_view,
                                VkImageLayout layout,
                                uint64_t cookie)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

	if (cookie == bindings.cookies[set][binding] && bindings.bindings[set][binding].image.fp.imageLayout == layout)
		return;

	ResourceBinding &b = bindings.bindings[set][binding];
	b.image.fp.imageLayout = layout;
	b.image.fp.imageView = float_view;
	b.image.integer.imageLayout = layout;
	b.image.integer.imageView = integer_view;
	bindings.cookies[set][binding] = cookie;
	dirty_sets |= 1u << set;
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view)
{
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
	set_texture(set, binding, view.get_float_view(), view.get_integer_view(),
	            view.get_image().get_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), view.get_cookie());
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler)
{
	set_sampler(set, binding, sampler);
	set_texture(set, binding, view);
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view, StockSampler stock)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
	const Sampler &sampler = device->get_stock_sampler(stock);
	set_texture(set, binding, view, sampler);
}

void CommandBuffer::set_storage_texture(unsigned set, unsigned binding, const ImageView &view)
{
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_STORAGE_BIT);
	set_texture(set, binding, view.get_float_view(), view.get_integer_view(),
	            view.get_image().get_layout(VK_IMAGE_LAYOUT_GENERAL), view.get_cookie());
}

void CommandBuffer::flush_descriptor_set(uint32_t set)
{
	const CombinedResourceLayout &layout = current_layout->get_resource_layout();
	const DescriptorSetLayout &set_layout = layout.sets[set];
	uint32_t num_dynamic_offsets = 0;
	uint32_t dynamic_offsets[VULKAN_NUM_BINDINGS];
	Hasher h;

	h.u32(set_layout.fp_mask);

	// UBOs
	FOR_EACH_BIT(set_layout.uniform_buffer_mask, binding)
	{
		h.u64(bindings.cookies[set][binding]);
		h.u32(bindings.bindings[set][binding].buffer.range);
		VK_ASSERT(bindings.bindings[set][binding].buffer.buffer != VK_NULL_HANDLE);

		dynamic_offsets[num_dynamic_offsets++] = bindings.bindings[set][binding].buffer.offset;
		}

	// SSBOs
	FOR_EACH_BIT(set_layout.storage_buffer_mask, binding)
	{
		h.u64(bindings.cookies[set][binding]);
		h.u32(bindings.bindings[set][binding].buffer.offset);
		h.u32(bindings.bindings[set][binding].buffer.range);
		VK_ASSERT(bindings.bindings[set][binding].buffer.buffer != VK_NULL_HANDLE);
		}

	// Sampled buffers
	FOR_EACH_BIT(set_layout.sampled_buffer_mask, binding)
	{
		h.u64(bindings.cookies[set][binding]);
		VK_ASSERT(bindings.bindings[set][binding].buffer_view != VK_NULL_HANDLE);
		}

	// Sampled images
	FOR_EACH_BIT(set_layout.sampled_image_mask, binding)
	{
		h.u64(bindings.cookies[set][binding]);
		if (!has_immutable_sampler(set_layout, binding))
		{
			h.u64(bindings.secondary_cookies[set][binding]);
			VK_ASSERT(bindings.bindings[set][binding].image.fp.sampler != VK_NULL_HANDLE);
		}
		h.u32(bindings.bindings[set][binding].image.fp.imageLayout);
		VK_ASSERT(bindings.bindings[set][binding].image.fp.imageView != VK_NULL_HANDLE);
		}

	// Separate images
	FOR_EACH_BIT(set_layout.separate_image_mask, binding)
	{
		h.u64(bindings.cookies[set][binding]);
		h.u32(bindings.bindings[set][binding].image.fp.imageLayout);
		VK_ASSERT(bindings.bindings[set][binding].image.fp.imageView != VK_NULL_HANDLE);
		}

	// Separate samplers
	FOR_EACH_BIT(set_layout.sampler_mask & ~set_layout.immutable_sampler_mask, binding)
	{
		h.u64(bindings.secondary_cookies[set][binding]);
		VK_ASSERT(bindings.bindings[set][binding].image.fp.sampler != VK_NULL_HANDLE);
		}

	// Storage images
	FOR_EACH_BIT(set_layout.storage_image_mask, binding)
	{
		h.u64(bindings.cookies[set][binding]);
		h.u32(bindings.bindings[set][binding].image.fp.imageLayout);
		VK_ASSERT(bindings.bindings[set][binding].image.fp.imageView != VK_NULL_HANDLE);
		}

	// Input attachments
	FOR_EACH_BIT(set_layout.input_attachment_mask, binding)
	{
		h.u64(bindings.cookies[set][binding]);
		h.u32(bindings.bindings[set][binding].image.fp.imageLayout);
		VK_ASSERT(bindings.bindings[set][binding].image.fp.imageView != VK_NULL_HANDLE);
		}

	Hash hash = h.get();
	std::pair<VkDescriptorSet, bool> allocated = current_layout->get_allocator(set)->find(hash);

	// The descriptor set was not successfully cached, rebuild.
	if (!allocated.second)
	{
		uint32_t write_count = 0;
		uint32_t buffer_info_count = 0;
		VkWriteDescriptorSet writes[VULKAN_NUM_BINDINGS];
		VkDescriptorBufferInfo buffer_info[VULKAN_NUM_BINDINGS];

		FOR_EACH_BIT(set_layout.uniform_buffer_mask, binding)
		{
			VkWriteDescriptorSet &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;

			// Offsets are applied dynamically.
			VkDescriptorBufferInfo &buffer = buffer_info[buffer_info_count++];
			buffer = bindings.bindings[set][binding].buffer;
			buffer.offset = 0;
			write.pBufferInfo = &buffer;
				}

		FOR_EACH_BIT(set_layout.storage_buffer_mask, binding)
		{
			VkWriteDescriptorSet &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pBufferInfo = &bindings.bindings[set][binding].buffer;
				}

		FOR_EACH_BIT(set_layout.sampled_buffer_mask, binding)
		{
			VkWriteDescriptorSet &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pTexelBufferView = &bindings.bindings[set][binding].buffer_view;
				}

		FOR_EACH_BIT(set_layout.sampled_image_mask, binding)
		{
			VkWriteDescriptorSet &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;

			if (set_layout.fp_mask & (1u << binding))
				write.pImageInfo = &bindings.bindings[set][binding].image.fp;
			else
				write.pImageInfo = &bindings.bindings[set][binding].image.integer;
				}

		FOR_EACH_BIT(set_layout.separate_image_mask, binding)
		{
			VkWriteDescriptorSet &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;

			if (set_layout.fp_mask & (1u << binding))
				write.pImageInfo = &bindings.bindings[set][binding].image.fp;
			else
				write.pImageInfo = &bindings.bindings[set][binding].image.integer;
				}

		FOR_EACH_BIT(set_layout.sampler_mask & ~set_layout.immutable_sampler_mask, binding)
		{
			VkWriteDescriptorSet &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			write.pImageInfo = &bindings.bindings[set][binding].image.fp;
				}

		FOR_EACH_BIT(set_layout.storage_image_mask, binding)
		{
			VkWriteDescriptorSet &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;

			if (set_layout.fp_mask & (1u << binding))
				write.pImageInfo = &bindings.bindings[set][binding].image.fp;
			else
				write.pImageInfo = &bindings.bindings[set][binding].image.integer;
				}

		FOR_EACH_BIT(set_layout.input_attachment_mask, binding)
		{
			VkWriteDescriptorSet &write = writes[write_count++];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.pNext = nullptr;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			write.dstArrayElement = 0;
			write.dstBinding = binding;
			write.dstSet = allocated.first;
			if (set_layout.fp_mask & (1u << binding))
				write.pImageInfo = &bindings.bindings[set][binding].image.fp;
			else
				write.pImageInfo = &bindings.bindings[set][binding].image.integer;
				}

		vkUpdateDescriptorSets(device->get_device(), write_count, writes, 0, nullptr);
	}

	vkCmdBindDescriptorSets(cmd, actual_render_pass ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE,
	                        current_pipeline_layout, set, 1, &allocated.first, num_dynamic_offsets, dynamic_offsets);
}

void CommandBuffer::flush_descriptor_sets()
{
	const CombinedResourceLayout &layout = current_layout->get_resource_layout();
	uint32_t set_update = layout.descriptor_set_mask & dirty_sets;
	FOR_EACH_BIT(set_update, set)
	{ flush_descriptor_set(set); 	}
	dirty_sets &= ~set_update;
}

void CommandBuffer::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	VK_ASSERT(current_program);
	VK_ASSERT(!is_compute);
	flush_render_state();
	vkCmdDraw(cmd, vertex_count, instance_count, first_vertex, first_instance);
}

void CommandBuffer::dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
{
	VK_ASSERT(current_program);
	VK_ASSERT(is_compute);
	flush_compute_state();
	vkCmdDispatch(cmd, groups_x, groups_y, groups_z);
}

void CommandBuffer::set_opaque_state()
{
	PipelineState::State &state = static_state.state;
	memset(&state, 0, sizeof(state));
	state.cull_mode = VK_CULL_MODE_BACK_BIT;
	state.blend_enable = false;
	state.depth_test = true;
	state.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
	state.depth_write = true;
	state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
}

void CommandBuffer::set_quad_state()
{
	PipelineState::State &state = static_state.state;
	memset(&state, 0, sizeof(state));
	state.cull_mode = VK_CULL_MODE_NONE;
	state.blend_enable = false;
	state.depth_test = false;
	state.depth_write = false;
	state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
}

void CommandBuffer::end()
{
	if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		LOGE("Failed to end command buffer.\n");

	if (vbo_block.mapped)
		device->request_vertex_block_nolock(vbo_block, 0);
	if (ubo_block.mapped)
		device->request_uniform_block_nolock(ubo_block, 0);
}

void CommandBuffer::begin_region(const char *name, const float *color)
{
	if (device->ext.supports_debug_marker)
	{
		VkDebugMarkerMarkerInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT };
		if (color)
		{
			for (unsigned i = 0; i < 4; i++)
				info.color[i] = color[i];
		}
		else
		{
			for (unsigned i = 0; i < 4; i++)
				info.color[i] = 1.0f;
		}

		info.pMarkerName = name;
		vkCmdDebugMarkerBeginEXT(cmd, &info);
	}
}

void CommandBuffer::end_region()
{
	if (device->ext.supports_debug_marker)
		vkCmdDebugMarkerEndEXT(cmd);
}


void CommandBufferDeleter::operator()(Vulkan::CommandBuffer *cmd)
{
	cmd->device->handle_pool.command_buffers.free(cmd);
}
}

/* === vulkan.cpp === */


#ifndef _WIN32
#include <dlfcn.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

//#undef VULKAN_DEBUG


namespace Vulkan
{
static bool has_vk_extension(const std::vector<VkExtensionProperties> &exts, const char *name)
{
	for (const VkExtensionProperties &e : exts)
		if (strcmp(e.extensionName, name) == 0)
			return true;
	return false;
}

static bool has_vk_layer(const std::vector<VkLayerProperties> &layers, const char *name)
{
	for (const VkLayerProperties &e : layers)
		if (strcmp(e.layerName, name) == 0)
			return true;
	return false;
}

bool Context::init_loader(PFN_vkGetInstanceProcAddr addr)
{
	if (!addr)
	{
#ifndef _WIN32
		static void *module;
		if (!module)
		{
			const char *vulkan_path = getenv("GRANITE_VULKAN_LIBRARY");
			if (vulkan_path)
				module = dlopen(vulkan_path, RTLD_LOCAL | RTLD_LAZY);
			if (!module)
				module = dlopen("libvulkan.so.1", RTLD_LOCAL | RTLD_LAZY);
			if (!module)
				module = dlopen("libvulkan.so", RTLD_LOCAL | RTLD_LAZY);
			if (!module)
				return false;
		}

		addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module, "vkGetInstanceProcAddr"));
		if (!addr)
			return false;
#else
		static HMODULE module;
		if (!module)
		{
			module = LoadLibraryA("vulkan-1.dll");
			if (!module)
				return false;
		}

		addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(module, "vkGetInstanceProcAddr"));
		if (!addr)
			return false;
#endif
	}

	volkInitializeCustom(addr);
	return true;
}

Context::Context(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
                 const char **required_device_extensions, unsigned num_required_device_extensions,
                 const char **required_device_layers, unsigned num_required_device_layers,
                 const VkPhysicalDeviceFeatures *required_features)
    : instance(instance)
    , owned_device(true)
{
	volkLoadInstance(instance);
	if (!create_device(gpu, surface, required_device_extensions, num_required_device_extensions, required_device_layers,
	                   num_required_device_layers, required_features))
	{
		LOGE("Failed to create Vulkan device.\n");
		destroy();
		return;
	}
	valid = true;
}

void Context::destroy()
{
	if (device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(device);

	if (owned_device && device != VK_NULL_HANDLE)
		vkDestroyDevice(device, nullptr);
}

Context::~Context()
{
	destroy();
}

bool Context::create_device(VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
                            unsigned num_required_device_extensions, const char **required_device_layers,
                            unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features)
{
	if (gpu == VK_NULL_HANDLE)
	{
		uint32_t gpu_count = 0;
		if (vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr) != VK_SUCCESS)
		{
			LOGE("vkEnumeratePhysicalDevices failed.\n");
			return false;
		}

		if (gpu_count == 0)
			return false;

		std::vector<VkPhysicalDevice> gpus(gpu_count);
		if (vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data()) != VK_SUCCESS)
		{
			LOGE("vkEnumeratePhysicalDevices failed.\n");
			return false;
		}

		for (VkPhysicalDevice &gpu : gpus)
		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(gpu, &props);
			LOGI("Found Vulkan GPU: %s\n", props.deviceName);
			LOGI("    API: %u.%u.%u\n",
			     VK_VERSION_MAJOR(props.apiVersion),
			     VK_VERSION_MINOR(props.apiVersion),
			     VK_VERSION_PATCH(props.apiVersion));
			LOGI("    Driver: %u.%u.%u\n",
			     VK_VERSION_MAJOR(props.driverVersion),
			     VK_VERSION_MINOR(props.driverVersion),
			     VK_VERSION_PATCH(props.driverVersion));
		}

		const char *gpu_index = getenv("GRANITE_VULKAN_DEVICE_INDEX");
		if (gpu_index)
		{
			unsigned index = strtoul(gpu_index, nullptr, 0);
			if (index < gpu_count)
				gpu = gpus[index];
		}

		if (gpu == VK_NULL_HANDLE)
			gpu = gpus.front();
	}

	uint32_t ext_count = 0;
	vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, nullptr);
	std::vector<VkExtensionProperties> queried_extensions(ext_count);
	if (ext_count)
		vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, queried_extensions.data());

	uint32_t layer_count = 0;
	vkEnumerateDeviceLayerProperties(gpu, &layer_count, nullptr);
	std::vector<VkLayerProperties> queried_layers(layer_count);
	if (layer_count)
		vkEnumerateDeviceLayerProperties(gpu, &layer_count, queried_layers.data());

	for (uint32_t i = 0; i < num_required_device_extensions; i++)
		if (!has_vk_extension(queried_extensions, required_device_extensions[i]))
			return false;

	for (uint32_t i = 0; i < num_required_device_layers; i++)
		if (!has_vk_layer(queried_layers, required_device_layers[i]))
			return false;

	this->gpu = gpu;
	vkGetPhysicalDeviceProperties(gpu, &gpu_props);
	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);

	LOGI("Selected Vulkan GPU: %s\n", gpu_props.deviceName);

	if (gpu_props.apiVersion >= VK_API_VERSION_1_1)
		LOGI("GPU supports Vulkan 1.1.\n");
	else if (gpu_props.apiVersion >= VK_API_VERSION_1_0)
		LOGI("GPU supports Vulkan 1.0.\n");

	uint32_t queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, nullptr);
	std::vector<VkQueueFamilyProperties> queue_props(queue_count);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, queue_props.data());

	for (unsigned i = 0; i < queue_count; i++)
	{
		VkBool32 supported = surface == VK_NULL_HANDLE;
		if (surface != VK_NULL_HANDLE)
			vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported);

		static const VkQueueFlags required = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT;
		if (supported && ((queue_props[i].queueFlags & required) == required))
		{
			graphics_queue_family = i;
			break;
		}
	}

	for (unsigned i = 0; i < queue_count; i++)
	{
		static const VkQueueFlags required = VK_QUEUE_COMPUTE_BIT;
		if (i != graphics_queue_family && (queue_props[i].queueFlags & required) == required)
		{
			compute_queue_family = i;
			break;
		}
	}

	for (unsigned i = 0; i < queue_count; i++)
	{
		static const VkQueueFlags required = VK_QUEUE_TRANSFER_BIT;
		if (i != graphics_queue_family && i != compute_queue_family && (queue_props[i].queueFlags & required) == required)
		{
			transfer_queue_family = i;
			break;
		}
	}

	if (transfer_queue_family == VK_QUEUE_FAMILY_IGNORED)
	{
		for (unsigned i = 0; i < queue_count; i++)
		{
			static const VkQueueFlags required = VK_QUEUE_TRANSFER_BIT;
			if (i != graphics_queue_family && (queue_props[i].queueFlags & required) == required)
			{
				transfer_queue_family = i;
				break;
			}
		}
	}

	if (graphics_queue_family == VK_QUEUE_FAMILY_IGNORED)
		return false;

	unsigned universal_queue_index = 1;
	uint32_t graphics_queue_index = 0;
	uint32_t compute_queue_index = 0;
	uint32_t transfer_queue_index = 0;

	if (compute_queue_family == VK_QUEUE_FAMILY_IGNORED)
	{
		compute_queue_family = graphics_queue_family;
		compute_queue_index = std::min(queue_props[graphics_queue_family].queueCount - 1, universal_queue_index);
		universal_queue_index++;
	}

	if (transfer_queue_family == VK_QUEUE_FAMILY_IGNORED)
	{
		transfer_queue_family = graphics_queue_family;
		transfer_queue_index = std::min(queue_props[graphics_queue_family].queueCount - 1, universal_queue_index);
		universal_queue_index++;
	}
	else if (transfer_queue_family == compute_queue_family)
		transfer_queue_index = std::min(queue_props[compute_queue_family].queueCount - 1, 1u);

	static const float graphics_queue_prio = 0.5f;
	static const float compute_queue_prio = 1.0f;
	static const float transfer_queue_prio = 1.0f;
	float prio[3] = { graphics_queue_prio, compute_queue_prio, transfer_queue_prio };

	unsigned queue_family_count = 0;
	VkDeviceQueueCreateInfo queue_info[3] = {};

	VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	device_info.pQueueCreateInfos = queue_info;

	queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info[queue_family_count].queueFamilyIndex = graphics_queue_family;
	queue_info[queue_family_count].queueCount = std::min(universal_queue_index,
	                                                     queue_props[graphics_queue_family].queueCount);
	queue_info[queue_family_count].pQueuePriorities = prio;
	queue_family_count++;

	if (compute_queue_family != graphics_queue_family)
	{
		queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info[queue_family_count].queueFamilyIndex = compute_queue_family;
		queue_info[queue_family_count].queueCount = std::min(transfer_queue_family == compute_queue_family ? 2u : 1u,
		                                                     queue_props[compute_queue_family].queueCount);
		queue_info[queue_family_count].pQueuePriorities = prio + 1;
		queue_family_count++;
	}

	if (transfer_queue_family != graphics_queue_family && transfer_queue_family != compute_queue_family)
	{
		queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info[queue_family_count].queueFamilyIndex = transfer_queue_family;
		queue_info[queue_family_count].queueCount = 1;
		queue_info[queue_family_count].pQueuePriorities = prio + 2;
		queue_family_count++;
	}

	device_info.queueCreateInfoCount = queue_family_count;

	std::vector<const char *> enabled_extensions;
	std::vector<const char *> enabled_layers;

	for (uint32_t i = 0; i < num_required_device_extensions; i++)
		enabled_extensions.push_back(required_device_extensions[i]);
	for (uint32_t i = 0; i < num_required_device_layers; i++)
		enabled_layers.push_back(required_device_layers[i]);

	if (has_vk_extension(queried_extensions, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) &&
	    has_vk_extension(queried_extensions, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME))
	{
		ext.supports_dedicated = true;
		enabled_extensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	}

	if (has_vk_extension(queried_extensions, VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
	{
		ext.supports_debug_marker = true;
		enabled_extensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
	}

#ifdef _WIN32
	ext.supports_external = false;
#else
	if (ext.supports_external && ext.supports_dedicated &&
	    has_vk_extension(queried_extensions, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) &&
	    has_vk_extension(queried_extensions, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) &&
	    has_vk_extension(queried_extensions, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME) &&
	    has_vk_extension(queried_extensions, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME))
	{
		ext.supports_external = true;
		enabled_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
		enabled_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
	}
	else
		ext.supports_external = false;
#endif

	VkPhysicalDeviceFeatures2KHR features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR };

	if (has_vk_extension(queried_extensions, VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME))
		enabled_extensions.push_back(VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);

	vkGetPhysicalDeviceFeatures(gpu, &features.features);

	// Enable device features we might care about.
	{
		VkPhysicalDeviceFeatures enabled_features = *required_features;
		if (features.features.textureCompressionETC2)
			enabled_features.textureCompressionETC2 = VK_TRUE;
		if (features.features.textureCompressionBC)
			enabled_features.textureCompressionBC = VK_TRUE;
		if (features.features.textureCompressionASTC_LDR)
			enabled_features.textureCompressionASTC_LDR = VK_TRUE;
		if (features.features.fullDrawIndexUint32)
			enabled_features.fullDrawIndexUint32 = VK_TRUE;
		if (features.features.imageCubeArray)
			enabled_features.imageCubeArray = VK_TRUE;
		if (features.features.fillModeNonSolid)
			enabled_features.fillModeNonSolid = VK_TRUE;
		if (features.features.independentBlend)
			enabled_features.independentBlend = VK_TRUE;
		if (features.features.sampleRateShading)
			enabled_features.sampleRateShading = VK_TRUE;
		if (features.features.fragmentStoresAndAtomics)
			enabled_features.fragmentStoresAndAtomics = VK_TRUE;
		if (features.features.shaderStorageImageExtendedFormats)
			enabled_features.shaderStorageImageExtendedFormats = VK_TRUE;
		if (features.features.shaderStorageImageMultisample)
			enabled_features.shaderStorageImageMultisample = VK_TRUE;
		if (features.features.largePoints)
			enabled_features.largePoints = VK_TRUE;

		features.features = enabled_features;
		ext.enabled_features = enabled_features;
	}

	device_info.pEnabledFeatures = &features.features;

#ifdef VULKAN_DEBUG
	{
		bool force_no_validation = false;
		const char *no_validation = getenv("GRANITE_VULKAN_NO_VALIDATION");
		if (no_validation && strtoul(no_validation, nullptr, 0) != 0)
			force_no_validation = true;
		if (!force_no_validation && has_vk_layer(queried_layers, "VK_LAYER_LUNARG_standard_validation"))
			enabled_layers.push_back("VK_LAYER_LUNARG_standard_validation");
	}
#endif

	device_info.enabledExtensionCount = enabled_extensions.size();
	device_info.ppEnabledExtensionNames = enabled_extensions.empty() ? nullptr : enabled_extensions.data();
	device_info.enabledLayerCount = enabled_layers.size();
	device_info.ppEnabledLayerNames = enabled_layers.empty() ? nullptr : enabled_layers.data();

	if (vkCreateDevice(gpu, &device_info, nullptr, &device) != VK_SUCCESS)
		return false;

	volkLoadDevice(device);
	vkGetDeviceQueue(device, graphics_queue_family, graphics_queue_index, &graphics_queue);
	vkGetDeviceQueue(device, compute_queue_family, compute_queue_index, &compute_queue);
	vkGetDeviceQueue(device, transfer_queue_family, transfer_queue_index, &transfer_queue);

	return true;
}
}

/* === device.cpp === */


#define LOCK() ((void)0)
#define DRAIN_FRAME_LOCK() VK_ASSERT(lock.counter == 0)

using namespace Util;

namespace Vulkan
{
Device::Device()
    : framebuffer_allocator(this)
    , transient_allocator(this)
{
}

void Device::add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush)
{
	LOCK();
	add_wait_semaphore_nolock(type, semaphore, stages, flush);
}

void Device::add_wait_semaphore_nolock(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages,
                                       bool flush)
{
	VK_ASSERT(stages != 0);
	if (flush)
		flush_frame(type);
	QueueData &data = get_queue_data(type);

#ifdef VULKAN_DEBUG
	for (Semaphore &sem : data.wait_semaphores)
		VK_ASSERT(sem.get() != semaphore.get());
#endif

	data.wait_semaphores.push_back(semaphore);
	data.wait_stages.push_back(stages);
	data.need_fence = true;

	// Sanity check.
	VK_ASSERT(data.wait_semaphores.size() < 16 * 1024);
}

void *Device::map_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
{
	void *host = managers.memory.map_memory(buffer.get_allocation(), access);
	return host;
}

void Device::unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
{
	managers.memory.unmap_memory(buffer.get_allocation(), access);
}

Shader *Device::request_shader(const uint32_t *data, size_t size)
{
	Util::Hasher hasher;
	hasher.data(data, size);

	Hash hash = hasher.get();
	Shader *ret = shaders.find(hash);
	if (!ret)
		ret = shaders.emplace_yield(hash, hash, this, data, size);
	return ret;
}

Program *Device::request_program(Vulkan::Shader *compute)
{
	Util::Hasher hasher;
	hasher.u64(compute->get_hash());

	Hash hash = hasher.get();
	Program *ret = programs.find(hash);
	if (!ret)
		ret = programs.emplace_yield(hash, this, compute);
	return ret;
}

Program *Device::request_program(const uint32_t *compute_data, size_t compute_size)
{
	Shader *compute = request_shader(compute_data, compute_size);
	return request_program(compute);
}

Program *Device::request_program(Shader *vertex, Shader *fragment)
{
	Util::Hasher hasher;
	hasher.u64(vertex->get_hash());
	hasher.u64(fragment->get_hash());

	Hash hash = hasher.get();
	Program *ret = programs.find(hash);

	if (!ret)
		ret = programs.emplace_yield(hash, this, vertex, fragment);
	return ret;
}

Program *Device::request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
                                 size_t fragment_size)
{
	Shader *vertex = request_shader(vertex_data, vertex_size);
	Shader *fragment = request_shader(fragment_data, fragment_size);
	return request_program(vertex, fragment);
}

PipelineLayout *Device::request_pipeline_layout(const CombinedResourceLayout &layout)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(layout.sets), sizeof(layout.sets));
	h.data(&layout.stages_for_bindings[0][0], sizeof(layout.stages_for_bindings));
	h.u32(layout.push_constant_range.stageFlags);
	h.u32(layout.push_constant_range.size);
	h.data(layout.spec_constant_mask, sizeof(layout.spec_constant_mask));
	h.u32(layout.attribute_mask);
	h.u32(layout.render_target_mask);

	Hash hash = h.get();
	PipelineLayout *ret = pipeline_layouts.find(hash);
	if (!ret)
		ret = pipeline_layouts.emplace_yield(hash, hash, this, layout);
	return ret;
}

DescriptorSetAllocator *Device::request_descriptor_set_allocator(const DescriptorSetLayout &layout, const uint32_t *stages_for_bindings)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(&layout), sizeof(layout));
	h.data(stages_for_bindings, sizeof(uint32_t) * VULKAN_NUM_BINDINGS);
	Hash hash = h.get();

	DescriptorSetAllocator *ret = descriptor_set_allocators.find(hash);
	if (!ret)
		ret = descriptor_set_allocators.emplace_yield(hash, hash, this, layout, stages_for_bindings);
	return ret;
}

void Device::bake_program(Program &program)
{
	CombinedResourceLayout layout;
	if (program.get_shader(ShaderStage::Vertex))
		layout.attribute_mask = program.get_shader(ShaderStage::Vertex)->get_layout().input_mask;
	if (program.get_shader(ShaderStage::Fragment))
		layout.render_target_mask = program.get_shader(ShaderStage::Fragment)->get_layout().output_mask;

	layout.descriptor_set_mask = 0;

	for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
	{
		const Shader *shader = program.get_shader(static_cast<ShaderStage>(i));
		if (!shader)
			continue;

		uint32_t stage_mask = 1u << i;

		const ResourceLayout &shader_layout = shader->get_layout();
		for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
		{
			layout.sets[set].sampled_image_mask |= shader_layout.sets[set].sampled_image_mask;
			layout.sets[set].storage_image_mask |= shader_layout.sets[set].storage_image_mask;
			layout.sets[set].uniform_buffer_mask |= shader_layout.sets[set].uniform_buffer_mask;
			layout.sets[set].storage_buffer_mask |= shader_layout.sets[set].storage_buffer_mask;
			layout.sets[set].sampled_buffer_mask |= shader_layout.sets[set].sampled_buffer_mask;
			layout.sets[set].input_attachment_mask |= shader_layout.sets[set].input_attachment_mask;
			layout.sets[set].sampler_mask |= shader_layout.sets[set].sampler_mask;
			layout.sets[set].separate_image_mask |= shader_layout.sets[set].separate_image_mask;
			layout.sets[set].fp_mask |= shader_layout.sets[set].fp_mask;

			FOR_EACH_BIT(shader_layout.sets[set].immutable_sampler_mask, binding)
			{
				StockSampler sampler = get_immutable_sampler(shader_layout.sets[set], binding);

				// Do we already have an immutable sampler? Make sure it matches the layout.
				if (has_immutable_sampler(layout.sets[set], binding))
				{
					if (sampler != get_immutable_sampler(layout.sets[set], binding))
						LOGE("Immutable sampler mismatch detected!\n");
				}

				set_immutable_sampler(layout.sets[set], binding, sampler);
						}

			uint32_t active_binds =
					shader_layout.sets[set].sampled_image_mask |
					shader_layout.sets[set].storage_image_mask |
					shader_layout.sets[set].uniform_buffer_mask|
					shader_layout.sets[set].storage_buffer_mask |
					shader_layout.sets[set].sampled_buffer_mask |
					shader_layout.sets[set].input_attachment_mask |
					shader_layout.sets[set].sampler_mask |
					shader_layout.sets[set].separate_image_mask;

			if (active_binds)
				layout.stages_for_sets[set] |= stage_mask;

			FOR_EACH_BIT(active_binds, bit)
			{
				layout.stages_for_bindings[set][bit] |= stage_mask;
						}
		}

		// Merge push constant ranges into one range.
		// Do not try to split into multiple ranges as it just complicates things for no obvious gain.
		if (shader_layout.push_constant_size != 0)
		{
			layout.push_constant_range.stageFlags |= 1u << i;
			layout.push_constant_range.size =
					std::max(layout.push_constant_range.size, shader_layout.push_constant_size);
		}

		layout.spec_constant_mask[i] = shader_layout.spec_constant_mask;
		layout.combined_spec_constant_mask |= shader_layout.spec_constant_mask;
	}

	for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
	{
		if (layout.stages_for_sets[i] != 0)
			layout.descriptor_set_mask |= 1u << i;
	}

	Hasher h;
	h.u32(layout.push_constant_range.stageFlags);
	h.u32(layout.push_constant_range.size);
	layout.push_constant_layout_hash = h.get();
	program.set_pipeline_layout(request_pipeline_layout(layout));
}

void Device::init_workarounds()
{
	// srcStageMask = ALL_GRAPHICS_BIT causes some weird stalls compared to waiting for fragment only.
	workarounds.optimize_all_graphics_barrier = gpu_props.vendorID == VENDOR_ID_ARM;
}

void Device::set_context(const Context &context)
{
	instance = context.get_instance();
	gpu = context.get_gpu();
	device = context.get_device();

	graphics_queue_family_index = context.get_graphics_queue_family();
	graphics_queue = context.get_graphics_queue();
	compute_queue_family_index = context.get_compute_queue_family();
	compute_queue = context.get_compute_queue();
	transfer_queue_family_index = context.get_transfer_queue_family();
	transfer_queue = context.get_transfer_queue();

	mem_props = context.get_mem_props();
	gpu_props = context.get_gpu_props();

	init_workarounds();

	init_stock_samplers();

#ifdef ANDROID
	init_frame_contexts(3); // Android needs a bit more ... ;)
#else
	init_frame_contexts(2); // By default, regular double buffer between CPU and GPU.
#endif

	ext = context.get_enabled_device_features();

	managers.memory.init(gpu, device);
	managers.memory.set_supports_dedicated_allocation(ext.supports_dedicated);
	managers.semaphore.init(device);
	managers.fence.init(device);
	managers.vbo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	managers.ubo.init(this, 256 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.minUniformBufferOffsetAlignment),
	                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}

void Device::init_stock_samplers()
{
	SamplerCreateInfo info = {};
	info.max_lod = VK_LOD_CLAMP_NONE;
	info.max_anisotropy = 1.0f;

	for (unsigned i = 0; i < static_cast<unsigned>(StockSampler::Count); i++)
	{
		StockSampler mode = static_cast<StockSampler>(i);

		switch (mode)
		{
		case StockSampler::NearestShadow:
		case StockSampler::LinearShadow:
			info.compare_enable = true;
			info.compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
			break;

		default:
			info.compare_enable = false;
			break;
		}

		switch (mode)
		{
		case StockSampler::TrilinearClamp:
		case StockSampler::TrilinearWrap:
			info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			break;

		default:
			info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		}

		switch (mode)
		{
		case StockSampler::LinearClamp:
		case StockSampler::LinearWrap:
		case StockSampler::TrilinearClamp:
		case StockSampler::TrilinearWrap:
		case StockSampler::LinearShadow:
			info.mag_filter = VK_FILTER_LINEAR;
			info.min_filter = VK_FILTER_LINEAR;
			break;

		default:
			info.mag_filter = VK_FILTER_NEAREST;
			info.min_filter = VK_FILTER_NEAREST;
			break;
		}

		switch (mode)
		{
		default:
		case StockSampler::LinearWrap:
		case StockSampler::NearestWrap:
		case StockSampler::TrilinearWrap:
			info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;

		case StockSampler::LinearClamp:
		case StockSampler::NearestClamp:
		case StockSampler::TrilinearClamp:
		case StockSampler::NearestShadow:
		case StockSampler::LinearShadow:
			info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		}
		samplers[i] = create_sampler(info, mode);
	}
}

static void request_block(Device &device, BufferBlock &block, VkDeviceSize size,
                          BufferPool &pool, std::vector<BufferBlock> *dma, std::vector<BufferBlock> &recycle)
{
	if (block.mapped)
		device.unmap_host_buffer(*block.cpu, MEMORY_ACCESS_WRITE_BIT);

	if (block.offset == 0)
	{
		if (block.size == pool.get_block_size())
			pool.recycle_block(std::move(block));
	}
	else
	{
		if (block.cpu != block.gpu)
		{
			VK_ASSERT(dma);
			dma->push_back(block);
		}

		if (block.size == pool.get_block_size())
			recycle.push_back(block);
	}

	if (size)
		block = pool.request_block(size);
	else
		block = {};
}

void Device::request_vertex_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_vertex_block_nolock(block, size);
}

void Device::request_vertex_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.vbo, &dma.vbo, frame().vbo_blocks);
}

void Device::request_uniform_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_uniform_block_nolock(block, size);
}

void Device::request_uniform_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.ubo, &dma.ubo, frame().ubo_blocks);
}

void Device::submit(CommandBufferHandle &cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	LOCK();
	submit_nolock(std::move(cmd), fence, semaphore_count, semaphores);
}

CommandBuffer::Type Device::get_physical_queue_type(CommandBuffer::Type queue_type) const
{
	if (queue_type != CommandBuffer::Type::AsyncGraphics)
	{
		return queue_type;
	}
	else
	{
		if (graphics_queue_family_index == compute_queue_family_index && graphics_queue != compute_queue)
			return CommandBuffer::Type::AsyncCompute;
		else
			return CommandBuffer::Type::Generic;
	}
}

void Device::submit_nolock(CommandBufferHandle cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	CommandBuffer::Type type = cmd->get_command_buffer_type();
	CommandPool &pool = get_command_pool(type);
	std::vector<CommandBufferHandle> &submissions = get_queue_submissions(type);

	pool.signal_submitted(cmd->get_command_buffer());
	cmd->end();
	submissions.push_back(std::move(cmd));

	VkFence cleared_fence = VK_NULL_HANDLE;

	if (fence || semaphore_count)
		submit_queue(type, fence ? &cleared_fence : nullptr, semaphore_count, semaphores);

	if (fence)
	{
		VK_ASSERT(!*fence);
		*fence = Fence(handle_pool.fences.allocate(this, cleared_fence));
	}

	decrement_frame_counter_nolock();
}

void Device::submit_empty_inner(CommandBuffer::Type type, VkFence *fence,
                                unsigned semaphore_count, Semaphore *semaphores)
{
	QueueData &data = get_queue_data(type);
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

	// Add external wait semaphores.
	std::vector<VkSemaphore> waits;
	std::vector<VkSemaphore> signals;
	std::vector<VkPipelineStageFlags> stages = std::move(data.wait_stages);

	for (Semaphore &semaphore : data.wait_semaphores)
	{
		VkSemaphore wait = semaphore->consume();
		frame().recycled_semaphores.push_back(wait);
		waits.push_back(wait);
	}
	data.wait_stages.clear();
	data.wait_semaphores.clear();

	// Add external signal semaphores.
	for (unsigned i = 0; i < semaphore_count; i++)
	{
		VkSemaphore cleared_semaphore = managers.semaphore.request_cleared_semaphore();
		signals.push_back(cleared_semaphore);
		VK_ASSERT(!semaphores[i]);
		semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true));
	}

	submit.signalSemaphoreCount = signals.size();
	submit.waitSemaphoreCount = waits.size();
	if (!signals.empty())
		submit.pSignalSemaphores = signals.data();
	if (!stages.empty())
		submit.pWaitDstStageMask = stages.data();
	if (!waits.empty())
		submit.pWaitSemaphores = waits.data();

	VkQueue queue;
	switch (type)
	{
	default:
	case CommandBuffer::Type::Generic:
		queue = graphics_queue;
		break;
	case CommandBuffer::Type::AsyncCompute:
		queue = compute_queue;
		break;
	case CommandBuffer::Type::AsyncTransfer:
		queue = transfer_queue;
		break;
	}

	VkFence cleared_fence = fence ? managers.fence.request_cleared_fence() : VK_NULL_HANDLE;
	VkResult result = vkQueueSubmit(queue, 1, &submit, cleared_fence);

	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit failed (code: %d).\n", int(result));

	if (fence)
	{
		frame().wait_fences.push_back(cleared_fence);
		*fence = cleared_fence;
		data.need_fence = false;
	}
	else
		data.need_fence = true;
}

void Device::submit_staging(CommandBufferHandle &cmd, VkBufferUsageFlags usage, bool flush)
{
	VkAccessFlags access = buffer_usage_to_possible_access(usage);
	VkPipelineStageFlags stages = buffer_usage_to_possible_stages(usage);

	if (transfer_queue == graphics_queue && transfer_queue == compute_queue)
	{
		// For single-queue systems, just use a pipeline barrier.
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, stages, access);
		submit_nolock(cmd, nullptr, 0, nullptr);
	}
	else
	{
		VkPipelineStageFlags compute_stages = stages &
		                      (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
		                       VK_PIPELINE_STAGE_TRANSFER_BIT |
		                       VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

		VkAccessFlags compute_access = access &
		                      (VK_ACCESS_SHADER_READ_BIT |
		                       VK_ACCESS_SHADER_WRITE_BIT |
		                       VK_ACCESS_TRANSFER_READ_BIT |
		                       VK_ACCESS_UNIFORM_READ_BIT |
		                       VK_ACCESS_TRANSFER_WRITE_BIT |
		                       VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

		VkPipelineStageFlags graphics_stages = stages;

		if (transfer_queue == graphics_queue)
		{
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             graphics_stages, access);

			if (compute_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
			}
			else
				submit_nolock(cmd, nullptr, 0, nullptr);
		}
		else if (transfer_queue == compute_queue)
		{
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             compute_stages, compute_access);

			if (graphics_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
			}
			else
				submit_nolock(cmd, nullptr, 0, nullptr);
		}
		else
		{
			if (graphics_stages != 0 && compute_stages != 0)
			{
				Semaphore semaphores[2];
				submit_nolock(cmd, nullptr, 2, semaphores);
				add_wait_semaphore_nolock(CommandBuffer::Type::Generic, semaphores[0], graphics_stages, flush);
				add_wait_semaphore_nolock(CommandBuffer::Type::AsyncCompute, semaphores[1], compute_stages, flush);
			}
			else if (graphics_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
			}
			else if (compute_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
			}
			else
				submit_nolock(cmd, nullptr, 0, nullptr);
		}
	}
}

void Device::submit_queue(CommandBuffer::Type type, VkFence *fence,
                          unsigned semaphore_count, Semaphore *semaphores)
{
	type = get_physical_queue_type(type);

	// Always check if we need to flush pending transfers.
	if (type != CommandBuffer::Type::AsyncTransfer)
		flush_frame(CommandBuffer::Type::AsyncTransfer);

	QueueData &data = get_queue_data(type);
	std::vector<CommandBufferHandle> &submissions = get_queue_submissions(type);

	if (submissions.empty())
	{
		if (fence || semaphore_count)
			submit_empty_inner(type, fence, semaphore_count, semaphores);
		return;
	}

	std::vector<VkCommandBuffer> cmds;
	cmds.reserve(submissions.size());

	std::vector<VkSubmitInfo> submits;
	submits.reserve(2);
	size_t last_cmd = 0;

	std::vector<VkSemaphore> waits[2];
	std::vector<VkSemaphore> signals[2];
	std::vector<VkFlags> stages[2];

	// Add external wait semaphores.
	stages[0] = std::move(data.wait_stages);

	for (Semaphore &semaphore : data.wait_semaphores)
	{
		VkSemaphore wait = semaphore->consume();
		frame().recycled_semaphores.push_back(wait);
		waits[0].push_back(wait);
	}
	data.wait_stages.clear();
	data.wait_semaphores.clear();

	for (CommandBufferHandle &cmd : submissions)
		cmds.push_back(cmd->get_command_buffer());

	if (cmds.size() > last_cmd)
	{
		// Push all pending cmd buffers to their own submission.
		submits.emplace_back();

		VkSubmitInfo &submit = submits.back();
		memset(&submit, 0, sizeof(submit));
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.pNext = nullptr;
		submit.commandBufferCount = cmds.size() - last_cmd;
		submit.pCommandBuffers = cmds.data() + last_cmd;
		last_cmd = cmds.size();
	}

	VkFence cleared_fence = fence ? managers.fence.request_cleared_fence() : VK_NULL_HANDLE;

	for (unsigned i = 0; i < semaphore_count; i++)
	{
		VkSemaphore cleared_semaphore = managers.semaphore.request_cleared_semaphore();
		signals[submits.size() - 1].push_back(cleared_semaphore);
		VK_ASSERT(!semaphores[i]);
		semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true));
	}

	for (unsigned i = 0; i < submits.size(); i++)
	{
		VkSubmitInfo &submit = submits[i];
		submit.waitSemaphoreCount = waits[i].size();
		if (!waits[i].empty())
		{
			submit.pWaitSemaphores = waits[i].data();
			submit.pWaitDstStageMask = stages[i].data();
		}

		submit.signalSemaphoreCount = signals[i].size();
		if (!signals[i].empty())
			submit.pSignalSemaphores = signals[i].data();
	}

	VkQueue queue;
	switch (type)
	{
	default:
	case CommandBuffer::Type::Generic:
		queue = graphics_queue;
		break;
	case CommandBuffer::Type::AsyncCompute:
		queue = compute_queue;
		break;
	case CommandBuffer::Type::AsyncTransfer:
		queue = transfer_queue;
		break;
	}

	VkResult result = vkQueueSubmit(queue, submits.size(), submits.data(), cleared_fence);
	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit failed (code: %d).\n", int(result));
	submissions.clear();

	if (fence)
	{
		frame().wait_fences.push_back(cleared_fence);
		*fence = cleared_fence;
		data.need_fence = false;
	}
	else
		data.need_fence = true;
}

void Device::flush_frame(CommandBuffer::Type type)
{
	if (type == CommandBuffer::Type::AsyncTransfer)
		sync_buffer_blocks();
	submit_queue(type, nullptr, 0, nullptr);
}

void Device::sync_buffer_blocks()
{
	if (dma.vbo.empty() && dma.ubo.empty())
		return;

	VkBufferUsageFlags usage = 0;

	CommandBufferHandle cmd = request_command_buffer_nolock(CommandBuffer::Type::AsyncTransfer);

	cmd->begin_region("buffer-block-sync");

	for (BufferBlock &block : dma.vbo)
	{
		VK_ASSERT(block.offset != 0);
		cmd->copy_buffer(*block.gpu, 0, *block.cpu, 0, block.offset);
		usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}

	for (BufferBlock &block : dma.ubo)
	{
		VK_ASSERT(block.offset != 0);
		cmd->copy_buffer(*block.gpu, 0, *block.cpu, 0, block.offset);
		usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}

	dma.vbo.clear();
	dma.ubo.clear();

	cmd->end_region();

	// Do not flush graphics or compute in this context.
	// We must be able to inject semaphores into all currently enqueued graphics / compute.
	submit_staging(cmd, usage, false);
}

void Device::end_frame_nolock()
{
	// Make sure we have a fence which covers all submissions in the frame.
	VkFence fence;

	if (transfer.need_fence || !frame().transfer_submissions.empty())
	{
		submit_queue(CommandBuffer::Type::AsyncTransfer, &fence, 0, nullptr);
		frame().recycle_fences.push_back(fence);
		transfer.need_fence = false;
	}

	if (graphics.need_fence || !frame().graphics_submissions.empty())
	{
		submit_queue(CommandBuffer::Type::Generic, &fence, 0, nullptr);
		frame().recycle_fences.push_back(fence);
		graphics.need_fence = false;
	}

	if (compute.need_fence || !frame().compute_submissions.empty())
	{
		submit_queue(CommandBuffer::Type::AsyncCompute, &fence, 0, nullptr);
		frame().recycle_fences.push_back(fence);
		compute.need_fence = false;
	}
}

void Device::flush_frame()
{
	LOCK();
	flush_frame_nolock();
}

void Device::flush_frame_nolock()
{
	flush_frame(CommandBuffer::Type::AsyncTransfer);
	flush_frame(CommandBuffer::Type::Generic);
	flush_frame(CommandBuffer::Type::AsyncCompute);
}

Device::QueueData &Device::get_queue_data(CommandBuffer::Type type)
{
	switch (get_physical_queue_type(type))
	{
	default:
	case CommandBuffer::Type::Generic:
		return graphics;
	case CommandBuffer::Type::AsyncCompute:
		return compute;
	case CommandBuffer::Type::AsyncTransfer:
		return transfer;
	}
}

CommandPool &Device::get_command_pool(CommandBuffer::Type type)
{
	switch (get_physical_queue_type(type))
	{
	default:
	case CommandBuffer::Type::Generic:
		return frame().graphics_cmd_pool;
	case CommandBuffer::Type::AsyncCompute:
		return frame().compute_cmd_pool;
	case CommandBuffer::Type::AsyncTransfer:
		return frame().transfer_cmd_pool;
	}
}

std::vector<CommandBufferHandle> &Device::get_queue_submissions(CommandBuffer::Type type)
{
	switch (get_physical_queue_type(type))
	{
	default:
	case CommandBuffer::Type::Generic:
		return frame().graphics_submissions;
	case CommandBuffer::Type::AsyncCompute:
		return frame().compute_submissions;
	case CommandBuffer::Type::AsyncTransfer:
		return frame().transfer_submissions;
	}
}

CommandBufferHandle Device::request_command_buffer(CommandBuffer::Type type)
{
	LOCK();
	return request_command_buffer_nolock(type);
}

CommandBufferHandle Device::request_command_buffer_nolock(CommandBuffer::Type type)
{
	VkCommandBuffer cmd = get_command_pool(type).request_command_buffer();

	VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &info);
	add_frame_counter_nolock();
	CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, type));
	return handle;
}

const Sampler &Device::get_stock_sampler(StockSampler sampler) const
{
	return *samplers[static_cast<unsigned>(sampler)];
}

Device::~Device()
{
	wait_idle();

	framebuffer_allocator.clear();
	transient_allocator.clear();
	for (SamplerHandle &sampler : samplers)
		sampler.reset();
}

void Device::init_frame_contexts(unsigned count)
{
	DRAIN_FRAME_LOCK();
	wait_idle_nolock();

	// Clear out caches which might contain stale data from now on.
	framebuffer_allocator.clear();
	transient_allocator.clear();
	per_frame.clear();

	for (unsigned i = 0; i < count; i++)
	{
		std::unique_ptr<PerFrame> frame = std::unique_ptr<PerFrame>(new PerFrame(this));
		per_frame.emplace_back(std::move(frame));
	}
}

Device::PerFrame::PerFrame(Device *device)
    : device(device->get_device())
    , managers(device->managers)
    , graphics_cmd_pool(device->get_device(), device->graphics_queue_family_index)
    , compute_cmd_pool(device->get_device(), device->compute_queue_family_index)
    , transfer_cmd_pool(device->get_device(), device->transfer_queue_family_index)
{
}

void Device::free_memory_nolock(const DeviceAllocation &alloc)
{
	frame().allocations.push_back(alloc);
}

#ifdef VULKAN_DEBUG

template <typename T, typename U>
static inline bool exists(const T &container, const U &value)
{
	return std::find(container.begin(), container.end(), value) != container.end();
}

#endif

void Device::reset_fence(VkFence fence)
{
	LOCK();
	frame().recycle_fences.push_back(fence);
}

void Device::destroy_pipeline_nolock(VkPipeline pipeline)
{
	VK_ASSERT(!exists(frame().destroyed_pipelines, pipeline));
	frame().destroyed_pipelines.push_back(pipeline);
}

void Device::destroy_image_view_nolock(VkImageView view)
{
	VK_ASSERT(!exists(frame().destroyed_image_views, view));
	frame().destroyed_image_views.push_back(view);
}

void Device::destroy_buffer_view_nolock(VkBufferView view)
{
	VK_ASSERT(!exists(frame().destroyed_buffer_views, view));
	frame().destroyed_buffer_views.push_back(view);
}

void Device::destroy_semaphore_nolock(VkSemaphore semaphore)
{
	VK_ASSERT(!exists(frame().destroyed_semaphores, semaphore));
	frame().destroyed_semaphores.push_back(semaphore);
}

void Device::recycle_semaphore_nolock(VkSemaphore semaphore)
{
	managers.semaphore.recycle(semaphore);
}

void Device::destroy_image_nolock(VkImage image)
{
	VK_ASSERT(!exists(frame().destroyed_images, image));
	frame().destroyed_images.push_back(image);
}

void Device::destroy_buffer_nolock(VkBuffer buffer)
{
	VK_ASSERT(!exists(frame().destroyed_buffers, buffer));
	frame().destroyed_buffers.push_back(buffer);
}

void Device::destroy_sampler_nolock(VkSampler sampler)
{
	VK_ASSERT(!exists(frame().destroyed_samplers, sampler));
	frame().destroyed_samplers.push_back(sampler);
}

void Device::destroy_framebuffer_nolock(VkFramebuffer framebuffer)
{
	VK_ASSERT(!exists(frame().destroyed_framebuffers, framebuffer));
	frame().destroyed_framebuffers.push_back(framebuffer);
}

void Device::clear_wait_semaphores()
{
	for (Semaphore &sem : graphics.wait_semaphores)
		vkDestroySemaphore(device, sem->consume(), nullptr);
	for (Semaphore &sem : compute.wait_semaphores)
		vkDestroySemaphore(device, sem->consume(), nullptr);
	for (Semaphore &sem : transfer.wait_semaphores)
		vkDestroySemaphore(device, sem->consume(), nullptr);

	graphics.wait_semaphores.clear();
	graphics.wait_stages.clear();
	compute.wait_semaphores.clear();
	compute.wait_stages.clear();
	transfer.wait_semaphores.clear();
	transfer.wait_stages.clear();
}

void Device::wait_idle()
{
	DRAIN_FRAME_LOCK();
	wait_idle_nolock();
}

void Device::wait_idle_nolock()
{
	if (!per_frame.empty())
		end_frame_nolock();

	if (device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(device);

	clear_wait_semaphores();

	// Free memory for buffer pools.
	managers.vbo.reset();
	managers.ubo.reset();
	for (std::unique_ptr<PerFrame> &frame : per_frame)
	{
		frame->vbo_blocks.clear();
		frame->ubo_blocks.clear();
	}

	framebuffer_allocator.clear();
	transient_allocator.clear();
	for (DescriptorSetAllocator &allocator : descriptor_set_allocators)
		allocator.clear();

	for (std::unique_ptr<PerFrame> &frame : per_frame)
	{
		// We have done WaitIdle, no need to wait for extra fences, it's also not safe.
		frame->wait_fences.clear();
		frame->begin();
	}
}

void Device::next_frame_context()
{
	DRAIN_FRAME_LOCK();

	// Flush the frame here as we might have pending staging command buffers from init stage.
	end_frame_nolock();

	framebuffer_allocator.begin_frame();
	transient_allocator.begin_frame();
	for (DescriptorSetAllocator &allocator : descriptor_set_allocators)
		allocator.begin_frame();

	VK_ASSERT(!per_frame.empty());
	frame_context_index++;
	if (frame_context_index >= per_frame.size())
		frame_context_index = 0;

	frame().begin();
}

void Device::add_frame_counter_nolock()
{
	lock.counter++;
}

void Device::decrement_frame_counter_nolock()
{
	VK_ASSERT(lock.counter > 0);
	lock.counter--;
}

void Device::PerFrame::begin()
{
	if (!wait_fences.empty())
	{
		vkWaitForFences(device, wait_fences.size(), wait_fences.data(), VK_TRUE, UINT64_MAX);
		wait_fences.clear();
	}

	if (!recycle_fences.empty())
	{
		vkResetFences(device, recycle_fences.size(), recycle_fences.data());
		for (VkFence &fence : recycle_fences)
			managers.fence.recycle_fence(fence);
		recycle_fences.clear();
	}

	graphics_cmd_pool.begin();
	compute_cmd_pool.begin();
	transfer_cmd_pool.begin();

	for (VkFramebuffer &framebuffer : destroyed_framebuffers)
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	for (VkSampler &sampler : destroyed_samplers)
		vkDestroySampler(device, sampler, nullptr);
	for (VkPipeline &pipeline : destroyed_pipelines)
		vkDestroyPipeline(device, pipeline, nullptr);
	for (VkImageView &view : destroyed_image_views)
		vkDestroyImageView(device, view, nullptr);
	for (VkBufferView &view : destroyed_buffer_views)
		vkDestroyBufferView(device, view, nullptr);
	for (VkImage &image : destroyed_images)
		vkDestroyImage(device, image, nullptr);
	for (VkBuffer &buffer : destroyed_buffers)
		vkDestroyBuffer(device, buffer, nullptr);
	for (VkSemaphore &semaphore : destroyed_semaphores)
		vkDestroySemaphore(device, semaphore, nullptr);
	for (VkSemaphore &semaphore : recycled_semaphores)
	{
		managers.semaphore.recycle(semaphore);
	}
	for (DeviceAllocation &alloc : allocations)
		alloc.free_immediate(managers.memory);

	for (BufferBlock &block : vbo_blocks)
		managers.vbo.recycle_block(std::move(block));
	for (BufferBlock &block : ubo_blocks)
		managers.ubo.recycle_block(std::move(block));
	vbo_blocks.clear();
	ubo_blocks.clear();

	destroyed_framebuffers.clear();
	destroyed_samplers.clear();
	destroyed_pipelines.clear();
	destroyed_image_views.clear();
	destroyed_buffer_views.clear();
	destroyed_images.clear();
	destroyed_buffers.clear();
	destroyed_semaphores.clear();
	recycled_semaphores.clear();
	allocations.clear();
}

Device::PerFrame::~PerFrame()
{
	begin();
}

uint32_t Device::find_memory_type(BufferDomain domain, uint32_t mask)
{
	uint32_t desired = 0, fallback = 0;
	switch (domain)
	{
	case BufferDomain::Device:
		desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = 0;
		break;

	case BufferDomain::Host:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;

	case BufferDomain::CachedHost:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & desired) == desired)
				return i;
		}
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & fallback) == fallback)
				return i;
		}
	}

	LOGE("Couldn't find memory type for buffer domain.\n");
	return UINT32_MAX;
}

uint32_t Device::find_memory_type(ImageDomain domain, uint32_t mask)
{
	uint32_t desired = 0, fallback = 0;
	switch (domain)
	{
	case ImageDomain::Physical:
		desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = 0;
		break;

	case ImageDomain::Transient:
		desired = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
		fallback = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & desired) == desired)
				return i;
		}
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & fallback) == fallback)
				return i;
		}
	}

	LOGE("Couldn't find memory type for image domain.\n");
	return UINT32_MAX;
}

static inline VkImageViewType get_image_view_type(const ImageCreateInfo &create_info, const ImageViewCreateInfo *view)
{
	unsigned layers = view ? view->layers : create_info.layers;
	unsigned base_layer = view ? view->base_layer : 0;

	if (layers == VK_REMAINING_ARRAY_LAYERS)
		layers = create_info.layers - base_layer;

	switch (create_info.type)
	{
	case VK_IMAGE_TYPE_1D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height == 1);
		VK_ASSERT(create_info.depth == 1);
		VK_ASSERT(create_info.samples == VK_SAMPLE_COUNT_1_BIT);

		if (layers > 1)
			return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
		else
			return VK_IMAGE_VIEW_TYPE_1D;

	case VK_IMAGE_TYPE_2D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height >= 1);
		VK_ASSERT(create_info.depth == 1);

		if ((create_info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && (layers % 6) == 0)
		{
			VK_ASSERT(create_info.width == create_info.height);

			if (layers > 6)
				return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			else
				return VK_IMAGE_VIEW_TYPE_CUBE;
		}
		else
		{
			if (layers > 1)
				return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			else
				return VK_IMAGE_VIEW_TYPE_2D;
		}

	case VK_IMAGE_TYPE_3D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height >= 1);
		VK_ASSERT(create_info.depth >= 1);
		return VK_IMAGE_VIEW_TYPE_3D;

	default:
		VK_ASSERT(0 && "bogus");
		return VK_IMAGE_VIEW_TYPE_RANGE_SIZE;
	}
}

BufferViewHandle Device::create_buffer_view(const BufferViewCreateInfo &view_info)
{
	VkBufferViewCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
	info.buffer = view_info.buffer->get_buffer();
	info.format = view_info.format;
	info.offset = view_info.offset;
	info.range = view_info.range;

	VkBufferView view;
	VkResult res = vkCreateBufferView(device, &info, nullptr, &view);
	if (res != VK_SUCCESS)
		return BufferViewHandle(nullptr);

	return BufferViewHandle(handle_pool.buffer_views.allocate(this, view, view_info));
}

class ImageResourceHolder
{
public:
	ImageResourceHolder(VkDevice device)
		: device(device)
	{
	}

	~ImageResourceHolder()
	{
		if (owned)
			cleanup();
	}

	VkDevice device;

	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView image_view = VK_NULL_HANDLE;
	VkImageView depth_view = VK_NULL_HANDLE;
	VkImageView stencil_view = VK_NULL_HANDLE;
	std::vector<VkImageView> rt_views;
	DeviceAllocation allocation;
	DeviceAllocator *allocator = nullptr;
	bool owned = true;

	bool create_default_views(const ImageCreateInfo &create_info, const VkImageViewCreateInfo *view_info)
	{
		if ((create_info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) == 0)
		{
			LOGE("Cannot create image view unless certain usage flags are present.\n");
			return false;
		}

		VkImageViewCreateInfo default_view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		if (!view_info)
		{
			default_view_info.image = image;
			default_view_info.format = create_info.format;
			default_view_info.components = create_info.swizzle;
			default_view_info.subresourceRange.aspectMask = format_to_aspect_mask(default_view_info.format);
			default_view_info.viewType = get_image_view_type(create_info, nullptr);
			default_view_info.subresourceRange.baseMipLevel = 0;
			default_view_info.subresourceRange.baseArrayLayer = 0;
			default_view_info.subresourceRange.levelCount = create_info.levels;
			default_view_info.subresourceRange.layerCount = create_info.layers;
			view_info = &default_view_info;
		}

		if (!create_alt_views(create_info, *view_info))
			return false;

		if (!create_render_target_views(create_info, *view_info))
			return false;

		if (!create_default_view(*view_info))
			return false;

		return true;
	}

private:
	bool create_render_target_views(const ImageCreateInfo &image_create_info, const VkImageViewCreateInfo &info)
	{
		rt_views.reserve(info.subresourceRange.layerCount);

		if (info.viewType == VK_IMAGE_VIEW_TYPE_3D)
			return true;

		// If we have a render target, and non-trivial case (layers = 1, levels = 1),
		// create an array of render targets which correspond to each layer (mip 0).
		if ((image_create_info.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0 &&
		    ((info.subresourceRange.levelCount > 1) || (info.subresourceRange.layerCount > 1)))
		{
			VkImageViewCreateInfo view_info = info;
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.subresourceRange.baseMipLevel = info.subresourceRange.baseMipLevel;
			for (uint32_t layer = 0; layer < info.subresourceRange.layerCount; layer++)
			{
				view_info.subresourceRange.levelCount = 1;
				view_info.subresourceRange.layerCount = 1;
				view_info.subresourceRange.baseArrayLayer = layer + info.subresourceRange.baseArrayLayer;

				VkImageView rt_view;
				if (vkCreateImageView(device, &view_info, nullptr, &rt_view) != VK_SUCCESS)
					return false;

				rt_views.push_back(rt_view);
			}
		}

		return true;
	}

	bool create_alt_views(const ImageCreateInfo &image_create_info, const VkImageViewCreateInfo &info)
	{
		if (info.viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
		    info.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY ||
		    info.viewType == VK_IMAGE_VIEW_TYPE_3D)
		{
			return true;
		}

		if (info.subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
		{
			if ((image_create_info.usage & ~VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
			{
				// Sanity check. Don't want to implement layered views for this.
				if (info.subresourceRange.levelCount > 1)
				{
					LOGE("Cannot create depth stencil attachments with more than 1 mip level currently, and non-DS usage flags.\n");
					return false;
				}

				if (info.subresourceRange.layerCount > 1)
				{
					LOGE("Cannot create layered depth stencil attachments with non-DS usage flags.\n");
					return false;
				}

				VkImageViewCreateInfo view_info = info;

				// We need this to be able to sample the texture, or otherwise use it as a non-pure DS attachment.
				view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				if (vkCreateImageView(device, &view_info, nullptr, &depth_view) != VK_SUCCESS)
					return false;

				view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
				if (vkCreateImageView(device, &view_info, nullptr, &stencil_view) != VK_SUCCESS)
					return false;
			}
		}

		return true;
	}

	bool create_default_view(const VkImageViewCreateInfo &info)
	{
		// Create the normal image view. This one contains every subresource.
		if (vkCreateImageView(device, &info, nullptr, &image_view) != VK_SUCCESS)
			return false;

		return true;
	}

	void cleanup()
	{
		if (image_view)
			vkDestroyImageView(device, image_view, nullptr);
		if (depth_view)
			vkDestroyImageView(device, depth_view, nullptr);
		if (stencil_view)
			vkDestroyImageView(device, stencil_view, nullptr);
		for (VkImageView &view : rt_views)
			vkDestroyImageView(device, view, nullptr);

		if (image)
			vkDestroyImage(device, image, nullptr);
		if (memory)
			vkFreeMemory(device, memory, nullptr);
		if (allocator)
			allocation.free_immediate(*allocator);
	}
};

ImageViewHandle Device::create_image_view(const ImageViewCreateInfo &create_info)
{
	ImageResourceHolder holder(device);
	const ImageCreateInfo &image_create_info = create_info.image->get_create_info();

	VkFormat format = create_info.format != VK_FORMAT_UNDEFINED ? create_info.format : image_create_info.format;

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = create_info.image->get_image();
	view_info.format = format;
	view_info.components = create_info.swizzle;
	view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
	view_info.subresourceRange.baseMipLevel = create_info.base_level;
	view_info.subresourceRange.baseArrayLayer = create_info.base_layer;
	view_info.subresourceRange.levelCount = create_info.levels;
	view_info.subresourceRange.layerCount = create_info.layers;
	view_info.viewType = get_image_view_type(image_create_info, &create_info);

	unsigned num_levels;
	if (view_info.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS)
		num_levels = create_info.image->get_create_info().levels - view_info.subresourceRange.baseMipLevel;
	else
		num_levels = view_info.subresourceRange.levelCount;

	unsigned num_layers;
	if (view_info.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS)
		num_layers = create_info.image->get_create_info().layers - view_info.subresourceRange.baseArrayLayer;
	else
		num_layers = view_info.subresourceRange.layerCount;

	view_info.subresourceRange.levelCount = num_levels;
	view_info.subresourceRange.layerCount = num_layers;

	if (!holder.create_default_views(image_create_info, &view_info))
		return ImageViewHandle(nullptr);

	ImageViewCreateInfo tmp = create_info;
	tmp.format = format;
	ImageViewHandle ret(handle_pool.image_views.allocate(this, holder.image_view, tmp));
	if (ret)
	{
		holder.owned = false;
		ret->set_alt_views(holder.depth_view, holder.stencil_view);
		ret->set_render_target_views(std::move(holder.rt_views));
		return ret;
	}
	else
		return ImageViewHandle(nullptr);
}

InitialImageBuffer Device::create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial)
{
	InitialImageBuffer result;

	bool generate_mips = (info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;
	TextureFormatLayout layout;

	unsigned copy_levels;
	if (generate_mips)
		copy_levels = 1;
	else if (info.levels == 0)
		copy_levels = TextureFormatLayout::num_miplevels(info.width, info.height, info.depth);
	else
		copy_levels = info.levels;

	switch (info.type)
	{
	case VK_IMAGE_TYPE_1D:
		layout.set_1d(info.format, info.width, info.layers, copy_levels);
		break;
	case VK_IMAGE_TYPE_2D:
		layout.set_2d(info.format, info.width, info.height, info.layers, copy_levels);
		break;
	case VK_IMAGE_TYPE_3D:
		layout.set_3d(info.format, info.width, info.height, info.depth, copy_levels);
		break;
	default:
		return {};
	}

	BufferCreateInfo buffer_info = {};
	buffer_info.domain = BufferDomain::Host;
	buffer_info.size = layout.get_required_size();
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	result.buffer = create_buffer(buffer_info, nullptr);
	set_name(*result.buffer, "image-upload-staging-buffer");

	// And now, do the actual copy.
	uint8_t *mapped = static_cast<uint8_t *>(map_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT));
	unsigned index = 0;

	layout.set_buffer(mapped, layout.get_required_size());

	for (unsigned level = 0; level < copy_levels; level++)
	{
		const TextureFormatLayout::MipInfo &mip_info = layout.get_mip_info(level);
		uint32_t dst_height_stride = layout.get_layer_size(level);
		size_t row_size = layout.get_row_size(level);

		for (unsigned layer = 0; layer < info.layers; layer++, index++)
		{
			uint32_t src_row_length =
					initial[index].row_length ? initial[index].row_length : mip_info.row_length;
			uint32_t src_array_height =
					initial[index].image_height ? initial[index].image_height : mip_info.image_height;

			uint32_t src_row_stride = layout.row_byte_stride(src_row_length);
			uint32_t src_height_stride = layout.layer_byte_stride(src_array_height, src_row_stride);

			uint8_t *dst = static_cast<uint8_t *>(layout.data(layer, level));
			const uint8_t *src = static_cast<const uint8_t *>(initial[index].data);

			for (uint32_t z = 0; z < mip_info.depth; z++)
				for (uint32_t y = 0; y < mip_info.block_image_height; y++)
					memcpy(dst + z * dst_height_stride + y * row_size, src + z * src_height_stride + y * src_row_stride, row_size);
		}
	}

	unmap_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);
	layout.build_buffer_image_copies(result.blits, result.num_blits);
	return result;
}

ImageHandle Device::create_image(const ImageCreateInfo &create_info, const ImageInitialData *initial)
{
	if (initial)
	{
		InitialImageBuffer staging_buffer = create_image_staging_buffer(create_info, initial);
		return create_image_from_staging_buffer(create_info, &staging_buffer);
	}
	else
		return create_image_from_staging_buffer(create_info, nullptr);
}

ImageHandle Device::create_image_from_staging_buffer(const ImageCreateInfo &create_info,
                                                     const InitialImageBuffer *staging_buffer)
{
	ImageResourceHolder holder(device);
	VkMemoryRequirements reqs;

	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	info.format = create_info.format;
	info.extent.width = create_info.width;
	info.extent.height = create_info.height;
	info.extent.depth = create_info.depth;
	info.imageType = create_info.type;
	info.mipLevels = create_info.levels;
	info.arrayLayers = create_info.layers;
	info.samples = create_info.samples;

	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	info.usage = create_info.usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (create_info.domain == ImageDomain::Transient)
		info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	if (staging_buffer)
		info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	info.flags = create_info.flags;

	if (info.mipLevels == 0)
		info.mipLevels = image_num_miplevels(info.extent);

	if (create_info.usage & VK_IMAGE_USAGE_STORAGE_BIT)
		info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

	if (!image_format_is_supported(create_info.format, image_usage_to_features(info.usage), info.tiling))
	{
		LOGE("Format %u is not supported for usage flags!\n", unsigned(create_info.format));
		return ImageHandle(nullptr);
	}

	if (vkCreateImage(device, &info, nullptr, &holder.image) != VK_SUCCESS)
	{
		LOGE("Failed to create image in vkCreateImage.\n");
		return ImageHandle(nullptr);
	}

	vkGetImageMemoryRequirements(device, holder.image, &reqs);
	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
	if (memory_type == UINT32_MAX)
		return ImageHandle(nullptr);

	if (!managers.memory.allocate_image_memory(reqs.size, reqs.alignment, memory_type,
	                                           ALLOCATION_TILING_OPTIMAL,
	                                           &holder.allocation, holder.image))
	{
		LOGE("Failed to allocate image memory (type %u, size: %u).\n", unsigned(memory_type), unsigned(reqs.size));
		return ImageHandle(nullptr);
	}

	if (vkBindImageMemory(device, holder.image, holder.allocation.get_memory(), holder.allocation.get_offset()) != VK_SUCCESS)
	{
		LOGE("Failed to bind image memory.\n");
		return ImageHandle(nullptr);
	}

	ImageCreateInfo tmpinfo = create_info;
	tmpinfo.usage = info.usage;
	tmpinfo.levels = info.mipLevels;

	bool has_view = (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) != 0;
	if (has_view)
	{
		if (!holder.create_default_views(tmpinfo, nullptr))
			return ImageHandle(nullptr);
	}

	ImageHandle handle(handle_pool.images.allocate(this, holder.image, holder.image_view, holder.allocation, tmpinfo));
	if (handle)
	{
		holder.owned = false;
		if (has_view)
		{
			handle->get_view().set_alt_views(holder.depth_view, holder.stencil_view);
			handle->get_view().set_render_target_views(std::move(holder.rt_views));
		}

		// Set possible dstStage and dstAccess.
		handle->set_stage_flags(image_usage_to_possible_stages(info.usage));
		handle->set_access_flags(image_usage_to_possible_access(info.usage));
	}

	// Copy initial data to texture.
	if (staging_buffer)
	{
		VK_ASSERT(create_info.domain != ImageDomain::Transient);
		VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);
		bool generate_mips = (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;

		// If graphics_queue != transfer_queue, we will use a semaphore, so no srcAccess mask is necessary.
		VkAccessFlags final_transition_src_access = 0;
		if (generate_mips)
			final_transition_src_access = VK_ACCESS_TRANSFER_READ_BIT; // Validation complains otherwise.
		else if (graphics_queue == transfer_queue)
			final_transition_src_access = VK_ACCESS_TRANSFER_WRITE_BIT;

		VkAccessFlags prepare_src_access = graphics_queue == transfer_queue ? VK_ACCESS_TRANSFER_WRITE_BIT : 0;
		bool need_mipmap_barrier = true;
		bool need_initial_barrier = true;

		// Now we've used the TRANSFER queue to copy data over to the GPU.
		// For mipmapping, we're now moving over to graphics,
		// the transfer queue is designed for CPU <-> GPU and that's it.

		// For concurrent queue mode, we just need to inject a semaphore.
		// For non-concurrent queue mode, we will have to inject ownership transfer barrier if the queue families do not match.

		CommandBufferHandle graphics_cmd = request_command_buffer(CommandBuffer::Type::Generic);
		CommandBufferHandle transfer_cmd;

		// Don't split the upload into multiple command buffers unless we have to.
		if (transfer_queue != graphics_queue)
			transfer_cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		else
			transfer_cmd = graphics_cmd;

		transfer_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                            VK_ACCESS_TRANSFER_WRITE_BIT);

		transfer_cmd->begin_region("copy-image-to-gpu");
		transfer_cmd->copy_buffer_to_image(*handle, *staging_buffer->buffer, staging_buffer->num_blits, staging_buffer->blits);
		transfer_cmd->end_region();

		if (transfer_queue != graphics_queue)
		{
			VkPipelineStageFlags dst_stages =
					generate_mips ? VkPipelineStageFlags(VK_PIPELINE_STAGE_TRANSFER_BIT) : handle->get_stage_flags();

			// We can't just use semaphores, we will also need a release + acquire barrier to marshal ownership from
			// transfer queue over to graphics ...
			if (transfer_queue_family_index != graphics_queue_family_index)
			{
				need_mipmap_barrier = false;

				VkImageMemoryBarrier release = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				release.image = handle->get_image();
				release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				release.dstAccessMask = 0;
				release.srcQueueFamilyIndex = transfer_queue_family_index;
				release.dstQueueFamilyIndex = graphics_queue_family_index;
				release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

				if (generate_mips)
				{
					release.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					release.subresourceRange.levelCount = 1;
				}
				else
				{
					release.newLayout = create_info.initial_layout;
					release.subresourceRange.levelCount = info.mipLevels;
					need_initial_barrier = false;
				}

				release.subresourceRange.aspectMask = format_to_aspect_mask(info.format);
				release.subresourceRange.layerCount = info.arrayLayers;

				VkImageMemoryBarrier acquire = release;
				acquire.srcAccessMask = 0;

				if (generate_mips)
					acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				else
					acquire.dstAccessMask = handle->get_access_flags() & image_layout_to_possible_access(create_info.initial_layout);

				transfer_cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
				                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				                      0, nullptr, 0, nullptr, 1, &release);

				graphics_cmd->barrier(dst_stages,
				                      dst_stages,
				                      0, nullptr, 0, nullptr, 1, &acquire);
			}

			Semaphore sem;
			submit(transfer_cmd, nullptr, 1, &sem);
			add_wait_semaphore(CommandBuffer::Type::Generic, sem, dst_stages, true);
		}

		if (generate_mips)
		{
			graphics_cmd->begin_region("mipgen");
			graphics_cmd->barrier_prepare_generate_mipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                                              VK_PIPELINE_STAGE_TRANSFER_BIT,
			                                              prepare_src_access, need_mipmap_barrier);
			graphics_cmd->generate_mipmap(*handle);
			graphics_cmd->end_region();
		}

		if (need_initial_barrier)
		{
			graphics_cmd->image_barrier(
					*handle, generate_mips ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					create_info.initial_layout,
					VK_PIPELINE_STAGE_TRANSFER_BIT, final_transition_src_access,
					handle->get_stage_flags(),
					handle->get_access_flags() & image_layout_to_possible_access(create_info.initial_layout));
		}

		bool share_async_graphics = get_physical_queue_type(CommandBuffer::Type::AsyncGraphics) == CommandBuffer::Type::AsyncCompute;

		// Add semaphore if the compute queue can be used for async graphics as well.
		if (share_async_graphics)
		{
			Semaphore sem;
			submit(graphics_cmd, nullptr, 1, &sem);

			VkPipelineStageFlags dst_stages = handle->get_stage_flags();
			if (graphics_queue_family_index != compute_queue_family_index)
				dst_stages &= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
			add_wait_semaphore(CommandBuffer::Type::AsyncCompute, sem, dst_stages, true);
		}
		else
			submit(graphics_cmd);
	}
	else if (create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
	{
		VK_ASSERT(create_info.domain != ImageDomain::Transient);
		CommandBufferHandle cmd = request_command_buffer(CommandBuffer::Type::Generic);
		cmd->image_barrier(*handle, info.initialLayout, create_info.initial_layout,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, handle->get_stage_flags(),
		                   handle->get_access_flags() &
		                   image_layout_to_possible_access(create_info.initial_layout));

		submit(cmd);
	}

	return handle;
}

static VkSamplerCreateInfo fill_vk_sampler_info(const SamplerCreateInfo &sampler_info)
{
	VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	info.magFilter = sampler_info.mag_filter;
	info.minFilter = sampler_info.min_filter;
	info.mipmapMode = sampler_info.mipmap_mode;
	info.addressModeU = sampler_info.address_mode_u;
	info.addressModeV = sampler_info.address_mode_v;
	info.addressModeW = sampler_info.address_mode_w;
	info.mipLodBias = sampler_info.mip_lod_bias;
	info.anisotropyEnable = sampler_info.anisotropy_enable;
	info.maxAnisotropy = sampler_info.max_anisotropy;
	info.compareEnable = sampler_info.compare_enable;
	info.compareOp = sampler_info.compare_op;
	info.minLod = sampler_info.min_lod;
	info.maxLod = sampler_info.max_lod;
	info.borderColor = sampler_info.border_color;
	info.unnormalizedCoordinates = sampler_info.unnormalized_coordinates;
	return info;
}

SamplerHandle Device::create_sampler(const SamplerCreateInfo &sampler_info, StockSampler stock_sampler)
{
	VkSamplerCreateInfo info = fill_vk_sampler_info(sampler_info);
	VkSampler sampler;

	(void)stock_sampler;
	if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
		return SamplerHandle(nullptr);

	return SamplerHandle(handle_pool.samplers.allocate(this, sampler));
}

BufferHandle Device::create_buffer(const BufferCreateInfo &create_info, const void *initial)
{
	VkBuffer buffer;
	VkMemoryRequirements reqs;
	DeviceAllocation allocation;

	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	info.size = create_info.size;
	info.usage = create_info.usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	uint32_t sharing_indices[3];
	if (graphics_queue_family_index != compute_queue_family_index ||
	    graphics_queue_family_index != transfer_queue_family_index)
	{
		// For buffers, always just use CONCURRENT access modes,
		// so we don't have to deal with acquire/release barriers in async compute.
		info.sharingMode = VK_SHARING_MODE_CONCURRENT;

		sharing_indices[info.queueFamilyIndexCount++] = graphics_queue_family_index;

		if (graphics_queue_family_index != compute_queue_family_index)
			sharing_indices[info.queueFamilyIndexCount++] = compute_queue_family_index;

		if (graphics_queue_family_index != transfer_queue_family_index &&
		    compute_queue_family_index != transfer_queue_family_index)
		{
			sharing_indices[info.queueFamilyIndexCount++] = transfer_queue_family_index;
		}

		info.pQueueFamilyIndices = sharing_indices;
	}

	if (vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
		return BufferHandle(nullptr);

	vkGetBufferMemoryRequirements(device, buffer, &reqs);

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
	if (memory_type == UINT32_MAX)
	{
		vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle(nullptr);
	}

	if (!managers.memory.allocate(reqs.size, reqs.alignment, memory_type, ALLOCATION_TILING_LINEAR, &allocation))
	{
		vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle(nullptr);
	}

	if (vkBindBufferMemory(device, buffer, allocation.get_memory(), allocation.get_offset()) != VK_SUCCESS)
	{
		allocation.free_immediate(managers.memory);
		vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle(nullptr);
	}

	BufferCreateInfo tmpinfo = create_info;
	tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	BufferHandle handle(handle_pool.buffers.allocate(this, buffer, allocation, tmpinfo));

	if (create_info.domain == BufferDomain::Device && initial && !memory_type_is_host_visible(memory_type))
	{
		CommandBufferHandle cmd;
		BufferCreateInfo staging_info = create_info;
		staging_info.domain = BufferDomain::Host;
		BufferHandle staging_buffer = create_buffer(staging_info, initial);
		set_name(*staging_buffer, "buffer-upload-staging-buffer");

		cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		cmd->begin_region("copy-buffer-staging");
		cmd->copy_buffer(*handle, *staging_buffer);
		cmd->end_region();

		LOCK();
		submit_staging(cmd, info.usage, true);
	}
	else if (initial)
	{
		void *ptr = managers.memory.map_memory(allocation, MEMORY_ACCESS_WRITE_BIT);
		if (!ptr)
			return BufferHandle(nullptr);

		memcpy(ptr, initial, create_info.size);
		managers.memory.unmap_memory(allocation, MEMORY_ACCESS_WRITE_BIT);
	}
	return handle;
}

bool Device::memory_type_is_host_visible(uint32_t type) const
{
	return (mem_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

bool Device::get_image_format_properties(VkFormat format, VkImageType type, VkImageTiling tiling,
                                         VkImageUsageFlags usage, VkImageCreateFlags flags,
                                         VkImageFormatProperties *properties)
{
	VkResult res = vkGetPhysicalDeviceImageFormatProperties(gpu, format, type, tiling, usage, flags,
	                                                    properties);
	return res == VK_SUCCESS;
}

bool Device::image_format_is_supported(VkFormat format, VkFormatFeatureFlags required, VkImageTiling tiling) const
{
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
	VkFormatFeatureFlags flags = tiling == VK_IMAGE_TILING_OPTIMAL ? props.optimalTilingFeatures : props.linearTilingFeatures;
	return (flags & required) == required;
}

VkFormat Device::get_default_depth_format() const
{
	if (image_format_is_supported(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
		return VK_FORMAT_D32_SFLOAT;
	if (image_format_is_supported(VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
		return VK_FORMAT_X8_D24_UNORM_PACK32;
	if (image_format_is_supported(VK_FORMAT_D16_UNORM, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
		return VK_FORMAT_D16_UNORM;

	return VK_FORMAT_UNDEFINED;
}

uint64_t Device::allocate_cookie()
{
	// Reserve lower bits for "special purposes".
	cookie += 16;
	return cookie;
}

const RenderPass &Device::request_render_pass(const RenderPassInfo &info, bool compatible)
{
	Hasher h;
	VkFormat formats[VULKAN_NUM_ATTACHMENTS];
	VkFormat depth_stencil;
	uint32_t lazy = 0;
	uint32_t optimal = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		VK_ASSERT(info.color_attachments[i]);
		formats[i] = info.color_attachments[i]->get_format();
		if (info.color_attachments[i]->get_image().get_create_info().domain == ImageDomain::Transient)
			lazy |= 1u << i;
		if (info.color_attachments[i]->get_image().get_layout_type() == Layout::Optimal)
			optimal |= 1u << i;
	}

	if (info.depth_stencil)
	{
		if (info.depth_stencil->get_image().get_create_info().domain == ImageDomain::Transient)
			lazy |= 1u << info.num_color_attachments;
		if (info.depth_stencil->get_image().get_layout_type() == Layout::Optimal)
			optimal |= 1u << info.num_color_attachments;
	}

	h.u32(info.num_subpasses);
	for (unsigned i = 0; i < info.num_subpasses; i++)
	{
		h.u32(info.subpasses[i].num_color_attachments);
		h.u32(info.subpasses[i].num_input_attachments);
		h.u32(info.subpasses[i].num_resolve_attachments);
		h.u32(static_cast<uint32_t>(info.subpasses[i].depth_stencil_mode));
		for (unsigned j = 0; j < info.subpasses[i].num_color_attachments; j++)
			h.u32(info.subpasses[i].color_attachments[j]);
		for (unsigned j = 0; j < info.subpasses[i].num_input_attachments; j++)
			h.u32(info.subpasses[i].input_attachments[j]);
		for (unsigned j = 0; j < info.subpasses[i].num_resolve_attachments; j++)
			h.u32(info.subpasses[i].resolve_attachments[j]);
	}

	depth_stencil = info.depth_stencil ? info.depth_stencil->get_format() : VK_FORMAT_UNDEFINED;
	h.data(reinterpret_cast<const uint32_t *>(formats), info.num_color_attachments * sizeof(VkFormat));
	h.u32(info.num_color_attachments);
	h.u32(depth_stencil);

	// Compatible render passes do not care about load/store, or image layouts.
	if (!compatible)
	{
		h.u32(info.op_flags);
		h.u32(info.clear_attachments);
		h.u32(info.load_attachments);
		h.u32(info.store_attachments);
		h.u32(optimal);
	}

	// Lazy flag can change external subpass dependencies, which is not compatible.
	h.u32(lazy);

	Hash hash = h.get();

	RenderPass *ret = render_passes.find(hash);
	if (!ret)
		ret = render_passes.emplace_yield(hash, hash, this, info);
	return *ret;
}

const Framebuffer &Device::request_framebuffer(const RenderPassInfo &info)
{
	return framebuffer_allocator.request_framebuffer(info);
}

ImageView &Device::get_transient_attachment(unsigned width, unsigned height, VkFormat format,
                                            unsigned index, unsigned samples, unsigned layers)
{
	return transient_allocator.request_attachment(width, height, format, index, samples, layers);
}

void Device::set_name(const Buffer &buffer, const char *name)
{
	if (ext.supports_debug_marker)
	{
		VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT;
		info.object = (uint64_t)buffer.get_buffer();
		info.pObjectName = name;
		vkDebugMarkerSetObjectNameEXT(device, &info);
	}
}

void Device::set_name(const Image &image, const char *name)
{
	if (ext.supports_debug_marker)
	{
		VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
		info.object = (uint64_t)image.get_image();
		info.pObjectName = name;
		vkDebugMarkerSetObjectNameEXT(device, &info);
	}
}

}

/* === atlas.cpp === */

using namespace std;

namespace PSX
{

FBAtlas::FBAtlas()
{
	for (StatusFlags &f : fb_info)
		f = STATUS_FB_PREFER;
}

void FBAtlas::load_image(const Rect &rect)
{
	if (rect.width == 0 || rect.height == 0)
		return;

	write_compute(Domain::Unscaled, rect);

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) &= ~STATUS_TEXTURE_RENDERED;
}

bool FBAtlas::texture_rendered(const Rect &rect)
{
	if (rect.width == 0 || rect.height == 0)
		return false;

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			if (info(x, y) & STATUS_TEXTURE_RENDERED)
				return true;
	return false;
}

Domain FBAtlas::blit_vram(const Rect &dst, const Rect &src)
{
	Domain domain = find_suitable_domain(src);

	sync_domain(domain, src);
	sync_domain(domain, dst);
	read_domain(domain, Stage::Compute, src);
	write_domain(domain, Stage::Compute, dst);

	unsigned dst_xbegin = dst.x / BLOCK_WIDTH;
	unsigned dst_xend = (dst.x + dst.width - 1) / BLOCK_WIDTH;
	unsigned dst_ybegin = dst.y / BLOCK_HEIGHT;
	unsigned dst_yend = (dst.y + dst.height - 1) / BLOCK_HEIGHT;

	unsigned src_xbegin = src.x / BLOCK_WIDTH;
	unsigned src_xend = (src.x + src.width - 1) / BLOCK_WIDTH;
	unsigned src_ybegin = src.y / BLOCK_HEIGHT;
	unsigned src_yend = (src.y + src.height - 1) / BLOCK_HEIGHT;

	{
		unsigned j_max = (dst_yend - dst_ybegin) < (src_yend - src_ybegin)
		               ? (dst_yend - dst_ybegin) : (src_yend - src_ybegin);
		unsigned i_max = (dst_xend - dst_xbegin) < (src_xend - src_xbegin)
		               ? (dst_xend - dst_xbegin) : (src_xend - src_xbegin);
		for (unsigned j = 0; j <= j_max; j++)
			for (unsigned i = 0; i <= i_max; i++)
			{
				bool rendered = info(src_xbegin + i, src_ybegin + j) & STATUS_TEXTURE_RENDERED;
				if (rendered)
					info(dst_xbegin + i, dst_ybegin + j) |= STATUS_TEXTURE_RENDERED;
				else
					info(dst_xbegin + i, dst_ybegin + j) &= ~STATUS_TEXTURE_RENDERED;
			}
	}

	return domain;
}

void FBAtlas::read_fragment(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	read_domain(domain, Stage::Fragment, rect);
}

void FBAtlas::read_compute(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	read_domain(domain, Stage::Compute, rect);
}

void FBAtlas::write_compute(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	write_domain(domain, Stage::Compute, rect);
}

void FBAtlas::read_transfer(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	read_domain(domain, Stage::Transfer, rect);
}

void FBAtlas::write_transfer(Domain domain, const Rect &rect)
{
	sync_domain(domain, rect);
	write_domain(domain, Stage::Transfer, rect);
}

void FBAtlas::read_texture(Domain domain)
{
	Rect shifted = renderpass.texture_window;
	bool palette;
	switch (renderpass.texture_mode)
	{
	case TextureMode::Palette4bpp:
	case TextureMode::Palette8bpp:
		palette = true;
		break;

	default:
		palette = false;
		break;
	}
	shifted.x += renderpass.texture_offset_x;
	shifted.y += renderpass.texture_offset_y;

	//Domain domain = palette ? Domain::Unscaled : find_suitable_domain(shifted);
	sync_domain(domain, shifted);

	Rect palette_rect = { renderpass.palette_offset_x, renderpass.palette_offset_y,
		                  renderpass.texture_mode == TextureMode::Palette8bpp ? 256u : 16u, 1 };

	if (palette)
		sync_domain(domain, palette_rect);

	read_domain(domain, Stage::FragmentTexture, shifted);
	if (palette)
		read_domain(domain, Stage::FragmentTexture, palette_rect);
}

bool FBAtlas::write_domain(Domain domain, Stage stage, const Rect &rect)
{
	if (inside_render_pass(rect))
		flush_render_pass();

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	unsigned write_domains = 0;
	unsigned hazard_domains = 0;
	unsigned resolve_domains = 0;
	if (domain == Domain::Unscaled)
	{
		hazard_domains = STATUS_FB_WRITE | STATUS_FB_READ | STATUS_TEXTURE_READ;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_FB_WRITE;
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_FB_WRITE;
		else if (stage == Stage::Fragment)
		{
			// Write-after-write in fragment is handled implicitly.
			// Write-after-read means rendering to a block after reading it as a texture.
			// This is a hazard we must handle.
			hazard_domains &= ~STATUS_FRAGMENT_FB_WRITE;
			resolve_domains = STATUS_FRAGMENT_FB_WRITE;
		}
		resolve_domains |= STATUS_FB_ONLY;
	}
	else
	{
		hazard_domains = STATUS_SFB_WRITE | STATUS_SFB_READ | STATUS_TEXTURE_READ;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_SFB_WRITE;
		else if (stage == Stage::Fragment)
		{
			// Write-after-write in fragment is handled implicitly.
			// Write-after-read means rendering to a block after reading it as a texture.
			// This is a hazard we must handle.
			hazard_domains &= ~STATUS_FRAGMENT_SFB_WRITE;
			resolve_domains = STATUS_FRAGMENT_SFB_WRITE;
		}
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_SFB_WRITE;
		resolve_domains |= STATUS_SFB_ONLY;
	}

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			write_domains |= info(x, y) & hazard_domains;

	// Trying to update VRAM before fragment is done reading it.
	// We could use copy-on-write here to avoid flushing, but this scenario is very rare.
	if (write_domains & STATUS_TEXTURE_READ)
		flush_render_pass();

	if (write_domains)
		pipeline_barrier(write_domains);

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) = (info(x, y) & ~STATUS_OWNERSHIP_MASK) | resolve_domains;

	return (write_domains & STATUS_FRAGMENT_SFB_READ) != 0;
}

void FBAtlas::read_domain(Domain domain, Stage stage, const Rect &rect)
{
	if (inside_render_pass(rect))
		flush_render_pass();

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	unsigned write_domains = 0;
	unsigned hazard_domains = 0;
	unsigned resolve_domains = 0;
	if (domain == Domain::Unscaled)
	{
		hazard_domains = STATUS_FB_WRITE;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_FB_READ;
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_FB_READ;
		else if (stage == Stage::Fragment)
		{
			hazard_domains &= ~STATUS_FRAGMENT_FB_READ;
			resolve_domains = STATUS_FRAGMENT_FB_READ;
		}
		else if (stage == Stage::FragmentTexture)
		{
			hazard_domains &= ~(STATUS_FRAGMENT_FB_READ | STATUS_TEXTURE_READ);
			resolve_domains = STATUS_FRAGMENT_FB_READ | STATUS_TEXTURE_READ;
		}
	}
	else
	{
		hazard_domains = STATUS_SFB_WRITE;
		if (stage == Stage::Compute)
			resolve_domains = STATUS_COMPUTE_SFB_READ;
		else if (stage == Stage::Transfer)
			resolve_domains = STATUS_TRANSFER_SFB_READ;
		else if (stage == Stage::Fragment)
		{
			hazard_domains &= ~STATUS_FRAGMENT_SFB_READ;
			resolve_domains = STATUS_FRAGMENT_SFB_READ;
		}
		else if (stage == Stage::FragmentTexture)
		{
			hazard_domains &= ~(STATUS_FRAGMENT_SFB_READ | STATUS_TEXTURE_READ);
			resolve_domains = STATUS_FRAGMENT_SFB_READ | STATUS_TEXTURE_READ;
		}
	}

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			write_domains |= info(x, y) & hazard_domains;

	if (write_domains)
		pipeline_barrier(write_domains);

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) |= resolve_domains;
}

void FBAtlas::sync_domain(Domain domain, const Rect &rect)
{
	if (inside_render_pass(rect))
		flush_render_pass();

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	// If we need to see a "clean" version
	// of a framebuffer domain, we need to see
	// anything other than this flag.
	unsigned dirty_bits = 1u << (domain == Domain::Unscaled ? STATUS_SFB_ONLY : STATUS_FB_ONLY);
	unsigned bits = 0;

	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			bits |= 1u << (info(x, y) & STATUS_OWNERSHIP_MASK);

	unsigned write_domains = 0;

	// We're asserting that a region is up to date, but it's
	// not, so we have to resolve it.
	if ((bits & dirty_bits) == 0)
		return;

	// For scaled domain,
	// we need to blit from unscaled domain to scaled.
	unsigned ownership;
	unsigned hazard_domains;
	unsigned resolve_domains;
	if (domain == Domain::Scaled)
	{
		ownership = STATUS_FB_ONLY;
		hazard_domains = STATUS_FB_WRITE | STATUS_SFB_WRITE | STATUS_SFB_READ;

		//resolve_domains = STATUS_TRANSFER_FB_READ | STATUS_FB_PREFER | STATUS_TRANSFER_SFB_WRITE;
		resolve_domains = STATUS_COMPUTE_FB_READ | STATUS_FB_PREFER | STATUS_COMPUTE_SFB_WRITE;
	}
	else
	{
		ownership = STATUS_SFB_ONLY;
		hazard_domains = STATUS_FB_WRITE | STATUS_SFB_WRITE | STATUS_FB_READ;

		//resolve_domains = STATUS_TRANSFER_SFB_READ | STATUS_SFB_PREFER | STATUS_TRANSFER_FB_WRITE;
		resolve_domains = STATUS_COMPUTE_SFB_READ | STATUS_SFB_PREFER | STATUS_COMPUTE_FB_WRITE;
	}

	for (unsigned y = ybegin; y <= yend; y++)
	{
		for (unsigned x = xbegin; x <= xend; x++)
		{
			StatusFlags &mask = info(x, y);
			// If our block isn't in the ownership class we want,
			// we need to read from one block and write to the other.
			// We might have to wait for writers on read,
			// and add hazard masks for our writes
			// so other readers can wait for us.
			if ((mask & STATUS_OWNERSHIP_MASK) == ownership)
				write_domains |= mask & hazard_domains;
		}
	}

	// If we hit any hazard, resolve it.
	if (write_domains)
		pipeline_barrier(write_domains);

	for (unsigned y = ybegin; y <= yend; y++)
	{
		for (unsigned x = xbegin; x <= xend; x++)
		{
			StatusFlags &mask = info(x, y);
			if ((mask & STATUS_OWNERSHIP_MASK) == ownership)
			{
				mask &= ~STATUS_OWNERSHIP_MASK;
				mask |= resolve_domains;
				listener->resolve(domain, (BLOCK_WIDTH * x) & (FB_WIDTH - 1), (BLOCK_HEIGHT * y) & (FB_HEIGHT - 1));
			}
		}
	}
}

Domain FBAtlas::find_suitable_domain(const Rect &rect)
{
	if (inside_render_pass(rect))
		return Domain::Scaled;

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

	for (unsigned y = ybegin; y <= yend; y++)
	{
		for (unsigned x = xbegin; x <= xend; x++)
		{
			unsigned i = info(x, y) & STATUS_OWNERSHIP_MASK;
			if (i == STATUS_FB_ONLY || i == STATUS_FB_PREFER)
				return Domain::Unscaled;
		}
	}
	return Domain::Scaled;
}

bool FBAtlas::inside_render_pass(const Rect &rect)
{
	if (!renderpass.inside)
		return false;

	unsigned xbegin = rect.x & ~(BLOCK_WIDTH - 1);
	unsigned ybegin = rect.y & ~(BLOCK_HEIGHT - 1);
	unsigned xend = ((rect.x + rect.width - 1) | (BLOCK_WIDTH - 1)) + 1;
	unsigned yend = ((rect.y + rect.height - 1) | (BLOCK_HEIGHT - 1)) + 1;

	unsigned rpx2 = renderpass.rect.x + renderpass.rect.width;
	unsigned rpy2 = renderpass.rect.y + renderpass.rect.height;
	unsigned x0 = (renderpass.rect.x > xbegin) ? renderpass.rect.x : xbegin;
	unsigned x1 = (rpx2 < xend) ? rpx2 : xend;
	unsigned y0 = (renderpass.rect.y > ybegin) ? renderpass.rect.y : ybegin;
	unsigned y1 = (rpy2 < yend) ? rpy2 : yend;

	return x1 > x0 && y1 > y0;
}

void FBAtlas::flush_render_pass()
{
	if (!renderpass.inside)
		return;

	// Clear out the "shadow" stage.
	for (StatusFlags &f : fb_info)
		f &= ~STATUS_TEXTURE_READ;

	renderpass.inside = false;
	const Rect &rect = renderpass.rect;
	if (rect.width == 0 || rect.height == 0)
		return;

	write_domain(Domain::Scaled, Stage::Fragment, rect);
	listener->flush_render_pass(rect);

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) |= STATUS_TEXTURE_RENDERED;
}

void FBAtlas::set_texture_window(const Rect &rect)
{
	renderpass.texture_window = rect;
}

void FBAtlas::extend_render_pass(const Rect &rect, bool scissor)
{
	bool scissor_invariant = !scissor || renderpass.scissor.contains(rect);
	listener->set_scissored_invariant(scissor_invariant);
	Rect scissored_rect = !scissor_invariant ? rect.scissor(renderpass.scissor) : rect;

	if (!scissored_rect.width || !scissored_rect.height)
		return;

	if (!renderpass.inside)
	{
		renderpass.rect = scissored_rect;
		sync_domain(Domain::Scaled, renderpass.rect);
		write_domain(Domain::Scaled, Stage::Fragment, renderpass.rect);
		renderpass.inside = true;
	}
	else if (!renderpass.rect.contains(scissored_rect))
	{
		renderpass.rect.extend_bounding_box(scissored_rect);

		// Avoid sync/write domain flushing our own render pass.
		renderpass.inside = false;

		// If we cleared the screen and we created a clear candidate,
		// everything inside this render pass can be safely discarded.
		if (!scissor && scissored_rect == renderpass.rect)
			discard_render_pass();

		sync_domain(Domain::Scaled, renderpass.rect);
		if (write_domain(Domain::Scaled, Stage::Fragment, renderpass.rect))
		{
			// If render pass was flushed here due to write-after-read hazards, set rect to
			// our new scissored_rect instead.
			renderpass.inside = true;
			flush_render_pass();
			renderpass.rect = scissored_rect;
		}

		renderpass.inside = true;
	}
}

void FBAtlas::write_fragment(Domain domain, const Rect &rect)
{
	bool reads_window = renderpass.texture_mode != TextureMode::None;
	if (reads_window)
	{
		Rect shifted = renderpass.texture_window;
		bool reads_palette;
		switch (renderpass.texture_mode)
		{
		case TextureMode::Palette4bpp:
		case TextureMode::Palette8bpp:
			reads_palette = true;
			break;

		default:
			reads_palette = false;
			break;
		}
		shifted.x += renderpass.texture_offset_x;
		shifted.y += renderpass.texture_offset_y;

		const Rect palette_rect = { renderpass.palette_offset_x, renderpass.palette_offset_y,
			                        renderpass.texture_mode == TextureMode::Palette8bpp ? 256u : 16u, 1 };

		if (reads_palette)
		{
			if (inside_render_pass(shifted) || inside_render_pass(palette_rect))
				flush_render_pass();
		}
		else if (inside_render_pass(shifted))
			flush_render_pass();

		read_texture(domain);
	}

	extend_render_pass(rect, true);
}

void FBAtlas::clear_rect(const Rect &rect, uint32_t fb_color)
{
	if (rect.width == 0 || rect.height == 0)
		return;

	// If we're clearing completely outside the renderpass, we're probably doing another render pass
	// somewhere else, so end the current one and start a new one instead.
	if (renderpass.inside && !renderpass.rect.intersects(rect))
		flush_render_pass();

	extend_render_pass(rect, false);

	// If the render pass area doesn't increase later, we can use loadOp == CLEAR instead of LOAD,
	// which helps a lot on mobile GPUs.
	listener->clear_quad(rect, fb_color, renderpass.rect == rect);

	unsigned xbegin = rect.x / BLOCK_WIDTH;
	unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
	unsigned ybegin = rect.y / BLOCK_HEIGHT;
	unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
	for (unsigned y = ybegin; y <= yend; y++)
		for (unsigned x = xbegin; x <= xend; x++)
			info(x, y) &= ~STATUS_TEXTURE_RENDERED;
}

void FBAtlas::set_draw_rect(const Rect &rect)
{
	renderpass.scissor = rect;
}

void FBAtlas::discard_render_pass()
{
	renderpass.inside = false;
	listener->discard_render_pass();
}

void FBAtlas::notify_external_barrier(StatusFlags domains)
{
	static const StatusFlags compute_read_stages = STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ;
	static const StatusFlags compute_write_stages = STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_WRITE;
	static const StatusFlags transfer_read_stages = STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_SFB_READ;
	static const StatusFlags transfer_write_stages = STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_WRITE;
	static const StatusFlags fragment_write_stages = STATUS_FRAGMENT_SFB_WRITE | STATUS_FRAGMENT_FB_WRITE;
	static const StatusFlags fragment_read_stages = STATUS_FRAGMENT_SFB_READ | STATUS_FRAGMENT_FB_READ;

	if (domains & compute_write_stages)
		domains |= compute_write_stages | compute_read_stages;
	if (domains & compute_read_stages)
		domains |= compute_read_stages;
	if (domains & transfer_write_stages)
		domains |= transfer_write_stages | transfer_read_stages;
	if (domains & transfer_read_stages)
		domains |= transfer_read_stages;
	if (domains & fragment_write_stages)
		domains |= fragment_write_stages | fragment_read_stages;
	if (domains & fragment_read_stages)
		domains |= fragment_read_stages;

	for (StatusFlags &f : fb_info)
		f &= ~domains;
}

void FBAtlas::pipeline_barrier(StatusFlags domains)
{
	if (domains & (STATUS_FRAGMENT_SFB_WRITE | STATUS_FRAGMENT_SFB_READ))
		flush_render_pass();
	listener->hazard(domains);
	notify_external_barrier(domains);
}
}

/* ============================================================
 *
 * Folded content from the parallel-psx/custom-textures/
 * source files and the previously-private image_io.hpp +
 * dbg_input_callback.h headers.
 *
 * ============================================================ */

/* === config_parser.cpp === */
#include <regex>

int ignore_arg_to_number(std::string arg) {
    if (arg == "*") {
        return -1;
    } else {
        return atoi(arg.c_str());
    }
}

std::vector<PSX::RectMatch> parse_config_file(const char *path) {
    std::vector<PSX::RectMatch> result;
    // https://stackoverflow.com/questions/7868936/read-file-line-by-line-using-ifstream-in-c
    std::string line;
    std::ifstream in(path);
    std::regex ignore_command("^\\s*ignore\\s+(\\d+|\\*)\\s*,\\s*(\\d+|\\*)\\s*,\\s*(\\d+|\\*)\\s*,\\s*(\\d+|\\*)\\s*(?:#.*)?$");
    while (std::getline(in, line)) {
        std::smatch sm;
        if (std::regex_match(line, sm, ignore_command)) {
            result.push_back(PSX::RectMatch(
                ignore_arg_to_number(sm[1]),
                ignore_arg_to_number(sm[2]),
                ignore_arg_to_number(sm[3]),
                ignore_arg_to_number(sm[4])
            ));
        }
    }
    return result;
}

/* === image_io.hpp (private; only used within this TU) === */


struct RGBAImage {
    uint8_t* data;
    int width;
    int height;

    ~RGBAImage();
};

std::unique_ptr<RGBAImage> load_image(const char *path);
bool write_image(const char *path, int width, int height, const void *data);

/* === image_io.cpp === */

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../stb/stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../stb/stb_image.h"

RGBAImage::~RGBAImage() {
    if (this->data != nullptr) {
        stbi_image_free(this->data);
    }
}

std::unique_ptr<RGBAImage> load_image(const char *path) {
    std::unique_ptr<RGBAImage> ptr = std::unique_ptr<RGBAImage>(new RGBAImage);
    int channels;
    ptr->data = stbi_load(path, &ptr->width, &ptr->height, &channels, 4);
    return ptr;
}
bool write_image(const char *path, int width, int height, const void *data) {
    return stbi_write_png(path, width, height, 4, data, 4 * width);
}

/* === dbg_input_callback.h (extern, defined in libretro.c) === */

extern retro_input_state_t dbg_input_state_cb;


/* === texture_tracker.cpp === */
#include "libretro-common/include/retro_dirent.h"

// Actually using the implementation in deps/zlib/crc32.c I think
#include "scrc32.h"

extern char retro_cd_base_name[4096];
extern char retro_cd_base_directory[4096];
#ifdef _WIN32
   static char retro_slash = '\\';
#else
   static char retro_slash = '/';
#endif

namespace PSX {

std::string dump_path() {
    std::string fullpath;

    fullpath += retro_cd_base_directory;
    fullpath += retro_slash;
    fullpath += retro_cd_base_name;
    fullpath += "-texture-dump";
    fullpath += retro_slash;

    return fullpath;
}

std::string replacements_path() {
    std::string fullpath;

    fullpath += retro_cd_base_directory;
    fullpath += retro_slash;
    fullpath += retro_cd_base_name;
    fullpath += "-texture-replacements";
    fullpath += retro_slash;

    return fullpath;
}

std::string replacement_filename_from_hash(uint32_t hash, uint32_t palette_hash) {
    char tail[32];
    snprintf(tail, sizeof(tail), "%x-%x.png", (unsigned)hash, (unsigned)palette_hash);
    return replacements_path() + tail;
}

inline uint8_t *loaded_pixel(LoadedImage &image, int x, int y) {
    return &image.owned_data[(y * image.width + x) * 4];
}

LoadedImage generate_mip(LoadedImage &higher) {
    // Generate custom mipmaps in order to avoid transparent (0, 0, 0, 0) and semi-transparent (r, g, b, a>=128)
    // mixing to create some dark opaque value (r, g, b, a<128).

    LoadedImage result;
    // Assumes higher.width and higher.height are both divisible by 2 (and also therefore > 1)
    result.width = higher.width / 2;
    result.height = higher.height / 2;
    result.owned_data.resize(result.width * result.height * 4);
    for (int y = 0; y < result.height; y++) {
        for (int x = 0; x < result.width; x++) {
            uint8_t *src00 = loaded_pixel(higher, x * 2 + 0, y * 2 + 0);
            uint8_t *src10 = loaded_pixel(higher, x * 2 + 1, y * 2 + 0);
            uint8_t *src01 = loaded_pixel(higher, x * 2 + 0, y * 2 + 1);
            uint8_t *src11 = loaded_pixel(higher, x * 2 + 1, y * 2 + 1);
            
            int numTransparent = 0;
            if (src00[0] == 0 && src00[1] == 0 && src00[2] == 0 && src00[3] == 0) numTransparent += 1;
            if (src10[0] == 0 && src10[1] == 0 && src10[2] == 0 && src10[3] == 0) numTransparent += 1;
            if (src01[0] == 0 && src01[1] == 0 && src01[2] == 0 && src01[3] == 0) numTransparent += 1;
            if (src11[0] == 0 && src11[1] == 0 && src11[2] == 0 && src11[3] == 0) numTransparent += 1;

            uint8_t *dst = loaded_pixel(result, x, y);
            if (numTransparent > 2) {
                dst[0] = 0;
                dst[1] = 0;
                dst[2] = 0;
                dst[3] = 0;
            } else {
                int r = src00[0] + src10[0] + src01[0] + src11[0];
                int g = src00[1] + src10[1] + src01[1] + src11[1];
                int b = src00[2] + src10[2] + src01[2] + src11[2];
                int a = src00[3] + src10[3] + src01[3] + src11[3];

                int numNotTransparent = 4 - numTransparent;
                dst[0] = r / numNotTransparent;
                dst[1] = g / numNotTransparent;
                dst[2] = b / numNotTransparent;
                dst[3] = a / numNotTransparent;
            }
        }
    }
    return result;
}

LoadedImage convert_tri_to_psx(uint8_t *image, int width, int height, int& alpha_flags) {
    LoadedImage result;
    result.width = width;
    result.height = height;
    result.owned_data.resize(width * height * 4);
    alpha_flags = 0;
    for (int i = 0; i < result.owned_data.size(); i += 4) {
        uint8_t *src = &image[i];
        uint8_t *dst = &result.owned_data[i];
        if (src[3] == 0) {
            // Transparent
            alpha_flags |= ALPHA_FLAG_TRANSPARENT;
            dst[0] = 0;
            dst[1] = 0;
            dst[2] = 0;
            dst[3] = 0;
        } else if (src[3] == 255) {
            alpha_flags |= ALPHA_FLAG_OPAQUE;
            if (src[0] == 0 && src[1] == 0 && src[2] == 0) {
                // Opaque black
                dst[0] = 1;
                dst[1] = 1;
                dst[2] = 1;
                dst[3] = 0;
            } else {
                // Opaque
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 0;
            }
        } else {
            alpha_flags |= ALPHA_FLAG_SEMI_TRANSPARENT;
            if (src[0] == 0 && src[1] == 0 && src[2] == 0) {
                // (0, 0, 0, 255) is a special reserved value
                dst[0] = 1;
                dst[1] = 1;
                dst[2] = 1;
                dst[3] = 255;
            } else {
                // Semi-transparent
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255;
            }
        }
    }
    return result;
}

std::vector<LoadedImage> prepare_texture(RGBAImage &image, int& alpha_flags) {
    std::vector<LoadedImage> levels;
    int width = image.width;
    int height = image.height;
    levels.push_back(convert_tri_to_psx(image.data, width, height, alpha_flags));
    while (width % 2 == 0 && height % 2 == 0) {
        levels.push_back(generate_mip(levels.back()));

        width /= 2;
        height /= 2;
    }
    return levels;
}

/*
void convert_psx_to_tri(uint8_t *image, int width, int height) {
    for (int i = 0; i < width * height * 4; i += 4) {
        uint8_t *pixel = &image[i];
        if (pixel[3] == 0) {
            if (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0) {
                // Transparent
                // do nothing, pixel is already in the correct format
            } else {
                // Opaque
                pixel[3] = 255;
            }
        } else {
            // Semi-transparent
            pixel[3] = 127;
        }
    }
}
*/

void io_thread(void *user_data) {
    std::shared_ptr<IOChannel> *ptr_to_ptr = (std::shared_ptr<IOChannel> *)user_data;
    std::shared_ptr<IOChannel> channel = *ptr_to_ptr;
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread starting\n");
    bool finished = false;
    while (!finished) {
        slock_lock(channel->lock);
        while (channel->requests.size() == 0 && !channel->done) {
            scond_wait(channel->cond, channel->lock);
        }
        if (channel->done) {
            finished = true;
        }
        std::vector<IORequest> requests = std::move(channel->requests); // Take all requests
        channel->requests.clear();
        slock_unlock(channel->lock);

        if (finished) {
            break;
        }

        for (IORequest &request : requests) {
            if (request.kind == IORequestKind::Load) {
                uint32_t hash = request.hash;
                uint32_t palette_hash = request.palette_hash;
                // TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread sees: %x-%x\n", hash, palette_hash);

                // Read in texture
                std::string path = replacement_filename_from_hash(hash, palette_hash);
                // TODO: use formats/image.h instead of stb_image?
                std::unique_ptr<RGBAImage> image = load_image(path.c_str());
                if (image->data != nullptr) {
                    // convert_tri_to_psx(image.data, image.width, image.height);
                
                    // Stick the response in the other vector
                    int alpha_flags_out = 0;
                    std::vector<LoadedImage> levels = prepare_texture(*image, alpha_flags_out);
                    IOResponse response = { hash, palette_hash, alpha_flags_out, std::move(levels) };

                    slock_lock(channel->lock);
                    channel->responses.push_back(std::move(response));
                    slock_unlock(channel->lock);
                } else {
                    TT_LOG(RETRO_LOG_ERROR, "failed to load: %s\n", path.c_str());
                }
            } else if (request.kind == IORequestKind::Dump) {
                // TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread dumping: %s\n", request.path.c_str());
                int success = write_image(request.path.c_str(), request.width, request.height, request.bytes.data());
                if (success == 0) {
                    TT_LOG(RETRO_LOG_ERROR, "failed to write to: %s\n", request.path.c_str());
                }
            }
        }
    }
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread ending\n");
}

IOChannel::IOChannel() {
    lock = slock_new();
    cond = scond_new();
    // TODO: check for NULL
}
IOChannel::~IOChannel() {
    slock_free(lock);
    scond_free(cond);
}

IOThread::IOThread() {
    channel = std::make_shared<IOChannel>();
    sthread_t *thread = sthread_create(io_thread, &channel);
    sthread_detach(thread);
}
IOThread::~IOThread() {
    slock_lock(channel->lock);
    channel->done = true;
    slock_unlock(channel->lock);
    scond_signal(channel->cond);
}

void TextureTracker::dump_image(TextureUpload &upload, UsedMode &mode) {
    uint32_t hash = upload.hash;

    std::vector<uint8_t> bytes;

    // from glsl/vram.h
    int shift;
    switch (mode.mode) {
        case TextureMode::ABGR1555:
            shift = 0;
            break;
        case TextureMode::Palette8bpp:
            shift = 1;
            break;
        case TextureMode::Palette4bpp:
            shift = 2;
            break;
        case TextureMode::None:
        default:
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Tried to dump unused texture %x.\n", hash);
            return; // Early out
    }

    std::string path = dump_path();
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%x", (unsigned)hash);
        path += buf;
    }

    uint16_t *palette = nullptr;
    if (mode.mode == TextureMode::Palette4bpp || mode.mode == TextureMode::Palette8bpp) {
        Rect palette_rect(mode.palette_offset_x, mode.palette_offset_y, mode.mode == TextureMode::Palette8bpp ? 256 : 16, 1);
        Palette p = get_palette(palette_rect);
        if (p.data != nullptr) {
            palette = p.data;
            char buf[16];
            snprintf(buf, sizeof(buf), "-%x", (unsigned)p.hash);
            path += buf;
        }
    }

    if (palette != nullptr) {
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping palette %i, %i.\n", mode.palette_offset_x, mode.palette_offset_y);
    } else if (mode.mode != TextureMode::ABGR1555) {
        path += "-missing";
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "MISSING palette %i, %i.\n", mode.palette_offset_x, mode.palette_offset_y);
    }

    int ppp = 1 << shift;
    int bpp = 16 >> shift;
    int mask = (1 << bpp) - 1;
    for (uint16_t pixel : upload.image) {
        for (int p = 0; p < ppp; p++) {
            uint16_t subpixel = (pixel >> (p * bpp)) & mask;
            if (mode.mode != TextureMode::ABGR1555 && palette == nullptr) {
                // Missing palette, dump a grayscale version of the image data
                bytes.push_back(255.0 * subpixel / mask);
                bytes.push_back(255.0 * subpixel / mask);
                bytes.push_back(255.0 * subpixel / mask);
                bytes.push_back(255.0);
            } else {
                uint16_t abgr1555;
                if (mode.mode == TextureMode::ABGR1555) {
                    abgr1555 = subpixel;
                } else {
                    abgr1555 = palette[subpixel];
                }
                int r = ((abgr1555 >> 0) & 0x1f) * 255.0 / 31.0;
                int g = ((abgr1555 >> 5) & 0x1f) * 255.0 / 31.0;
                int b = ((abgr1555 >> 10) & 0x1f) * 255.0 / 31.0;
                int a = (abgr1555 >> 15) * 255.0;
                // Convert psx to tri
                if (a == 0) {
                    if (r == 0 && g == 0 && b == 0) {
                        // Transparent
                        // do nothing, pixel is already in the correct format
                    } else {
                        // Opaque
                        a = 255;
                    }
                } else {
                    // Semi-transparent
                    a = 127;
                }
                bytes.push_back(r);
                bytes.push_back(g);
                bytes.push_back(b);
                bytes.push_back(a);
            } 
        }
    }

    path += ".png";

    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dump info: mode=%i, w=%i, h=%i, len=%i, bytesLen=%i\n", mode.mode, upload.width, upload.height, upload.image.size(), bytes.size());
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping to %s.\n", path.c_str());

    //stbi_write_png(path.c_str(), upload.width * ppp, upload.height, 4, bytes.data(), 4 * upload.width * ppp);
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "requesting dump: %s\n", path.c_str());
    IORequest dump;
    dump.kind = IORequestKind::Dump;
    dump.path = path;
    dump.width = upload.width * ppp;
    dump.height = upload.height;
    dump.bytes = std::move(bytes);

    slock_lock(iothread.channel->lock);
    iothread.channel->requests.push_back(std::move(dump));
    slock_unlock(iothread.channel->lock);
    scond_signal(iothread.channel->cond);
}

std::set<HdTextureId> read_texture_directory(const char *path) {
    std::set<HdTextureId> result;
    RDIR *dir;
    dir = retro_opendir(path);
    if (dir != NULL) {
        while (retro_readdir(dir)) {
            // https://stackoverflow.com/questions/13701657/control-whole-string-with-sscanf
            uint32_t hash;
            uint32_t palette_hash;
            int chars_read;
            const char *name = retro_dirent_get_name(dir);
            if (sscanf(name, "%x-%x.png%n", &hash, &palette_hash, &chars_read) != 2 ||
                chars_read != strlen(name)
            ) {
                continue;
            }

            result.insert({ hash, palette_hash });
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "file found: %s\n", name);
        }
        retro_closedir(dir);
    }
    return result;
}

TextureTracker::TextureTracker()
{
    known_files = read_texture_directory(replacements_path().c_str());
    TT_LOG(RETRO_LOG_INFO, "num hd textures: %d\n", known_files.size());

    // Read in the dump config file
    dump_ignore = parse_config_file((dump_path() + "/dump.cfg").c_str());
    for (RectMatch m : dump_ignore) {
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "Ignoring %d,%d,%d,%d\n", m.x, m.y, m.w, m.h);
    }
}

SRect toSRect(Rect rect) {
    return SRect(rect.x, rect.y, rect.width, rect.height);
}
Rect fromSRect(SRect rect) {
    return Rect(rect.x, rect.y, rect.width, rect.height);
}

Palette TextureTracker::get_palette(Rect palette_rect) {
    assert(palette_rect.height == 1);

    static std::unordered_set<RectIndex> overlap;
    for (RectIndex index : tracker.overlapping(palette_rect, overlap)) {
        EnduringTextureRect &other = tracker.textures[index]; // TODO: The `other.alive` check is unnecessary because tracker.overlapping never returns dead indices
        if (fromSRect(other.texture_rect.vram_rect).contains(palette_rect) && other.alive) {
            if (other.texture_rect.offset_x != 0 || other.texture_rect.offset_y != 0) {
                continue; // TODO: handle offset subrects
            }
            int x = palette_rect.x - other.texture_rect.vram_rect.x;
            int y = palette_rect.y - other.texture_rect.vram_rect.y;
            int offset = y * other.texture_rect.vram_rect.width + x;
            uint16_t *data = other.texture_rect.upload->image.data() + offset;
            uint32_t hash = crc32(0, (unsigned char*)data, palette_rect.width * sizeof(uint16_t));
            return { data, hash };
        }
    }
    return { nullptr, 0 };
}

uint32_t TextureTracker::get_palette_hash(Rect palette_rect) {
    for (CachedPaletteHash &cached : cached_palette_hashes) {
        if (cached.rect == palette_rect) {
            return cached.hash;
        }
    }
    Palette palette = get_palette(palette_rect);
    if (palette.data != nullptr) {
        cached_palette_hashes.push_back({ palette_rect, palette.hash });
        return palette.hash;
    }
    return 0; // TODO: better way to indicate no palette found?
}

void TextureTracker::clear_palette_cache(Rect rect) {
    cached_palette_hashes.clear();
}

void TextureTracker::clearRegion(Rect rect) {
    if (rect.width == 0 || rect.height == 0) {
        // Some games do this, apparently.
        return;
    }
    tracker.clear(toSRect(rect));
    fused_pages.mark_dead(rect);

    clear_palette_cache(rect);
}

bool imageMatches(TextureUpload &upload, Rect rect, uint16_t *vram) {
    unsigned x = rect.x,
             y = rect.y,
             w = rect.width,
             h = rect.height;
    int index = 0;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (upload.image[index] != vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))]) {
                return false;
            }
            index += 1;
        }
    }
    return true;
}

void TextureTracker::blit(Rect dst, Rect src) {
    tracker.blit(toSRect(dst), toSRect(src));
    fused_pages.mark_dirty(dst);
    fused_pages.rebuild_dirty(tracker, uploader);
    clear_palette_cache(dst);
}

uint32_t TextureTracker::dbgHashVram(Rect rect, uint16_t *vram) {
    std::vector<uint16_t> vec;
    unsigned x = rect.x,
            y = rect.y,
            w = rect.width,
            h = rect.height;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            vec.push_back(vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))]);
        }
    }
    uint32_t hash = crc32(0, (unsigned char*)vec.data(), rect.width * rect.height * sizeof(uint16_t));
    return hash;
}

std::pair<SRect, bool> intersect(SRect a, SRect b) {
    int al = a.left(),   ar = a.right(),  at = a.top(), ab = a.bottom();
    int bl = b.left(),   br = b.right(),  bt = b.top(), bb = b.bottom();
    int left   = (al > bl) ? al : bl;
    int right  = (ar < br) ? ar : br;
    int top    = (at > bt) ? at : bt;
    int bottom = (ab < bb) ? ab : bb;
    int width = right - left;
    int height = bottom - top;
    if (width <= 0 || height <= 0) {
        return std::make_pair(SRect(0, 0, 1, 1), false);
    } else {
        return std::make_pair(SRect(left, top, width, height), true);
    }
}

TextureRect subTexture(TextureRect original, SRect sub_vram_rect) {
    return TextureRect(
        original.upload,
        original.offset_x + sub_vram_rect.left() - original.vram_rect.left(),
        original.offset_y + sub_vram_rect.top() - original.vram_rect.top(),
        sub_vram_rect
    );
}

std::pair<TextureRect, bool> clip_texture_rect_to_vram(TextureRect &t, Rect vram_rect) {
    std::pair<SRect, bool> intersection = intersect(t.vram_rect, toSRect(vram_rect));
    if (intersection.second) {
        return std::make_pair(subTexture(t, intersection.first), true);
    } else {
        return std::make_pair(TextureRect(nullptr, 0, 0, SRect(0, 0, 1, 1)), false);
    }
}

void TextureTracker::notifyReadback(Rect rect, uint16_t *vram) {
    // These hacks are my workaround for the dialog frame texture restorable getting evicted by FMVs
    if (rect.width == 96 && rect.height == 224 && rect.y == 0 && (rect.x % 96) == 0) {
        // HACK: Looks like final fmv frame readback for cross fade, ignore
        return;
    }
    if (rect.width == 64 && rect.height == 224 && rect.y == 0 && (rect.x % 64) == 0) {
        // HACK: Looks like final fmv frame readback for cross fade, ignore
        return;
    }

    uint32_t hash = dbgHashVram(rect, vram);
    
    for (std::vector<RestorableRect>::iterator it = restorable_rects.begin(); it != restorable_rects.end(); ) {
        if (it->rect.intersects(rect)) {
            restorable_rects.erase(it);
        } else {
            it++;
        }
    }

    static std::unordered_set<RectIndex> overlap;

    std::vector<TextureRect> to_restore;
    for (RectIndex index : tracker.overlapping(rect, overlap)) {
        EnduringTextureRect &e = tracker.textures[index];
        if (e.alive) { // TODO: This check is unnecessary because tracker.overlapping never returns dead indices
            // Clip to the requested rect
            std::pair<TextureRect, bool> result = clip_texture_rect_to_vram(e.texture_rect, rect);
            if (result.second) {
                // assert(rect.contains(fromSRect(result.first.vram_rect)));
                to_restore.push_back(result.first);
            }
        }
    }

    restorable_rects.push_back({ rect, hash, std::move(to_restore) });
}

void TextureTracker::upload(Rect rect, uint16_t *vram) {
    clear_palette_cache(rect);

    if (rect.width == FB_WIDTH && rect.height == FB_HEIGHT) {
        // probably loading a save state, this is the entirety of vram
        tracker.clear(toSRect(rect));
        fused_pages.mark_dead(rect);
        return;
    }

    // Would this ever happen?
    if (rect.width == 0 || rect.height == 0) {
        return;
    }

    std::shared_ptr<TextureUpload> upload;
    bool preexisting = false;
    {
        std::vector<uint16_t> vec;
        unsigned x = rect.x,
                y = rect.y,
                w = rect.width,
                h = rect.height;
        for (int j = y; j < y + h; j++) {
            for (int i = x; i < x + w; i++) {
                vec.push_back(vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))]);
            }
        }
        uint32_t hash = crc32(0, (unsigned char*)vec.data(), rect.width * rect.height * sizeof(uint16_t));
        // TODO: check for hash collision, by checking if existing upload has different dimensions. not sure how to recover if it does,
        //       but the odds of a collision are probably much higher than the odds that both textures would be in play simultaneously,
        //       so it'd probably be safe to simply ignore the newest upload and clear instead.
        upload = find_upload(hash);
        if (upload == nullptr) {
            upload = std::make_shared<TextureUpload>();
            upload->image = std::move(vec);
            upload->width = rect.width;
            upload->height = rect.height;
            upload->hash = hash;
            upload->dumpable = true;
            // Don't dump uploads specified by dump.cfg
            for (RectMatch rm : dump_ignore) {
                if (rm.matches(rect)) {
                    upload->dumpable = false;
                    break;
                }
            }
        } else {
            preexisting = true;
        }
    }

    RestorableRect *restore = nullptr;
    for (RestorableRect &other : restorable_rects) {
        if (other.hash == upload->hash && other.rect == rect) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "RESTORATION: %x\n", other.hash);
            restore = &other;
            break;
        }
    }
    if (restore != nullptr) {
        for (TextureRect &t : restore->to_restore) {
            tracker.place(t); // TODO: clip to other.rect
        }
    } else {
        tracker.upload(toSRect(rect), upload);
    }
    fused_pages.mark_dirty(rect);
    fused_pages.rebuild_dirty(tracker, uploader);

    if (!preexisting) {
        load_hd_texture(upload->hash);
    }
}

void TextureTracker::load_hd_texture(uint32_t hash) {
    std::set<HdTextureId>::iterator it_low = known_files.lower_bound({ hash, 0 });
    std::set<HdTextureId>::iterator it_high = known_files.upper_bound({ hash, 0xFFFFFFFF });
    if (it_low != it_high) {
        slock_lock(iothread.channel->lock);
        for (std::set<HdTextureId>::iterator it = it_low; it != it_high; it++) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "requesting texture: %x-%x\n", hash, it->palette_hash);
            IORequest load;
            load.kind = IORequestKind::Load;
            load.hash = hash;
            load.palette_hash = it->palette_hash;
            iothread.channel->requests.push_back(std::move(load));
        }
        slock_unlock(iothread.channel->lock);
        scond_signal(iothread.channel->cond);
    }
}

void output_rect_json(std::ostream &stream, Rect &rect) {
    stream << "{ "
           << "\"x\": " << rect.x << ","
           << "\"y\": " << rect.y << ","
           << "\"width\": " << rect.width << ","
           << "\"height\": " << rect.height
           << "}\n";
}

void TextureTracker::dump_texture(std::shared_ptr<TextureUpload> &upload, UsedMode &mode, DumpedMode dump_mode) {
    if (!upload->dumpable) {
        return;
    }

    bool already_dumped = false;
    for (const DumpedMode &d : upload->dumped_modes) {
        if (d == dump_mode) {
            already_dumped = true;
            break;
        }
    }
    if (!already_dumped) {
        upload->dumped_modes.push_back(dump_mode);
        if (dump_enabled) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping %x\n", upload->hash);
            dump_image(*upload, mode);
        }
    }
}

std::pair<HdTextureHandle, bool> HandleLRUCache::get(Rect rect, uint32_t palette_hash) {
    for (int i = 0; i < entries.size(); i++) {
        CacheEntry &entry = entries[i];
        if (entry.handle.palette_hash == palette_hash && entry.rect.contains(rect)) {
            CacheEntry hit = entry;
            for (int j = i; j > 0; j--) {
                entries[j] = entries[j - 1];
            }
            entries[0] = hit;
            dbg_hits += 1;
            return { hit.handle, true };
        }
    }
    dbg_misses += 1;
    return { HdTextureHandle::make_none(), false };
}
void HandleLRUCache::insert(Rect rect, uint32_t palette_hash, HdTextureHandle handle) {
    if (entries.size() >= max_size) {
        entries.pop_back();
    }
    entries.insert(entries.begin(), { rect, handle });
}
void HandleLRUCache::clear() {
    entries.clear();
}

HdTextureHandle TextureTracker::get_hd_texture_index(Rect rect, UsedMode &mode, unsigned int page_x, unsigned int page_y, bool &fastpath_capable_out, bool &cache_hit) {
    fastpath_capable_out = false;
    Rect palette_rect(mode.palette_offset_x, mode.palette_offset_y, mode.mode == TextureMode::Palette8bpp ? 256 : 16, 1);

    // TODO: I'm pretty sure this doesn't handle TextureMode::ABGR1555

    uint32_t palette_hash = 0;
    cache_hit = false;
    if (hd_textures_enabled || dump_enabled) {
        if (mode.mode == TextureMode::Palette8bpp || mode.mode == TextureMode::Palette4bpp) {
            palette_hash = get_palette_hash(palette_rect);
        }
    }
    if (hd_textures_enabled) {
        // Check if the same texture as last time is used.
        std::pair<HdTextureHandle, bool> cache_result = handle_cache.get(rect, palette_hash);
        cache_hit = cache_result.second;
        if (cache_hit) {
            // cache_result.first is currently always a non-fused, non-none, index + palette_hash
            // in the future it may be useful to cache none, but there's currently no way to check if such a containing rect is still alive (since HdTextureHandle's index would be -1)
            EnduringTextureRect &tex = tracker.textures[cache_result.first.index]; // Forgive me
            if (tex.alive) {
                fastpath_capable_out = fastpath_enabled && (tex.texture_rect.upload->textures[palette_hash].alpha_flags & ALPHA_FLAG_TRANSPARENT) == 0;
                return cache_result.first;
            }
        }
    }

    static std::unordered_set<RectIndex> overlap;
    tracker.overlapping(rect, overlap);

    // Dump texture
    for (RectIndex index : overlap) {
        TextureRect *tex = tracker.get_index(index);
        dump_texture(tex->upload, mode, { mode.mode, palette_hash });
    }
    if (frame_dump != nullptr) {
        if (frame_dump_need_comma) {
            *frame_dump << ",";
        } else {
            frame_dump_need_comma = true;
        }
        *frame_dump << " { \"rect\": ";
        output_rect_json(*frame_dump, rect);
        *frame_dump << ", \"mode\": { \"mode\": " << int(mode.mode)
            << ", \"palette_x\": " << mode.palette_offset_x
            << ", \"palette_y\": " << mode.palette_offset_y
            << "} }\n";
    }

    if (!hd_textures_enabled) {
        fastpath_capable_out = false;
        return HdTextureHandle::make_none();
    }

    HdTextureHandle result = HdTextureHandle::make_none();

    Rect result_rect;
    for (RectIndex index : overlap) {
        TextureRect *tex = tracker.get_index(index);
        std::map<uint32_t, HdImageHandle>::iterator overlapped_image = tex->upload->textures.find(palette_hash);
        if (overlapped_image != tex->upload->textures.end()) {
            if (result == HdTextureHandle::make_none()) {
                // note that if tex->vram_rect contains rect, then it will be the only entry in overlap, so an early out would be pointless
                result_rect = fromSRect(tex->vram_rect);
                fastpath_capable_out = fastpath_enabled && fromSRect(tex->vram_rect).contains(rect) && (overlapped_image->second.alpha_flags & ALPHA_FLAG_TRANSPARENT) == 0;
                result = HdTextureHandle::make(index, palette_hash);
            } else {
                // Multiple overlap, must fuse
                unsigned int width
                    = mode.mode == TextureMode::Palette4bpp ? 64
                    : mode.mode == TextureMode::Palette8bpp ? 128
                    : 256;
                Rect page_rect = { page_x, page_y, width, 256 };
                fastpath_capable_out = false;
                return fused_pages.get_or_make(page_rect, palette_hash, tracker, uploader);
            }
        }
    }

    if (result != HdTextureHandle::make_none()) {
        handle_cache.insert(result_rect, palette_hash, result);
    }
    return result;
}

HdTexture TextureTracker::get_hd_texture(HdTextureHandle handle) {
    if (!handle.fused) {
        // HdTextureHandle's are perhaps too tricky.  They assume that the RectTracker's textures vector hasn't removed anything since the handle was
        // created. So it would seem all you need to do is, in Renderer::reset_queue, call RectTracker::releaseDeadHandles. Except you have to be
        // very very careful that no handles outside of the queues (ie. local) exist across a call to reset_queue.  That is, the handle must go into
        // the queue as soon as possible, otherwise that hd texture might not work (previously it would segfault).
        TextureRect *tex = tracker.get_index(handle.index);
        if (tex == nullptr) {
            if (handle.index != -1) {
                TT_LOG(RETRO_LOG_WARN, "stale HdTextureHandle: %d, %x\n", handle.index, handle.palette_hash);
            }
            return {
                {0, 0, 1, 1},
                {0, 0, int(default_hd_texture->get_width()), int(default_hd_texture->get_height())},
                default_hd_texture
            };
        }
        TextureUpload &upload = *tex->upload;
        // Use find rather than index, because if a stale HdTextureHandle was provided this could segfault
        // because indexing on a key that isn't present would initialize a new one with a null pointer
        std::map<uint32_t, HdImageHandle>::iterator iter = upload.textures.find(handle.palette_hash);
        if (iter == upload.textures.end()) {
            TT_LOG(RETRO_LOG_WARN, "stale HdTextureHandle: %d, %x\n", handle.index, handle.palette_hash);
            return {
                {0, 0, 1, 1},
                {0, 0, int(default_hd_texture->get_width()), int(default_hd_texture->get_height())},
                default_hd_texture
            };
        }
        Vulkan::ImageHandle &image = iter->second.image;
        int scaleX = image->get_width() / upload.width;
        int scaleY = image->get_height() / upload.height;
        SRect texture_subrect = tex->texture_subrect();
        return {
            tex->vram_rect,
            {
                texture_subrect.x * scaleX,
                texture_subrect.y * scaleY,
                texture_subrect.width * scaleX,
                texture_subrect.height * scaleY
            },
            image
        };
    } else {
        return fused_pages.get_from_handle(handle, default_hd_texture);
    }
}

bool is_power_of_two(int n) {
    // https://stackoverflow.com/questions/108318/whats-the-simplest-way-to-test-whether-a-number-is-a-power-of-2-in-c
    return n != 0 && (n & (n - 1)) == 0;
}

// TEMPORARY:
void TextureTracker::on_queues_reset() {
    handle_cache.clear();
    tracker.releaseDeadHandles(); // This is called from reset_queue, so as of now no HdTextureHandle's exist

    // Poll HD uploads

    slock_lock(iothread.channel->lock);
    std::vector<IOResponse> responses = std::move(iothread.channel->responses); // Take the responses
    iothread.channel->responses.clear();
    slock_unlock(iothread.channel->lock);

    for (IOResponse &response : responses) {
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "received texture: %x\n", response.hash);
        std::shared_ptr<TextureUpload> upload = find_upload(response.hash);

        if (upload != nullptr && upload->textures.find(response.palette_hash) == upload->textures.end()) {
            // warn if the hd texture width/height aren't power of 2 multiples of the original vram
            int width = response.levels[0].width;
            int height = response.levels[0].height;

            if (width  % upload->width  == 0 && is_power_of_two(width  / upload->width) &&
                height % upload->height == 0 && is_power_of_two(height / upload->height))
            {
                TT_LOG_VERBOSE(RETRO_LOG_INFO, "Found active texture found for %x (0x%x)\n",
                    response.hash, response.alpha_flags
                );

                Vulkan::ImageHandle texture = uploader->upload_texture(response.levels);

                upload->textures[response.palette_hash] = { texture, response.alpha_flags };

                for (EnduringTextureRect &e : tracker.textures) {
                    if (e.alive && e.texture_rect.upload == upload) {
                        fused_pages.mark_dirty(fromSRect(e.texture_rect.vram_rect));
                    }
                }
            } else {
                TT_LOG(RETRO_LOG_WARN, "Dimension mismatch for %x-%x, original=%dx%d, replacement=%dx%d\n",
                    response.hash,
                    response.palette_hash,
                    upload->width,
                    upload->height,
                    width,
                    height
                );
            }
        }
    }
    fused_pages.rebuild_dirty(tracker, uploader);
    fused_pages.remove_dead();
}
std::shared_ptr<TextureUpload> TextureTracker::find_upload(uint32_t hash) {
    std::shared_ptr<TextureUpload> upload = tracker.find_upload(hash);

    if (upload != nullptr) {
        return upload;
    }

    // backup search, in case it's restorable but currently missing from the rect tracker
    for (RestorableRect &entry : restorable_rects) {
        for (TextureRect &t : entry.to_restore) {
            if (hash == t.upload->hash) {
                return t.upload;
            }
        }
    }

    return nullptr;
}

void TextureTracker::endFrame() {
    frame += 1;

    if (frame % 300 == 0) {
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "hit ratio: %f (%ld, %ld)\n", double(handle_cache.dbg_hits) / (handle_cache.dbg_hits + handle_cache.dbg_misses), handle_cache.dbg_hits, handle_cache.dbg_misses);
        handle_cache.dbg_hits = 0;
        handle_cache.dbg_misses = 0;
    }

    if (frame_dump != nullptr) {
        *frame_dump << "]}\n";
        delete frame_dump;
        frame_dump = nullptr;
    }

    if (dbg_input_state_cb != 0) {
        if (frame_dump_key.query()) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Left bracket!\n");
            frame_dump = new std::ofstream(dump_path() + "test_dump.json");
            frame_dump_need_comma = false;
            *frame_dump << "{ \"initial\": [\n";
            bool need_comma = false;
            for (EnduringTextureRect &etexture : tracker.textures) {
                if (!etexture.alive) continue;
                TextureRect &texture = etexture.texture_rect;
                if (need_comma) {
                    *frame_dump << ",";
                } else {
                    need_comma = true;
                }
                *frame_dump << " { \"rect\": ";
                Rect rect = fromSRect(texture.vram_rect);
                output_rect_json(*frame_dump, rect);
                *frame_dump << ", \"hash\": \"" << std::hex << texture.upload->hash << "\" }\n" << std::dec;
            }
            *frame_dump << "], \"events\": [\n";
        }

        if (hd_toggle_key.query()) {
            hd_textures_enabled = !hd_textures_enabled;
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Toggling hd textures: %s\n", hd_textures_enabled ? "on" : "off");
        }

        if (reload_key.query()) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Reloading hd textures from disk\n");
            reload_textures_from_disk();
        }

        if (fastpath_key.query()) {
            fastpath_enabled = !fastpath_enabled;
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Toggling fastpath %s\n", fastpath_enabled ? "ON" : "OFF");
        }
    }
}

void TextureTracker::set_texture_uploader(Renderer *t) {
    uploader = t;
    std::vector<LoadedImage> default_levels;

    LoadedImage default_image;
    default_image.width = 1;
    default_image.height = 1;
    default_image.owned_data.push_back(0);
    default_image.owned_data.push_back(0);
    default_image.owned_data.push_back(0);
    default_image.owned_data.push_back(0);
    default_levels.push_back(std::move(default_image));

    default_hd_texture = uploader->upload_texture(default_levels);
}

void TextureTracker::reload_textures_from_disk() {
    // Reload the directory listing
    known_files = read_texture_directory(replacements_path().c_str());
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Found %d hd textures\n", known_files.size());

    // Delete existing textures
    std::set<uint32_t> hashes;
    for (EnduringTextureRect &texture : tracker.textures) {
        texture.texture_rect.upload->textures.clear();
        hashes.insert(texture.texture_rect.upload->hash);
    }
    for (RestorableRect &restorable : restorable_rects) {
        for (TextureRect &tr : restorable.to_restore) {
            tr.upload->textures.clear();
            hashes.insert(tr.upload->hash);
        }
    }

    // Delete fused textures
    fused_pages.mark_dead({0, 0, FB_WIDTH, FB_HEIGHT});

    // Issue requests to the iothread to load the hd textures
    for (uint32_t hash : hashes) {
        load_hd_texture(hash);
    }
}

// RectTracker

bool intersects(SRect a, SRect b) {
    return !(
        a.left() >= b.right() ||
        b.left() >= a.right() ||
        a.top() >= b.bottom() ||
        b.top() >= a.bottom()
    );
}

SRect bounds(int left, int right, int top, int bottom) {
    return SRect(left, top, right - left, bottom - top);
}

void split(SRect original, SRect remove, SRect *results, unsigned &count) {
    std::pair<SRect, bool> intersectionResult = intersect(original, remove);
    if (!intersectionResult.second) {
        results[count++] = original;
        return;
    }

    SRect intersection = intersectionResult.first;

    // Top rect
    if (intersection.top() > original.top()) {
        results[count++] = bounds(
            original.left(),
            original.right(),
            original.top(),
            intersection.top()
        );
    }

    // Bottom rect
    if (intersection.bottom() < original.bottom()) {
        results[count++] = bounds(
            original.left(),
            original.right(),
            intersection.bottom(),
            original.bottom()
        );
    }

    // Left rect
    if (intersection.left() > original.left()) {
        results[count++] = bounds(
            original.left(),
            intersection.left(),
            intersection.top(),
            intersection.bottom()
        );
    }

    // Right rect
    if (intersection.right() < original.right()) {
        results[count++] = bounds(
            intersection.right(),
            original.right(),
            intersection.top(),
            intersection.bottom()
        );
    }
}

void RectTracker::upload(SRect rect, std::shared_ptr<TextureUpload> upload) {
    TextureRect texture(upload, 0, 0, rect);
    place(texture);
    lookup_grid_dirty = true;
}

SRect moved(SRect rect, int dx, int dy) {
    return SRect(rect.x + dx, rect.y + dy, rect.width, rect.height);
}

void RectTracker::blit(SRect dst, SRect src) {
    std::vector<TextureRect> to_place;
    int moveX = dst.x - src.x;
    int moveY = dst.y - src.y;
    for (EnduringTextureRect &eold : textures) {
        if (eold.alive) {
            TextureRect &old = eold.texture_rect;
            std::pair<SRect, bool> intersection = intersect(old.vram_rect, src);
            if (intersection.second) {
                TextureRect sub = subTexture(old, intersection.first);
                TextureRect subMoved = TextureRect(sub.upload, sub.offset_x, sub.offset_y, moved(sub.vram_rect, moveX, moveY));
                to_place.push_back(subMoved);
            }
        }
    }
    clear_rect(dst);
    for (TextureRect &t : to_place) {
        place(t);
    }
    lookup_grid_dirty = true;
}
void RectTracker::clear(SRect rect) {
    clear_rect(rect);
    lookup_grid_dirty = true;
}
void RectTracker::releaseDeadHandles() {
    std::vector<EnduringTextureRect>::iterator retainedIt = textures.begin();
    for (std::vector<EnduringTextureRect>::iterator it = textures.begin(); it != textures.end(); it++) {
        if (it->alive) {
            *retainedIt = *it;
            retainedIt++;
        }
    }
    textures.erase(retainedIt, textures.end());

    lookup_grid_dirty = true;
}

std::unordered_set<RectIndex>& RectTracker::overlapping(Rect uvrect, std::unordered_set<RectIndex> &results) {
    if (lookup_grid_dirty) {
        rebuild_lookup_grid();
    }

    // TODO: remove this when renderer/build_attribs doesn't have an unnecessary - 1
    if (uvrect.width == 0) {
        uvrect.width = 1;
    }

    SRect rect = toSRect(uvrect);

    results.clear();
    lookup_grid.get(rect, results);
    return results;
}

TextureRect* RectTracker::get_index(RectIndex index) {
    if (index < 0 || index >= textures.size()) {
        return nullptr;
    }
    return &textures[index].texture_rect;
}

void RectTracker::clear_rect(SRect &rect) {
    SRect splits[4];
    unsigned splits_count = 0;

    std::vector<TextureRect> newTextures;
    for (EnduringTextureRect &eold : textures) {
        if (eold.alive) {
            TextureRect &old = eold.texture_rect;

            splits_count = 0;
            split(old.vram_rect, rect, splits, splits_count);
            if (splits_count == 1 && splits[0] == old.vram_rect) {
                // The rect didn't split, do nothing
            } else {
                // The rect split, mark this texture as dead and push its splits to be added
                eold.alive = false;
                for (unsigned i = 0; i < splits_count; i++) {
                    newTextures.push_back(subTexture(old, splits[i]));
                }
            }
        }
    }
    for (TextureRect newTexture : newTextures) {
        textures.push_back({ newTexture, true });
    }
}
void RectTracker::place(TextureRect texture) {
    clear_rect(texture.vram_rect);
    textures.push_back({ texture, true });
}

void RectTracker::rebuild_lookup_grid() {
    lookup_grid.clear();
    for (int i = 0; i < textures.size(); i++) {
        if (textures[i].alive) {
            lookup_grid.insert(textures[i].texture_rect.vram_rect, i);
        }
    }
    lookup_grid_dirty = false;
}

std::shared_ptr<TextureUpload> RectTracker::find_upload(uint32_t hash) {
    for (EnduringTextureRect &eold : textures) {
        if (eold.texture_rect.upload->hash == hash) {
            return eold.texture_rect.upload;
        }
    }
    return nullptr;
}

int clamp(int x, int low, int high) {
    if (x < low)  x = low;
    if (x > high) x = high;
    return x;
}

struct CellBounds {
    int lowX;
    int highX; // exclusive
    int lowY;
    int highY; // exclusive
};

CellBounds cellBounds(SRect vram) {
    return CellBounds({
        clamp(vram.left() / LOOKUP_CELL_WIDTH, 0, LOOKUP_GRID_COLUMNS),
        clamp(ceil(vram.right() / float(LOOKUP_CELL_WIDTH)), 0, LOOKUP_GRID_COLUMNS),
        clamp(vram.top() / LOOKUP_CELL_HEIGHT, 0, LOOKUP_GRID_ROWS),
        clamp(ceil(vram.bottom() / float(LOOKUP_CELL_HEIGHT)), 0, LOOKUP_GRID_ROWS)
    });
}

void LookupGrid::insert(SRect r, RectIndex index) {
    CellBounds c = cellBounds(r);
    for (int x = c.lowX; x < c.highX; x++) {
        for (int y = c.lowY; y < c.highY; y++) {
            cells[y * LOOKUP_GRID_COLUMNS + x].push_back({r, index});
        }
    }
}
void LookupGrid::get(SRect r, std::unordered_set<RectIndex> &results) {
    CellBounds c = cellBounds(r);
    for (int x = c.lowX; x < c.highX; x++) {
        for (int y = c.lowY; y < c.highY; y++) {
            for (LookupEntry &entry : cells[y * LOOKUP_GRID_COLUMNS + x]) {
                if (intersects(entry.rect, r)) {
                    results.insert(entry.index);
                }
            }
        }
    }
}
void LookupGrid::clear() {
    for (int i = 0; i < LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS; i++) {
        cells[i].clear();
    }
}

// FusedPages

int64_t page_bytes(FusionRects &fusion) {
    return fusion.scaleX * fusion.scaleY * fusion.vram_rect.width * fusion.vram_rect.height * 4;
}

void FusedPages::dbg_print_info() {
    int64_t num_bytes = 0;
    for (FusedPage &page : pages) {
        num_bytes += page_bytes(page.fusion);
    }
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Fused Pages: %lu, Bytes: %ld (%.1f MiB)\n", pages.size(), num_bytes, num_bytes / 1048576.0);
}

bool srect_gt(const SRect &a, const SRect &b) {
    if (a.x != b.x)
        return a.x > b.x;
    if (a.y != b.y)
        return a.y > b.y;
    if (a.width != b.width)
        return a.width > b.width;
    return a.height > b.height;
}

static bool texture_rect_sort_gt(const TextureRect &a, const TextureRect &b) {
    // Compare .upload by internal pointer
    if (a.upload.get() != b.upload.get())
        return a.upload.get() > b.upload.get();
    if (a.vram_rect != b.vram_rect)
        return srect_gt(a.vram_rect, b.vram_rect);
    return srect_gt(a.texture_subrect(), b.texture_subrect());
}

FusionRects fusion_rects(Rect full_page_rect, uint32_t palette_hash, RectTracker &tracker) {
    FusionRects f;
    f.scaleX = 0;
    f.scaleY = 0;
    f.vram_rect = {0, 0, 0, 0};

    for (EnduringTextureRect &e : tracker.textures) {
        if (!e.alive) {
            continue;
        }
        std::pair<SRect, bool> intersection = intersect(toSRect(full_page_rect), e.texture_rect.vram_rect);
        if (intersection.second) {
            TextureUpload &upload = *e.texture_rect.upload;
            std::map<uint32_t, HdImageHandle>::iterator hd_texture = upload.textures.find(palette_hash);
            if (hd_texture != upload.textures.end()) {
                // Clip to the destination texture (important, otherwise it might blit out of bounds which may have wrought havoc upon my sanity)
                TextureRect clipped = subTexture(e.texture_rect, intersection.first);
                f.scaleX = std::max(f.scaleX, hd_texture->second.image->get_width() / upload.width);
                f.scaleY = std::max(f.scaleY, hd_texture->second.image->get_height() / upload.height);
                Rect r = fromSRect(clipped.vram_rect);
                if (f.vram_rect.width == 0) {
                    f.vram_rect = r;
                } else {
                    f.vram_rect.extend_bounding_box(r);
                }
                f.rects.push_back(clipped);
            }
        }
    }

    // Sort rects so that the vector itself can be compared
    std::sort(f.rects.begin(), f.rects.end(), texture_rect_sort_gt);

    return f;
}

void rebuild_page(FusedPage &page, RectTracker &tracker, Renderer *uploader) {
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilding page for %x, %d,%d %dx%d\n",
        page.palette,
        page.fusion.vram_rect.x,
        page.fusion.vram_rect.y,
        page.fusion.vram_rect.width,
        page.fusion.vram_rect.height
    );

    page.dirty = false;

    {
        FusionRects fusion = fusion_rects(page.full_page_rect, page.palette, tracker);
        if (page.fusion == fusion) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: no change\n");
            return;
        }
        page.fusion = std::move(fusion);
    }

    if (page.fusion.rects.size() == 0) {
        page.dead = true;
        page.texture.reset();
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: page is now dead\n");
        return;
    }

    // assert(page.scaleX > 0 && page.scaleX <= 64);
    // assert(page.scaleY > 0 && page.scaleY <= 64);

    Vulkan::CommandBufferHandle &cmd = uploader->command_buffer_hack_fixme();

    int texture_width = page.fusion.vram_rect.width * page.fusion.scaleX;
    int texture_height = page.fusion.vram_rect.height * page.fusion.scaleY;
    // assert(texture_width > 0 && texture_width < 10000);
    // assert(texture_height > 0 && texture_height < 10000);

    // TODO: I don't know SHIT about barriers.
    
    // special sentinel value
    // Note that due to the way textures are put into a page, these special values will not bleed into neighbors in the mipmaps,
    // because the mipmaps are only used down to the original resolution, and hd textures are aligned to that original resolution's
    // texels.
    VkClearValue fallthrough = {0.0, 0.0, 0.0, 1.0};

    int mip_levels = log2(std::min(page.fusion.scaleX, page.fusion.scaleY)) + 1;

    if (page.texture && page.texture->get_width() == texture_width && page.texture->get_height() == texture_height) {
        // Switch back into transfer dst layout
        cmd->image_barrier(*page.texture,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

        cmd->clear_image(*page.texture, fallthrough);
    } else {
        page.texture = uploader->create_texture(texture_width, texture_height, mip_levels);
        cmd->clear_image(*page.texture, fallthrough);
    }

    // Second pass to blit all the existing textures into the new texture
    for (TextureRect &tex : page.fusion.rects) {
        TextureUpload &upload = *tex.upload;

        std::map<uint32_t, HdImageHandle>::iterator hd_texture = upload.textures.find(page.palette);
        if (hd_texture == upload.textures.end()) {
            // That's odd
            continue;
        }

        Vulkan::ImageHandle &image = hd_texture->second.image;

        int srcWidth = image->get_width();
        int srcHeight = image->get_height();

        int sx = srcWidth / upload.width;
        int sy = srcHeight / upload.height;

        int rx = page.fusion.scaleX / sx;
        int ry = page.fusion.scaleY / sy;

        SRect subrect = tex.texture_subrect();

        VkOffset3D dst_offset = {
            (tex.vram_rect.x - int(page.fusion.vram_rect.x)) * int(page.fusion.scaleX),
            (tex.vram_rect.y - int(page.fusion.vram_rect.y)) * int(page.fusion.scaleY),
            0
        };
        VkOffset3D dst_extent = {
            tex.vram_rect.width * int(page.fusion.scaleX),
            tex.vram_rect.height * int(page.fusion.scaleY),
            1
        };

        // Switch into transfer src
        // what the fuck am I doing?
        cmd->image_barrier(
            *image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0
        );

        // Blit into every mipmap level down to base vram
        // TODO: this isn't a great way to do this, will probably be blurrier than it could be if src and dst aspect ratios are different
        // TODO: is this line even right? it sure doesn't look right
        int full_res_levels = log2(std::max(rx, ry)) + 1;
        // assert(max_level >= 0 && max_level <= 6);
        // TODO: this is incredibly finicky, and one bad (out of bounds) blit can bork everything
        for (int dstLevel = 0; dstLevel < mip_levels; dstLevel++) {
            int srcLevel = std::max(0, dstLevel - full_res_levels);

            cmd->blit_image(*page.texture, *image,
                dst_offset,
                dst_extent,
                {
                    (sx * subrect.x) >> srcLevel,
                    (sy * subrect.y) >> srcLevel,
                    0
                },
                { 
                    (sx * subrect.width) >> srcLevel,
                    (sy * subrect.height) >> srcLevel,
                    1
                },
                dstLevel,
                srcLevel
            );
            
            dst_offset.x >>= 1;
            dst_offset.y >>= 1;
            dst_extent.x = std::max(dst_extent.x >> 1, 1);
            dst_extent.y = std::max(dst_extent.y >> 1, 1);
        }

        // Change back to shader read
        cmd->image_barrier(
            *image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0
        );
    }

    // I have no idea what the fuck I'm doing
    // Make the fused texture readable by shaders
    cmd->image_barrier(*page.texture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: page now %ux%u, %ld bytes (%.1f MiB)\n",
        page.fusion.vram_rect.width * page.fusion.scaleX, page.fusion.vram_rect.height * page.fusion.scaleY,
        page_bytes(page.fusion), page_bytes(page.fusion) / 1048576.0
    );
}

HdTexture FusedPages::get_from_handle(HdTextureHandle handle, Vulkan::ImageHandle &default_hd_texture) {
    if (handle.index < 0 || handle.index >= pages.size()) {
        TT_LOG(RETRO_LOG_WARN, "BAD fused index!\n");
        return {
            {0, 0, 1, 1},
            {0, 0, int(default_hd_texture->get_width()), int(default_hd_texture->get_height())},
            default_hd_texture
        };
    }
    FusedPage &page = pages[handle.index];
    if (!page.texture) {
        TT_LOG(RETRO_LOG_WARN, "Missing fused texture!\n");
        return {
            {0, 0, 1, 1},
            {0, 0, int(default_hd_texture->get_width()), int(default_hd_texture->get_height())},
            default_hd_texture
        };
    }
    return {
        toSRect(page.fusion.vram_rect),
        { 0, 0, int(page.texture->get_width()), int(page.texture->get_height()) },
        page.texture
    };
}

HdTextureHandle FusedPages::get_or_make(Rect page_rect, uint32_t palette, RectTracker &tracker, Renderer *uploader) {
    for (int x = 0; x < pages.size(); x++) {
        FusedPage &page = pages[x];
        if (!page.dead && page.palette == palette && page.full_page_rect == page_rect) {
            // return page
            return HdTextureHandle::make_fused(x);
        }
    }

    // Make a new fused page
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Creating new fused page for palette %x\n", palette);
    FusedPage page;
    page.dead = false;
    page.dirty = false;
    page.full_page_rect = page_rect;
    page.palette = palette;
    rebuild_page(page, tracker, uploader);
    pages.push_back(page);
    return HdTextureHandle::make_fused(pages.size() - 1);
}
void FusedPages::mark_dirty(Rect rect) {
    for (FusedPage &page : pages) {
        if (!page.dead && page.full_page_rect.intersects(rect)) {
            page.dirty = true;
        }
    }
}
void FusedPages::mark_dead(Rect rect) {
    for (FusedPage &page : pages) {
        if (!page.dead && page.full_page_rect.intersects(rect)) {
            page.dead = true;
        }
    }
}
void FusedPages::rebuild_dirty(RectTracker &tracker, Renderer *uploader) {
    bool changed = false;
    for (FusedPage &page : pages) {
        if (!page.dead && page.dirty) {
            rebuild_page(page, tracker, uploader);
            changed = true;
        }
    }
    if (changed) {
        dbg_print_info();
    }
}
void FusedPages::remove_dead() {
    std::vector<FusedPage>::iterator retainedIt = pages.begin();
    for (std::vector<FusedPage>::iterator it = pages.begin(); it != pages.end(); it++) {
        if (!it->dead) {
            *retainedIt = *it;
            retainedIt++;
        }
    }
    pages.erase(retainedIt, pages.end());
}


//========================================
// Save State

TextureUpload copy_upload_without_handles(const TextureUpload &to_copy) {
    TextureUpload copy = to_copy;
    copy.textures.clear();
    return copy;
}

TextureRectSaveState to_save_state(const TextureRect &t, std::map<uint32_t, TextureUpload> &uploads) {
    uint32_t hash = t.upload->hash;
    if (uploads.find(hash) == uploads.end()) {
        uploads[hash] = copy_upload_without_handles(*t.upload);
    }
    return {
        t.upload->hash,
        t.offset_x,
        t.offset_y,
        t.vram_rect
    };
}
TextureRect from_save_state(const TextureRectSaveState &t, std::map<uint32_t, std::shared_ptr<TextureUpload>> &uploads) {
    std::map<uint32_t, std::shared_ptr<TextureUpload>>::iterator it = uploads.find(t.upload_hash);
    if (it == uploads.end()) {
        TT_LOG(RETRO_LOG_ERROR, "SaveState upload missing!\n");
    }
    return {
        it->second,
        t.offset_x,
        t.offset_y,
        t.vram_rect
    };
}

TextureTrackerSaveState TextureTracker::save_state() {
    TextureTrackerSaveState state;

    for (EnduringTextureRect &r : tracker.textures) {
        if (r.alive) {
            state.rects.push_back(to_save_state(r.texture_rect, state.uploads));
        }
    }
    for (RestorableRect &r : restorable_rects) {
        RestorableRectSaveState saved;
        saved.hash = r.hash;
        saved.rect = r.rect;
        for (TextureRect &t : r.to_restore) {
            saved.to_restore.push_back(to_save_state(t, state.uploads));
        }
        state.restorable.push_back(std::move(saved));
    }

    return state;
}


void TextureTracker::load_state(const TextureTrackerSaveState &state) {
    std::map<uint32_t, std::shared_ptr<TextureUpload>> uploads;
    for (std::map<uint32_t, TextureUpload>::const_iterator it = state.uploads.begin(); it != state.uploads.end(); it++) {
        std::shared_ptr<TextureUpload> ptr = std::shared_ptr<TextureUpload>(new TextureUpload);
        *ptr = it->second;
        uploads[it->first] = std::move(ptr);
    }

    clearRegion({ 0, 0, FB_WIDTH, FB_HEIGHT });
    tracker.textures.clear(); // load_state should only be called right after creating this TextureTracker, so this ought to be empty already anyway
    for (const TextureRectSaveState &r : state.rects) {
        tracker.place(from_save_state(r, uploads));
    }
    restorable_rects.clear();
    for (const RestorableRectSaveState &r : state.restorable) {
        RestorableRect loaded;
        loaded.hash = r.hash;
        loaded.rect = r.rect;
        for (const TextureRectSaveState &t : r.to_restore) {
            loaded.to_restore.push_back(from_save_state(t, uploads));
        }
        restorable_rects.push_back(std::move(loaded));
    }
    // Need to reload the hd textures, too
    for (std::map<uint32_t, TextureUpload>::const_iterator it = state.uploads.begin(); it != state.uploads.end(); it++) {
        load_hd_texture(it->first);
    }
}
// End of Save State
//========================================


bool DbgHotkey::query() {
    uint16_t state = dbg_input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, key);
    bool is_key_down = state != 0;
    bool just_pressed = is_key_down && !was_key_down;
    was_key_down = is_key_down;
    return just_pressed;
}

}
#include "libretro_vulkan.h"

// #include "mednafen/mednafen.h" is required
// for #include "mednafen/psx/gpu.h" to work properly.
#include "mednafen/mednafen.h"
#include "mednafen/psx/gpu.h"

#include "libretro_cbs.h"
#include "libretro_options.h"
#include "beetle_psx_globals.h"

using namespace Vulkan;
using namespace PSX;
using namespace std;

static Context *context = nullptr;
static Device *device = nullptr;
static Renderer *renderer = nullptr;
static unsigned scaling = 4;

extern enum rsx_renderer_type rsx_type;
extern retro_log_printf_t log_cb;
namespace Granite
{
retro_log_printf_t libretro_log;
}

static retro_hw_render_callback hw_render;
static const struct retro_hw_render_interface_vulkan *vulkan;
static vector<retro_vulkan_image> swapchain_images;
static vector<ImageHandle> scanout_handles;
static Renderer::SaveState save_state;
static bool inside_frame;
static bool has_software_fb;
static bool scaled_uv_offset;
static int filter_exclude_sprites;
static int filter_exclude_2d_polygons;
static bool adaptive_smoothing;
static bool super_sampling;
static unsigned msaa = 1;
static bool mdec_yuv;
/*
 * Queue for rsx_vulkan_* operations that arrive between the libretro
 * frontend's RETRO_ENVIRONMENT_SET_HW_RENDER acceptance and the
 * frontend invoking vk_context_reset. Drained at the end of
 * vk_context_reset, after the renderer is constructed. This used to be
 * a std::vector<std::function<void()>> with per-entry-point lambdas;
 * it was migrated to the shared C-callable rsx_defer module so the GL
 * backend (a C TU, can't use std::function) can use the same mechanism
 * and so both backends share a single defer policy. See
 * rsx/rsx_defer.h for which ops are deferred and which are dropped.
 */
static rsx_defer_queue_t defer = { nullptr, 0, 0 };
static dither_mode dither_mode = DITHER_NATIVE;
static bool dump_textures = false;
static bool replace_textures = false;
static bool track_textures = false;
/*
 * File-local copy of the crop_overscan core option, distinct
 * from the cross-TU `crop_overscan` global declared in
 * beetle_psx_globals.h. Both are populated from the same
 * BEETLE_OPT(crop_overscan) env var (in parallel - libretro.cpp
 * writes the global, this file writes vulkan_crop_overscan), so
 * their values track identically; renaming here just makes the
 * shadow explicit and stops the static-after-extern conflict
 * that fc4d742's switch to the central globals header surfaced.
 */
static int vulkan_crop_overscan;
static int image_offset_cycles;
static unsigned image_crop;
static int initial_scanline;
static int last_scanline;
static int initial_scanline_pal;
static int last_scanline_pal;
static bool frame_duping_enabled = false;
static uint32_t prev_frame_width = 320;
static uint32_t prev_frame_height = 240;
static bool show_vram = false;

static retro_video_refresh_t video_refresh_cb;

static const VkApplicationInfo *get_application_info(void)
{
   static const VkApplicationInfo info = {
      VK_STRUCTURE_TYPE_APPLICATION_INFO,
      nullptr,
      "Beetle PSX",
      0,
      "parallel-psx",
      0,
      VK_MAKE_VERSION(1, 0, 32),
   };
   return &info;
}

/*
 * Dispatcher for the deferred-op queue. Called once per queued op
 * during vk_context_reset's drain. Each case re-enters the matching
 * rsx_vulkan_<op>() entry point with the captured arguments; by drain
 * time the renderer is up so the entry point's "renderer present"
 * branch executes and actually performs the work that the original
 * call could not. The user pointer is unused - everything the entry
 * points need is in this TU's file-statics.
 */
static void vk_defer_dispatch(void *user, const rsx_defer_op_t *op)
{
   (void)user;
   if (!op)
      return;

   switch (op->kind)
   {
      case RSX_DEFER_SET_TEX_WINDOW:
         rsx_vulkan_set_tex_window(op->u.set_tex_window.tww,
                                   op->u.set_tex_window.twh,
                                   op->u.set_tex_window.twx,
                                   op->u.set_tex_window.twy);
         break;
      case RSX_DEFER_SET_DRAW_OFFSET:
         rsx_vulkan_set_draw_offset(op->u.set_draw_offset.x,
                                    op->u.set_draw_offset.y);
         break;
      case RSX_DEFER_SET_DRAW_AREA:
         rsx_vulkan_set_draw_area(op->u.set_draw_area.x0,
                                  op->u.set_draw_area.y0,
                                  op->u.set_draw_area.x1,
                                  op->u.set_draw_area.y1);
         break;
      case RSX_DEFER_SET_VRAM_FRAMEBUFFER_COORDS:
         rsx_vulkan_set_vram_framebuffer_coords(
               op->u.set_vram_framebuffer_coords.xstart,
               op->u.set_vram_framebuffer_coords.ystart);
         break;
      case RSX_DEFER_SET_HORIZONTAL_DISPLAY_RANGE:
         rsx_vulkan_set_horizontal_display_range(
               op->u.set_horizontal_display_range.x1,
               op->u.set_horizontal_display_range.x2);
         break;
      case RSX_DEFER_SET_VERTICAL_DISPLAY_RANGE:
         rsx_vulkan_set_vertical_display_range(
               op->u.set_vertical_display_range.y1,
               op->u.set_vertical_display_range.y2);
         break;
      case RSX_DEFER_SET_DISPLAY_MODE:
         rsx_vulkan_set_display_mode(op->u.set_display_mode.depth_24bpp,
                                     op->u.set_display_mode.is_pal,
                                     op->u.set_display_mode.is_480i,
                                     op->u.set_display_mode.width_mode);
         break;
      case RSX_DEFER_LOAD_IMAGE:
         rsx_vulkan_load_image(op->u.load_image.x,
                               op->u.load_image.y,
                               op->u.load_image.w,
                               op->u.load_image.h,
                               op->u.load_image.vram,
                               op->u.load_image.mask_test,
                               op->u.load_image.set_mask);
         break;
      case RSX_DEFER_TOGGLE_DISPLAY:
         rsx_vulkan_toggle_display(op->u.toggle_display.status);
         break;
   }
}

static void vk_context_reset(void)
{
   if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void**)&vulkan) || !vulkan)
      return;

   if (vulkan->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
   {
      vulkan = nullptr;
      return;
   }

   assert(context);
   device = new Device;
   device->set_context(*context);

   renderer = new Renderer(*device, scaling, msaa, save_state.vram.empty() ? nullptr : &save_state);
   if (!renderer->is_valid())
   {
      delete renderer;
      delete device;
      renderer = nullptr;
      device = nullptr;
      vulkan = nullptr;
      return;
   }

   tt_log_startup("vk renderer init: scaling=%u msaa=%u has_software_fb=%d\n",
         (unsigned)scaling, (unsigned)msaa, (int)has_software_fb);

   /* Replay any rsx_vulkan_* state-sets / VRAM uploads that arrived
    * between rsx_vulkan_open's SET_HW_RENDER and this context_reset
    * firing. By this point `renderer` is non-null so each call lands
    * on the live renderer instead of being silently dropped. */
   if (rsx_defer_count(&defer) > 0)
      rsx_defer_drain(&defer, vk_defer_dispatch, nullptr);

   renderer->flush();
}

static void vk_context_destroy(void)
{
   if (device == nullptr)
      return;

   save_state = renderer->save_vram_state();
   vulkan     = nullptr;
   scanout_handles.clear();
   swapchain_images.clear();

   delete renderer;
   delete device;
   delete context;
   renderer = nullptr;
   device = nullptr;
   context = nullptr;

   /* Free the deferred-op storage. The pre-migration C++ defer queue
    * never cleared on destroy, which meant any ops queued in the
    * (rare) gap between destroy and the next reset accumulated; that
    * was a latent leak that nobody noticed because the gap is normally
    * empty. The new policy clears here, matching gl_context_destroy
    * and keeping behaviour symmetric across backends. */
   rsx_defer_clear(&defer);
}

static bool libretro_create_device(
      struct retro_vulkan_context *libretro_context,
      VkInstance instance,
      VkPhysicalDevice gpu,
      VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get_instance_proc_addr,
      const char **required_device_extensions,
      unsigned num_required_device_extensions,
      const char **required_device_layers,
      unsigned num_required_device_layers,
      const VkPhysicalDeviceFeatures *required_features)
{
   if (!Vulkan::Context::init_loader(get_instance_proc_addr))
      return false;

   if (context)
   {
      delete context;
      context = nullptr;
   }

   /* parallel-psx's Vulkan::Context constructor used to throw on
    * failure; it now sets a valid=false flag instead. The
    * try/catch boundary is gone with it. */
   context = new Vulkan::Context(instance, gpu, surface, required_device_extensions, num_required_device_extensions,
                                 required_device_layers, num_required_device_layers,
                                 required_features);
   if (!context->is_valid())
   {
      delete context;
      context = nullptr;
      return false;
   }

   context->release_device();
   libretro_context->gpu = context->get_gpu();
   libretro_context->device = context->get_device();
   libretro_context->presentation_queue = context->get_graphics_queue();
   libretro_context->presentation_queue_family_index = context->get_graphics_queue_family();
   libretro_context->queue = context->get_graphics_queue();
   libretro_context->queue_family_index = context->get_graphics_queue_family();
   return true;
}

bool rsx_vulkan_open(bool is_pal)
{
   Granite::libretro_log = log_cb;
   content_is_pal = is_pal;

   hw_render.context_type    = RETRO_HW_CONTEXT_VULKAN;
   hw_render.version_major   = VK_MAKE_VERSION(1, 0, 32);
   hw_render.version_minor   = 0;
   hw_render.context_reset   = vk_context_reset;
   hw_render.context_destroy = vk_context_destroy;
   hw_render.cache_context   = false;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   static const struct retro_hw_render_context_negotiation_interface_vulkan iface = {
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION,

      get_application_info,
      libretro_create_device,
      nullptr,
   };

   environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void*)&iface);

   return true;
}

void rsx_vulkan_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
}

void rsx_vulkan_set_video_refresh(retro_video_refresh_t cb)
{
   video_refresh_cb = cb;
}

void rsx_vulkan_get_system_av_info(struct retro_system_av_info *info)
{
   rsx_vulkan_refresh_variables();

   memset(info, 0, sizeof(*info));

   // Set retro_game_geometry
   info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W;
   info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H;
   info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W * (super_sampling ? 1 : scaling);
   info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H * (super_sampling ? 1 : scaling);
   info->geometry.aspect_ratio = rsx_common_get_aspect_ratio(content_is_pal, vulkan_crop_overscan,
                                       content_is_pal ? initial_scanline_pal : initial_scanline,
                                       content_is_pal ? last_scanline_pal : last_scanline,
                                       aspect_ratio_setting, show_vram, widescreen_hack, widescreen_hack_aspect_ratio_setting);

   // Set retro_system_timing
   info->timing.fps = rsx_common_get_timing_fps();
   info->timing.sample_rate = SOUND_FREQUENCY;
}

void rsx_vulkan_refresh_variables(void)
{
   struct retro_variable var = {0};

   var.key = BEETLE_OPT(renderer_software_fb);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         has_software_fb = true;
      else
         has_software_fb = false;
   }
   else
      /* If 'BEETLE_OPT(renderer_software_fb)' option is not found, then
       * we are running in software mode */
      has_software_fb = true;

   tt_log_startup("rsx_vulkan_refresh_variables: has_software_fb=%d\n",
         (int)has_software_fb);

   unsigned old_scaling = scaling;
   unsigned old_msaa = msaa;
   bool old_super_sampling = super_sampling;
   bool old_show_vram = show_vram;
   int old_crop_overscan = vulkan_crop_overscan;
   unsigned old_image_crop = image_crop;
   bool old_widescreen_hack = widescreen_hack;
   unsigned old_widescreen_hack_aspect_ratio_setting = widescreen_hack_aspect_ratio_setting;
   bool visible_scanlines_changed = false;

   var.key = BEETLE_OPT(internal_resolution);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      /* Same limitations as libretro.cpp */
      scaling = var.value[0] - '0';
      if (var.value[1] != 'x')
      {
         scaling  = (var.value[0] - '0') * 10;
         scaling += var.value[1] - '0';
      }
   }

   var.key = BEETLE_OPT(scaled_uv_offset);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         scaled_uv_offset = true;
      else
         scaled_uv_offset = false;
   }

   var.key = BEETLE_OPT(filter_exclude_sprite);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "all"))
         filter_exclude_sprites = 2;
      else if (!strcmp(var.value, "opaque"))
         filter_exclude_sprites = 1;
      else
         filter_exclude_sprites = 0;
   }

   var.key = BEETLE_OPT(filter_exclude_2d_polygon);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "all"))
         filter_exclude_2d_polygons = 2;
      else if (!strcmp(var.value, "opaque"))
         filter_exclude_2d_polygons = 1;
      else
         filter_exclude_2d_polygons = 0;
   }

   var.key = BEETLE_OPT(adaptive_smoothing);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         adaptive_smoothing = true;
      else
         adaptive_smoothing = false;
   }

   var.key = BEETLE_OPT(super_sampling);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         super_sampling = true;
      else
         super_sampling = false;
   }

   var.key = BEETLE_OPT(msaa);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      msaa = strtoul(var.value, nullptr, 0);
   }

   var.key = BEETLE_OPT(mdec_yuv);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         mdec_yuv = true;
      else
         mdec_yuv = false;
   }

   var.key = BEETLE_OPT(dither_mode);
   dither_mode = DITHER_NATIVE;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "internal resolution"))
         dither_mode = DITHER_UPSCALED;
      else if (!strcmp(var.value, "disabled"))
         dither_mode = DITHER_OFF;
   }

   var.key = BEETLE_OPT(crop_overscan);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         vulkan_crop_overscan = 0;
      else if (strcmp(var.value, "static") == 0)
         vulkan_crop_overscan = 1;
      else if (strcmp(var.value, "smart") == 0)
         vulkan_crop_overscan = 2;
   }

   var.key = BEETLE_OPT(image_offset_cycles);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      image_offset_cycles = atoi(var.value);
   }
   
   var.key = BEETLE_OPT(image_crop);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         image_crop = 0;
      else
         image_crop = atoi(var.value);
   }

   var.key = BEETLE_OPT(initial_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_initial_scanline = atoi(var.value);
      if (new_initial_scanline != initial_scanline)
      {
         initial_scanline = new_initial_scanline;
         visible_scanlines_changed = true;
      }
   }

   var.key = BEETLE_OPT(last_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_last_scanline = atoi(var.value);
      if (new_last_scanline != last_scanline)
      {
         last_scanline = new_last_scanline;
         visible_scanlines_changed = true;
      }
   }

   var.key = BEETLE_OPT(initial_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_initial_scanline_pal = atoi(var.value);
      if (new_initial_scanline_pal != initial_scanline_pal)
      {
         initial_scanline_pal = new_initial_scanline_pal;
         visible_scanlines_changed = true;
      }
   }

   var.key = BEETLE_OPT(last_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_last_scanline_pal = atoi(var.value);
      if (new_last_scanline_pal != last_scanline_pal)
      {
         last_scanline_pal = new_last_scanline_pal;
         visible_scanlines_changed = true;
      }
   }

   var.key = BEETLE_OPT(widescreen_hack);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         widescreen_hack = true;
      else
         widescreen_hack = false;
   }

   var.key = BEETLE_OPT(widescreen_hack_aspect_ratio);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "16:10"))
         widescreen_hack_aspect_ratio_setting = 0;
      else if (!strcmp(var.value, "16:9"))
         widescreen_hack_aspect_ratio_setting = 1;
      else if (!strcmp(var.value, "18:9"))
         widescreen_hack_aspect_ratio_setting = 2;
      else if (!strcmp(var.value, "19:9"))
         widescreen_hack_aspect_ratio_setting = 3;
      else if (!strcmp(var.value, "20:9"))
         widescreen_hack_aspect_ratio_setting = 4;
      else if (!strcmp(var.value, "21:9"))
         widescreen_hack_aspect_ratio_setting = 5;
      else if (!strcmp(var.value, "32:9"))
         widescreen_hack_aspect_ratio_setting = 6;
   }

   var.key = BEETLE_OPT(track_textures);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         track_textures = true;
      else
         track_textures = false;
   }

   var.key = BEETLE_OPT(dump_textures);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         dump_textures = true;
      else
         dump_textures = false;
   }

   var.key = BEETLE_OPT(replace_textures);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         replace_textures = true;
      else
         replace_textures = false;
   }

   struct retro_core_option_display option_display;
   option_display.visible = track_textures;

   option_display.key = BEETLE_OPT(dump_textures);
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = BEETLE_OPT(replace_textures);
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   var.key = BEETLE_OPT(frame_duping);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
      {
         bool frontend_can_dupe = false;
         if (environ_cb(RETRO_ENVIRONMENT_GET_CAN_DUPE, &frontend_can_dupe))
         {
            frame_duping_enabled = frontend_can_dupe;
            if (!frontend_can_dupe)
               log_cb(RETRO_LOG_INFO, "Frontend does not support frame duping. Frame duping will be disabled.\n");
         }
      }
      else
         frame_duping_enabled = false;
   }

   var.key = BEETLE_OPT(display_vram);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         show_vram = true;
      else
         show_vram = false;
   }

   // Clean this up. Possible to categorize by order of severity, e.g. geometry dirty flag vs full system_av dirty flag
   if ((old_scaling != scaling ||
        old_super_sampling != super_sampling ||
        old_msaa != msaa ||
        old_show_vram != show_vram ||
        old_crop_overscan != vulkan_crop_overscan ||
        old_image_crop != image_crop ||
        old_widescreen_hack != widescreen_hack ||
        old_widescreen_hack_aspect_ratio_setting != widescreen_hack_aspect_ratio_setting ||
        visible_scanlines_changed)
       && renderer)
   {
      // Potential bad behavior from calling rsx_vulkan_get_system_av_info() from inside
      // rsx_vulkan_refresh_variables() since both functions call each other...
      retro_system_av_info info;
      rsx_vulkan_get_system_av_info(&info);

      if (!environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info))
      {
         // Failed to change scale, just keep using the old one.
         scaling = old_scaling;
      }
   }
}

static void ensure_sync_index_resources(void)
{
   unsigned mask = vulkan->get_sync_index_mask(vulkan->handle);
   unsigned num_frames = 0;
   for (unsigned i = 0; i < 32; i++)
      if (mask & (1u << i))
         num_frames = i + 1;

   if (num_frames != swapchain_images.size())
   {
      swapchain_images.resize(num_frames);
      scanout_handles.resize(num_frames);
   }
}

void rsx_vulkan_prepare_frame(void)
{
   if (device == nullptr)
   {
      rsx_type = RSX_SOFTWARE;
      return;
   }

   inside_frame = true;
   device->flush_frame();
   vulkan->wait_sync_index(vulkan->handle);
   ensure_sync_index_resources();
   unsigned index = vulkan->get_sync_index(vulkan->handle);
   device->next_frame_context();

   renderer->set_scaled_uv_offset(scaled_uv_offset);
   renderer->set_filter_mode(static_cast<Renderer::FilterMode>(filter_mode));
   renderer->set_sprite_filter_exclude(static_cast<Renderer::FilterExclude>(filter_exclude_sprites));
   renderer->set_polygon_2d_filter_exclude(static_cast<Renderer::FilterExclude>(filter_exclude_2d_polygons));
}

static Renderer::ScanoutMode get_scanout_mode(bool bpp24)
{
   if (bpp24)
      return Renderer::ScanoutMode::BGR24;
   else if (dither_mode != DITHER_OFF)
      return Renderer::ScanoutMode::ABGR1555_Dither;
   else
      return Renderer::ScanoutMode::ABGR1555_555;
}

void rsx_vulkan_finalize_frame(const void *fb, unsigned width,
                               unsigned height, unsigned pitch)
{
   if (device == nullptr)
      return;

   tt_log("vk finalize_frame display=%ux%u\n",
         (unsigned)width, (unsigned)height);
   tt_frame_advance();

   if (frame_duping_enabled && !GPU_get_display_change_count())
   {
      /* Any visual core option changes will be deferred to next non-duped frame */

      //printf("No PSX GPU display update; duping frame\n");
      renderer->flush();
      video_refresh_cb(NULL, prev_frame_width, prev_frame_height, 0);

      inside_frame = false;
      return;
   }

   renderer->set_track_textures(track_textures);
   renderer->set_dump_textures(dump_textures);
   renderer->set_replace_textures(replace_textures);
   renderer->set_adaptive_smoothing(adaptive_smoothing);
   renderer->set_dither_native_resolution(dither_mode == DITHER_NATIVE);
   renderer->set_horizontal_overscan_cropping(vulkan_crop_overscan);
   renderer->set_horizontal_offset_cycles(image_offset_cycles);
   renderer->set_visible_scanlines(initial_scanline, last_scanline, initial_scanline_pal, last_scanline_pal);
   renderer->set_horizontal_additional_cropping(image_crop);

   renderer->set_display_filter(super_sampling ? Renderer::ScanoutFilter::SSAA : Renderer::ScanoutFilter::None);
   if (renderer->get_scanout_mode() == Renderer::ScanoutMode::BGR24)
      renderer->set_mdec_filter(mdec_yuv ? Renderer::ScanoutFilter::MDEC_YUV : Renderer::ScanoutFilter::None);
   else
      renderer->set_mdec_filter(Renderer::ScanoutFilter::None);

   auto scanout = show_vram ? renderer->scanout_vram_to_texture() : renderer->scanout_to_texture();
   unsigned index = vulkan->get_sync_index(vulkan->handle);

   retro_vulkan_image *image                          = &swapchain_images[index];

   image->create_info.sType                           = 
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   image->create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
   image->create_info.format                          = scanout->get_format();
   image->create_info.subresourceRange.baseMipLevel   = 0;
   image->create_info.subresourceRange.baseArrayLayer = 0;
   image->create_info.subresourceRange.levelCount     = 1;
   image->create_info.subresourceRange.layerCount     = 1;
   image->create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
   image->create_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
   image->create_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
   image->create_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
   image->create_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
   image->create_info.image                           = scanout->get_image();
   image->image_layout                                = 
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   image->image_view                                  = 
      scanout->get_view().get_view();

   vulkan->set_image(vulkan->handle, image, 0,
         nullptr, VK_QUEUE_FAMILY_IGNORED);
   renderer->flush();

   scanout_handles[index] = scanout;
   video_refresh_cb(RETRO_HW_FRAME_BUFFER_VALID, scanout->get_width(), scanout->get_height(), 0);
   inside_frame = false;

   prev_frame_width = scanout->get_width();
   prev_frame_height = scanout ->get_height();
}

/* Draw commands */

void rsx_vulkan_set_tex_window(uint8_t tww, uint8_t twh,
                               uint8_t twx, uint8_t twy)
{
   uint8_t tex_x_mask = ~(tww << 3);
   uint8_t tex_y_mask = ~(twh << 3);
   uint8_t tex_x_or   = (twx & tww) << 3;
   uint8_t tex_y_or   = (twy & twh) << 3;

   if (renderer)
   {
      renderer->set_texture_window({ tex_x_mask, tex_y_mask, tex_x_or, tex_y_or });
      tt_log("vk set_tex_window tww=%u twh=%u twx=%u twy=%u\n",
            (unsigned)tww, (unsigned)twh, (unsigned)twx, (unsigned)twy);
   }
   else
      rsx_defer_push_set_tex_window(&defer, tww, twh, twx, twy);
}

void rsx_vulkan_set_draw_offset(int16_t x, int16_t y)
{
   if (renderer)
      renderer->set_draw_offset(x, y);
   else
      rsx_defer_push_set_draw_offset(&defer, x, y);
}

void rsx_vulkan_set_draw_area(uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1)
{
   int width  = x1 - x0 + 1;
   int height = y1 - y0 + 1;
   if (width  < 0) width  = 0;
   if (height < 0) height = 0;

   {
      int w_max = int(FB_WIDTH  - x0);
      int h_max = int(FB_HEIGHT - y0);
      if (width  > w_max) width  = w_max;
      if (height > h_max) height = h_max;
   }

   if (renderer)
   {
      renderer->set_draw_rect({ x0, y0, unsigned(width), unsigned(height) });
      tt_log("vk set_draw_area top_left=(%u,%u) bot_right_inclusive=(%u,%u)\n",
            (unsigned)x0, (unsigned)y0, (unsigned)x1, (unsigned)y1);
   }
   else
      /* Defer the raw inputs (x0,y0,x1,y1); the dispatcher re-enters
       * this entry point which redoes the width/height clamp on top of
       * a live FB_WIDTH/FB_HEIGHT. Functionally identical to capturing
       * the post-clamp values, just keeps the clamp in one place. */
      rsx_defer_push_set_draw_area(&defer, x0, y0, x1, y1);
}

void rsx_vulkan_set_vram_framebuffer_coords(uint32_t xstart, uint32_t ystart)
{
   if (renderer)
      renderer->set_vram_framebuffer_coords(xstart, ystart);
   else
      rsx_defer_push_set_vram_framebuffer_coords(&defer, xstart, ystart);
}

void rsx_vulkan_set_horizontal_display_range(uint16_t x1, uint16_t x2)
{
   if (renderer)
      renderer->set_horizontal_display_range(x1, x2);
   else
      rsx_defer_push_set_horizontal_display_range(&defer, x1, x2);
}

void rsx_vulkan_set_vertical_display_range(uint16_t y1, uint16_t y2)
{
   if (renderer)
      renderer->set_vertical_display_range(y1, y2);
   else
      rsx_defer_push_set_vertical_display_range(&defer, y1, y2);
}

void rsx_vulkan_set_display_mode(bool depth_24bpp,
                                 bool is_pal,
                                 bool is_480i,
                                 int width_mode)
{
   if (renderer)
      renderer->set_display_mode(get_scanout_mode(depth_24bpp), is_pal,
                                 is_480i, static_cast<Renderer::WidthMode>(width_mode));
   else
      rsx_defer_push_set_display_mode(&defer, depth_24bpp, is_pal,
                                      is_480i, width_mode);
}

void rsx_vulkan_push_triangle(
      float p0x, float p0y, float p0w,
      float p1x, float p1y, float p1w,
      float p2x, float p2y, float p2w,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint16_t t0x, uint16_t t0y,
      uint16_t t1x, uint16_t t1y,
      uint16_t t2x, uint16_t t2y,
      uint16_t min_u, uint16_t min_v,
      uint16_t max_u, uint16_t max_v,
      uint16_t texpage_x, uint16_t texpage_y,
      uint16_t clut_x, uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_texture_color_modulate(texture_blend_mode == 2);
   renderer->set_palette_offset(clut_x, clut_y);
   renderer->set_texture_offset(texpage_x, texpage_y);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->set_UV_limits(min_u, min_v, max_u, max_v);
   if (texture_blend_mode != 0)
   {
      switch (depth_shift)
      {
         default:
         case 0:
            renderer->set_texture_mode(TextureMode::ABGR1555);
            break;
         case 1:
            renderer->set_texture_mode(TextureMode::Palette8bpp);
            break;
         case 2:
            renderer->set_texture_mode(TextureMode::Palette4bpp);
            break;
      }
   }
   else
      renderer->set_texture_mode(TextureMode::None);

   switch (blend_mode)
   {
      default:
         renderer->set_semi_transparent(SemiTransparentMode::None);
         break;

      case 0:
         renderer->set_semi_transparent(SemiTransparentMode::Average);
         break;
      case 1:
         renderer->set_semi_transparent(SemiTransparentMode::Add);
         break;
      case 2:
         renderer->set_semi_transparent(SemiTransparentMode::Sub);
         break;
      case 3:
         renderer->set_semi_transparent(SemiTransparentMode::AddQuarter);
         break;
   }

   renderer->set_primitive_type(PrimitiveType::Polygon);

   Vertex vertices[3] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
   };

   renderer->draw_triangle(vertices);
}

void rsx_vulkan_push_quad(
      float p0x, float p0y, float p0w,
      float p1x, float p1y, float p1w,
      float p2x, float p2y, float p2w,
      float p3x, float p3y, float p3w,
      uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3,
      uint16_t t0x, uint16_t t0y, 
      uint16_t t1x, uint16_t t1y,
      uint16_t t2x, uint16_t t2y,
      uint16_t t3x, uint16_t t3y,
      uint16_t min_u, uint16_t min_v,
      uint16_t max_u, uint16_t max_v,
      uint16_t texpage_x, uint16_t texpage_y,
      uint16_t clut_x, uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask,
      bool is_sprite, bool may_be_2d)
{
   if (!renderer)
      return;

   renderer->set_texture_color_modulate(texture_blend_mode == 2);
   renderer->set_palette_offset(clut_x, clut_y);
   renderer->set_texture_offset(texpage_x, texpage_y);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->set_UV_limits(min_u, min_v, max_u, max_v);
   if (texture_blend_mode != 0)
   {
      switch (depth_shift)
      {
         default:
         case 0:
            renderer->set_texture_mode(TextureMode::ABGR1555);
            break;
         case 1:
            renderer->set_texture_mode(TextureMode::Palette8bpp);
            break;
         case 2:
            renderer->set_texture_mode(TextureMode::Palette4bpp);
            break;
      }
   }
   else
      renderer->set_texture_mode(TextureMode::None);

   switch (blend_mode)
   {
      default:
         renderer->set_semi_transparent(SemiTransparentMode::None);
         break;

      case 0:
         renderer->set_semi_transparent(SemiTransparentMode::Average);
         break;
      case 1:
         renderer->set_semi_transparent(SemiTransparentMode::Add);
         break;
      case 2:
         renderer->set_semi_transparent(SemiTransparentMode::Sub);
         break;
      case 3:
         renderer->set_semi_transparent(SemiTransparentMode::AddQuarter);
         break;
   }

   if (is_sprite)
      renderer->set_primitive_type(PrimitiveType::Sprite);
   else if (may_be_2d)
      renderer->set_primitive_type(PrimitiveType::May_Be_2D_Polygon);
   else
      renderer->set_primitive_type(PrimitiveType::Polygon);

   Vertex vertices[4] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
      { p3x, p3y, p3w, c3, t3x, t3y },
   };

   renderer->draw_quad(vertices);
}

void rsx_vulkan_push_line(
      int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0,
      uint32_t c1,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_texture_mode(TextureMode::None);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   switch (blend_mode)
   {
      default:
         renderer->set_semi_transparent(SemiTransparentMode::None);
         break;

      case 0:
         renderer->set_semi_transparent(SemiTransparentMode::Average);
         break;
      case 1:
         renderer->set_semi_transparent(SemiTransparentMode::Add);
         break;
      case 2:
         renderer->set_semi_transparent(SemiTransparentMode::Sub);
         break;
      case 3:
         renderer->set_semi_transparent(SemiTransparentMode::AddQuarter);
         break;
   }

   Vertex vertices[2] = {
      { float(p0x), float(p0y), 1.0f, c0, 0, 0 },
      { float(p1x), float(p1y), 1.0f, c1, 0, 0 },
   };
   renderer->set_texture_color_modulate(false);
   renderer->draw_line(vertices);
}

void rsx_vulkan_load_image(
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram,
      bool mask_test, bool set_mask)
{
   if (!renderer)
   {
      /* Pre-context_reset uploads (e.g. savestate-load arriving before
       * the Vulkan context is up, or game boot pushing VRAM ahead of
       * the frontend's context_reset). The captured `vram` pointer
       * aliases GPU.vram, which lives for the whole core lifetime, so
       * holding it across the deferred-replay window is safe. */
      rsx_defer_push_load_image(&defer, x, y, w, h, vram, mask_test, set_mask);
      return;
   }

   tt_log("vk load_image rect=(%u,%u %ux%u) mask_test=%d set_mask=%d\n",
         (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h,
         (int)mask_test, (int)set_mask);

   renderer->notify_texture_upload(PSX::Rect { x, y, w, h }, vram);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   auto handle   = renderer->copy_cpu_to_vram({ x, y, w, h });
   uint16_t *tmp = renderer->begin_copy(handle);

   /* The row loop has two independent invariants: x-wrap (dual_copy)
    * and y-wrap. Both are determined entirely by (x, y, w, h) before
    * the loop runs. Hoisting them out lets the inner loops degenerate
    * to a single straight memcpy in the common no-wrap case and lets
    * the compiler/CPU prefetcher see the full source-pointer stride.
    *
    * The (y + off_y) & (FB_HEIGHT - 1) mask in the original code was
    * a no-op for every iteration when y + h <= FB_HEIGHT (the dominant
    * case for upload rects), but the compiler can't prove that without
    * splitting the loop.
    */
   const bool dual_copy = x + w > FB_WIDTH;
   const bool y_wrap    = y + h > FB_HEIGHT;

   if (dual_copy)
   {
      const unsigned first  = FB_WIDTH - x;
      const unsigned second = w - first;
      if (y_wrap)
      {
         for (unsigned off_y = 0; off_y < h; off_y++)
         {
            const uint16_t *row = vram + ((y + off_y) & (FB_HEIGHT - 1)) * FB_WIDTH;
            memcpy(tmp + off_y * w,         row + x, first  * sizeof(uint16_t));
            memcpy(tmp + off_y * w + first, row,     second * sizeof(uint16_t));
         }
      }
      else
      {
         for (unsigned off_y = 0; off_y < h; off_y++)
         {
            const uint16_t *row = vram + (y + off_y) * FB_WIDTH;
            memcpy(tmp + off_y * w,         row + x, first  * sizeof(uint16_t));
            memcpy(tmp + off_y * w + first, row,     second * sizeof(uint16_t));
         }
      }
   }
   else
   {
      if (y_wrap)
      {
         for (unsigned off_y = 0; off_y < h; off_y++)
            memcpy(tmp + off_y * w,
                  vram + ((y + off_y) & (FB_HEIGHT - 1)) * FB_WIDTH + x,
                  w * sizeof(uint16_t));
      }
      else
      {
         for (unsigned off_y = 0; off_y < h; off_y++)
            memcpy(tmp + off_y * w,
                  vram + (y + off_y) * FB_WIDTH + x,
                  w * sizeof(uint16_t));
      }
   }
   renderer->end_copy(handle);

   // This is called on state loading. 
   if (!inside_frame)
      renderer->flush();
}

bool rsx_vulkan_read_vram(uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h,
                          uint16_t *vram)
{
   if (!renderer)
      return false;

   tt_log("vk read_vram rect=(%u,%u %ux%u)\n",
         (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h);

   renderer->copy_vram_to_cpu_synchronous({ x, y, w, h }, vram);
   return true;
}

void rsx_vulkan_fill_rect(uint32_t color,
                          uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h)
{
   if (renderer)
   {
      tt_log("vk fill_rect rect=(%u,%u %ux%u) color=0x%06x\n",
            (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h,
            (unsigned)(color & 0xFFFFFFu));
      renderer->clear_rect({ x, y, w, h }, color);
   }
}

void rsx_vulkan_copy_rect(uint16_t src_x, uint16_t src_y,
                          uint16_t dst_x, uint16_t dst_y,
                          uint16_t w, uint16_t h, 
                          bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   tt_log("vk copy_rect src=(%u,%u) dst=(%u,%u) %ux%u mask_test=%d set_mask=%d\n",
         (unsigned)src_x, (unsigned)src_y,
         (unsigned)dst_x, (unsigned)dst_y,
         (unsigned)w, (unsigned)h,
         (int)mask_test, (int)set_mask);

   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->blit_vram({ dst_x, dst_y, w, h }, { src_x, src_y, w, h });
}

void rsx_vulkan_toggle_display(bool status)
{
   if (renderer)
      renderer->toggle_display(status == 0);
   else
      rsx_defer_push_toggle_display(&defer, status);
}

bool rsx_vulkan_has_software_renderer(void)
{
   return has_software_fb;
}
