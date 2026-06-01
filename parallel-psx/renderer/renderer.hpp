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

	inline bool compare_key(Hash masked, Hash hash) const
	{
		return get_key_for_index(masked) == hash;
	}

	inline Hash get_hash(const T *value) const
	{
		return static_cast<const IntrusiveHashMapEnabled<T> *>(value)->get_hash();
	}

	inline Hash get_key_for_index(Hash masked) const
	{
		return get_hash(values[masked]);
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
	uint32_t get_levels() const;
	uint32_t get_layers() const;
	uint32_t get_block_stride() const;
	uint32_t get_block_dim_x() const;
	uint32_t get_block_dim_y() const;
	VkImageType get_image_type() const;
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

/* Below this point: helpers that depend on the C++ TextureFormatLayout class
 * (texture_format.hpp). These cannot be lowered to C without first detangling
 * the layout machinery and would need a separate C-friendly accessor. */

static inline void format_num_blocks(VkFormat format, uint32_t *width, uint32_t *height)
{
	uint32_t align_width, align_height;
	Vulkan::TextureFormatLayout::format_block_dim(format, align_width, align_height);
	*width = (*width + align_width - 1) / align_width;
	*height = (*height + align_height - 1) / align_height;
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

	inline bool allocation_is_global() const
	{
		return !alloc && base;
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
	LinkedDeviceHost, // On desktop, directly mapped VRAM over PCI.
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

	static ImageCreateInfo
	immutable_3d_image(unsigned width, unsigned height, unsigned depth, VkFormat format, bool mipmapped = false)
	{
		ImageCreateInfo info = immutable_2d_image(width, height, format, mipmapped);
		info.depth = depth;
		info.type = VK_IMAGE_TYPE_3D;
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
	AttachmentAllocator(Device *device, bool transient)
		: device(device), transient(transient)
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
	bool transient;
};

class TransientAttachmentAllocator : public AttachmentAllocator
{
public:
	TransientAttachmentAllocator(Device *device)
		: AttachmentAllocator(device, true)
	{
	}
};

class PhysicalAttachmentAllocator : public AttachmentAllocator
{
public:
	PhysicalAttachmentAllocator(Device *device)
		: AttachmentAllocator(device, false)
	{
	}
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

struct IndexState
{
	VkBuffer buffer;
	VkDeviceSize offset;
	VkIndexType index_type;
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
		AsyncTransfer,
		Count
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
	void set_index_buffer(const Buffer &buffer, VkDeviceSize offset, VkIndexType index_type);

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
	IndexState index = {};
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
	TransientAttachmentAllocator transient_allocator;

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
