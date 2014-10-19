// Copyright (C) 2014, Panagiotis Christopoulos Charitos.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include "anki/util/Memory.h"
#include "anki/util/Functions.h"
#include "anki/util/Assert.h"
#include "anki/util/NonCopyable.h"
#include "anki/util/Thread.h"
#include "anki/util/Vector.h"
#include "anki/util/Atomic.h"
#include "anki/util/Logger.h"
#include <cstdlib>
#include <cstring>

namespace anki {

//==============================================================================
// Misc                                                                        =
//==============================================================================

#define ANKI_MEM_SIGNATURES ANKI_DEBUG

#if ANKI_MEM_SIGNATURES
using Signature = U32;

static Signature computeSignature(void* ptr)
{
	ANKI_ASSERT(ptr);
	PtrSize sig64 = reinterpret_cast<PtrSize>(ptr);
	Signature sig = sig64;
	sig ^= 0x5bd1e995;
	sig ^= sig << 24;
	return sig;
}
#endif

//==============================================================================
// Other                                                                       =
//==============================================================================

//==============================================================================
void* mallocAligned(PtrSize size, PtrSize alignmentBytes) noexcept
{
#if ANKI_POSIX 
#	if ANKI_OS != ANKI_OS_ANDROID
	void* out;
	U alignment = getAlignedRoundUp(alignmentBytes, sizeof(void*));
	int err = posix_memalign(&out, alignment, size);

	if(!err)
	{
		// Make sure it's aligned
		ANKI_ASSERT(isAligned(alignmentBytes, out));
	}
	else
	{
		ANKI_LOGE("mallocAligned() failed");
	}

	return out;
#	else
	void* out = memalign(
		getAlignedRoundUp(alignmentBytes, sizeof(void*)), size);

	if(out)
	{
		// Make sure it's aligned
		ANKI_ASSERT(isAligned(alignmentBytes, out));
	}
	else
	{
		ANKI_LOGE("memalign() failed");
	}

	return out;
#	endif
#elif ANKI_OS == ANKI_OS_WINDOWS
	void* out = _aligned_malloc(size, alignmentBytes);

	if(out)
	{
		// Make sure it's aligned
		ANKI_ASSERT(isAligned(alignmentBytes, out));
	}
	else
	{
		ANKI_LOGE("_aligned_malloc() failed");
	}

	return out;
#else
#	error "Unimplemented"
#endif
}

//==============================================================================
void freeAligned(void* ptr) noexcept
{
#if ANKI_POSIX
	::free(ptr);
#elif ANKI_OS == ANKI_OS_WINDOWS
	_aligned_free(ptr);
#else
#	error "Unimplemented"
#endif
}

//==============================================================================
void* allocAligned(
	void* userData, void* ptr, PtrSize size, PtrSize alignment) noexcept
{
	(void)userData;
	void* out;

	if(ptr == nullptr)
	{
		// Allocate
		ANKI_ASSERT(size > 0);
		out = mallocAligned(size, alignment);
	}
	else
	{
		// Deallocate
		ANKI_ASSERT(size == 0);
		ANKI_ASSERT(alignment == 0);

		freeAligned(ptr);
		out = nullptr;
	}

	return out;
}

//==============================================================================
// HeapMemoryPool                                                              =
//==============================================================================

//==============================================================================
/// The hidden implementation of HeapMemoryPool
class HeapMemoryPool::Implementation: public NonCopyable
{
public:
	AtomicU32 m_refcount;
	AtomicU32 m_allocationsCount;
	AllocAlignedCallback m_allocCb;
	void* m_allocCbUserData;
#if ANKI_MEM_SIGNATURES
	Signature m_signature;
	static const U32 MAX_ALIGNMENT = 16;
	U32 m_headerSize;
#endif

	~Implementation()
	{
		if(m_allocationsCount != 0)
		{
			ANKI_LOGW("Memory pool destroyed before all memory being released");
		}
	}

	void create(AllocAlignedCallback allocCb, void* allocCbUserData)
	{
		m_refcount = 1;
		m_allocationsCount = 0;
		m_allocCb = allocCb;
		m_allocCbUserData = allocCbUserData;
#if ANKI_MEM_SIGNATURES
		m_signature = computeSignature(this);
		m_headerSize = getAlignedRoundUp(MAX_ALIGNMENT, sizeof(Signature));
#endif
	}

	void* allocate(PtrSize size, PtrSize alignment)
	{
#if ANKI_MEM_SIGNATURES
		ANKI_ASSERT(alignment <= MAX_ALIGNMENT && "Wrong assumption");
		size += m_headerSize;
#endif

		void* mem = m_allocCb(m_allocCbUserData, nullptr, size, alignment);

		if(mem != nullptr)
		{
			++m_allocationsCount;

#if ANKI_MEM_SIGNATURES
			memset(mem, 0, m_headerSize);
			memcpy(mem, &m_signature, sizeof(m_signature));
			U8* memU8 = static_cast<U8*>(mem);
			memU8 += m_headerSize;
			mem = static_cast<void*>(memU8);
#endif
		}
		else
		{
			ANKI_LOGE("Out of memory");
		}

		return mem;
	}

	Bool free(void* ptr)
	{
#if ANKI_MEM_SIGNATURES
		U8* memU8 = static_cast<U8*>(ptr);
		memU8 -= m_headerSize;
		if(memcmp(memU8, &m_signature, sizeof(m_signature)) != 0)
		{
			ANKI_LOGE("Signature missmatch on free");
		}

		ptr = static_cast<void*>(memU8);
#endif
		--m_allocationsCount;
		m_allocCb(m_allocCbUserData, ptr, 0, 0);

		return true;
	}
};

//==============================================================================
HeapMemoryPool& HeapMemoryPool::operator=(const HeapMemoryPool& other)
{
	clear();

	if(other.m_impl)
	{
		m_impl = other.m_impl;
		++m_impl->m_refcount;
	}

	return *this;
}

//==============================================================================
Error HeapMemoryPool::create(
	AllocAlignedCallback allocCb, void* allocCbUserData)
{
	ANKI_ASSERT(allocCb != nullptr);

	Error error = ErrorCode::NONE;
	m_impl = static_cast<Implementation*>(allocCb(allocCbUserData, nullptr, 
		sizeof(Implementation), alignof(Implementation)));

	if(m_impl)
	{
		m_impl->create(allocCb, allocCbUserData);
	}
	else
	{
		ANKI_LOGE("Out of memory");
		error = ErrorCode::OUT_OF_MEMORY;
	}

	return error;
}

//==============================================================================
void HeapMemoryPool::clear()
{
	if(m_impl)
	{
		U32 refcount = --m_impl->m_refcount;

		if(refcount == 0)
		{
			auto allocCb = m_impl->m_allocCb;
			auto ud = m_impl->m_allocCbUserData;
			ANKI_ASSERT(allocCb);

			m_impl->~Implementation();
			allocCb(ud, m_impl, 0, 0);
		}

		m_impl = nullptr;
	}
}

//==============================================================================
void* HeapMemoryPool::allocate(PtrSize size, PtrSize alignment) noexcept
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->allocate(size, alignment);
}

//==============================================================================
Bool HeapMemoryPool::free(void* ptr) noexcept
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->free(ptr);
}

//==============================================================================
U32 HeapMemoryPool::getAllocationsCount() const
{
	ANKI_ASSERT(m_impl != nullptr);

	return m_impl->m_allocationsCount.load();
}

//==============================================================================
// StackMemoryPool                                                             =
//==============================================================================

//==============================================================================
/// The hidden implementation of StackMemoryPool
class StackMemoryPool::Implementation: public NonCopyable
{
public:
	/// The header of each allocation
	class MemoryBlockHeader
	{
	public:
		U8 m_size[sizeof(U32)]; ///< It's U8 to allow whatever alignment
	};

	static_assert(alignof(MemoryBlockHeader) == 1, "Alignment error");
	static_assert(sizeof(MemoryBlockHeader) == sizeof(U32), "Size error");

	/// Refcount
	AtomicU32 m_refcount;

	/// User allocation function
	AllocAlignedCallback m_allocCb;

	/// User allocation function data
	void* m_allocCbUserData;

	/// Alignment of allocations
	PtrSize m_alignmentBytes;

	/// Aligned size of MemoryBlockHeader
	PtrSize m_headerSize;

	/// Pre-allocated memory chunk
	U8* m_memory;

	/// Size of the pre-allocated memory chunk
	PtrSize m_memsize;

	/// Points to the memory and more specifically to the top of the stack
	Atomic<U8*> m_top;

	AtomicU32 m_allocationsCount;

	// Destroy
	~Implementation()
	{
		if(m_memory != nullptr)
		{
#if ANKI_DEBUG
			// Invalidate the memory
			memset(m_memory, 0xCC, m_memsize);
#endif
			m_allocCb(m_allocCbUserData, m_memory, 0, 0);
		}
	}

	// Construct
	Error create(AllocAlignedCallback allocCb, void* allocCbUserData,
		PtrSize size, PtrSize alignmentBytes)
	{
		ANKI_ASSERT(allocCb);
		ANKI_ASSERT(size > 0);
		ANKI_ASSERT(alignmentBytes > 0);

		Error error = ErrorCode::NONE;

		m_refcount = 1;
		m_allocCb = allocCb;
		m_allocCbUserData = allocCbUserData;
		m_alignmentBytes = alignmentBytes;
		m_memsize = getAlignedRoundUp(alignmentBytes, size);
		m_allocationsCount = 0;

		m_memory = (U8*)m_allocCb(
			m_allocCbUserData, nullptr, m_memsize, m_alignmentBytes);

		if(m_memory != nullptr)
		{
#if ANKI_DEBUG
			// Invalidate the memory
			memset(m_memory, 0xCC, m_memsize);
#endif

			// Align allocated memory
			m_top = m_memory;

			// Calc header size
			m_headerSize = 
				getAlignedRoundUp(m_alignmentBytes, sizeof(MemoryBlockHeader));
		}
		else
		{
			ANKI_LOGE("Out of memory");
			error = ErrorCode::OUT_OF_MEMORY;
		}

		return error;
	}

	PtrSize getTotalSize() const
	{
		return m_memsize;
	}

	PtrSize getAllocatedSize() const
	{
		ANKI_ASSERT(m_memory != nullptr);
		return m_top.load() - m_memory;
	}

	const void* getBaseAddress() const
	{
		ANKI_ASSERT(m_memory != nullptr);
		return m_memory;
	}

	/// Allocate
	void* allocate(PtrSize size, PtrSize alignment) noexcept
	{
		ANKI_ASSERT(m_memory != nullptr);
		ANKI_ASSERT(alignment <= m_alignmentBytes);
		(void)alignment;

		size = getAlignedRoundUp(m_alignmentBytes, size + m_headerSize);

		ANKI_ASSERT(size < MAX_U32 && "Too big allocation");

		U8* out = m_top.fetch_add(size);

		if(out + size <= m_memory + m_memsize)
		{
#if ANKI_DEBUG
			// Invalidate the block
			memset(out, 0xCC, size);
#endif

			// Write the block header
			MemoryBlockHeader* header = 
				reinterpret_cast<MemoryBlockHeader*>(out);
			U32 size32 = size;
			memcpy(&header->m_size[0], &size32, sizeof(U32));

			// Set the correct output
			out += m_headerSize;

			// Check alignment
			ANKI_ASSERT(isAligned(m_alignmentBytes, out));

			// Increase count
			++m_allocationsCount;
		}
		else
		{
			// Not always an error
			out = nullptr;
		}

		return out;
	}

	/// Free
	Bool free(void* ptr) noexcept
	{
		// ptr shouldn't be null or not aligned. If not aligned it was not 
		// allocated by this class
		ANKI_ASSERT(ptr != nullptr && isAligned(m_alignmentBytes, ptr));

		// memory is nullptr if moved
		ANKI_ASSERT(m_memory != nullptr);

		// Correct the p
		U8* realptr = (U8*)ptr - m_headerSize;

		// realptr should be inside the pool's preallocated memory
		ANKI_ASSERT(realptr >= m_memory);

		// Get block size
		MemoryBlockHeader* header = (MemoryBlockHeader*)realptr;
		U32 size;
		memcpy(&size, &header->m_size[0], sizeof(U32));

		// Check if the size is within limits
		ANKI_ASSERT(realptr + size <= m_memory + m_memsize);

		// Atomic stuff
		U8* expected = realptr + size;
		U8* desired = realptr;

		// if(top == expected) {
		//     top = desired;
		//     exchange = true;
		// } else {
		//     expected = top;
		//     exchange = false;
		// }
		Bool exchange = m_top.compare_exchange_strong(expected, desired);

		// Decrease count
		--m_allocationsCount;

		return exchange;
	}

	/// Reset
	void reset()
	{
		// memory is nullptr if moved
		ANKI_ASSERT(m_memory != nullptr);

#if ANKI_DEBUG
		// Invalidate the memory
		memset(m_memory, 0xCC, m_memsize);
#endif

		m_top = m_memory;
	}
};

//==============================================================================
StackMemoryPool& StackMemoryPool::operator=(const StackMemoryPool& other)
{
	clear();

	if(other.m_impl)
	{
		m_impl = other.m_impl;
		++m_impl->m_refcount;
	}

	return *this;
}

//==============================================================================
Error StackMemoryPool::create(
	AllocAlignedCallback alloc, void* allocUserData,
	PtrSize size, PtrSize alignmentBytes)
{
	ANKI_ASSERT(m_impl == nullptr);

	Error error = ErrorCode::NONE;
	m_impl = static_cast<Implementation*>(alloc(allocUserData, nullptr, 
		sizeof(Implementation), alignof(Implementation)));
	
	if(m_impl)
	{
		construct(m_impl);
		error = m_impl->create(alloc, allocUserData, size, alignmentBytes);
	}
	else
	{
		ANKI_LOGE("Out of memory");
		error = ErrorCode::OUT_OF_MEMORY;
	}

	return error;
}

//==============================================================================
void StackMemoryPool::clear()
{
	if(m_impl)
	{
		U32 refcount = --m_impl->m_refcount;

		if(refcount == 0)
		{
			auto allocCb = m_impl->m_allocCb;
			auto ud = m_impl->m_allocCbUserData;
			ANKI_ASSERT(allocCb);

			m_impl->~Implementation();
			allocCb(ud, m_impl, 0, 0);
		}

		m_impl = nullptr;
	}
}

//==============================================================================
PtrSize StackMemoryPool::getTotalSize() const
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->getTotalSize();
}

//==============================================================================
PtrSize StackMemoryPool::getAllocatedSize() const
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->getAllocatedSize();
}

//==============================================================================
void* StackMemoryPool::allocate(PtrSize size, PtrSize alignment) noexcept
{
	ANKI_ASSERT(m_impl != nullptr);
	void* mem = m_impl->allocate(size, alignment);

	if (mem == nullptr)
	{
		ANKI_LOGE("Out of memory");
	}

	return mem;
}

//==============================================================================
Bool StackMemoryPool::free(void* ptr) noexcept
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->free(ptr);
}

//==============================================================================
void StackMemoryPool::reset()
{
	ANKI_ASSERT(m_impl != nullptr);
	m_impl->reset();
}

//==============================================================================
U32 StackMemoryPool::getUsersCount() const
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->m_refcount.load();
}

//==============================================================================
StackMemoryPool::Snapshot StackMemoryPool::getShapshot() const
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->m_top.load();
}

//==============================================================================
void StackMemoryPool::resetUsingSnapshot(Snapshot s)
{
	ANKI_ASSERT(m_impl != nullptr);
	ANKI_ASSERT(static_cast<U8*>(s) >= m_impl->m_memory);
	ANKI_ASSERT(static_cast<U8*>(s) < m_impl->m_memory + m_impl->m_memsize);

	m_impl->m_top.store(static_cast<U8*>(s));
}

//==============================================================================
U32 StackMemoryPool::getAllocationsCount() const
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->m_allocationsCount.load();
}

//==============================================================================
// ChainMemoryPool                                                             =
//==============================================================================

//==============================================================================
/// The hidden implementation of ChainMemoryPool
class ChainMemoryPool::Implementation: public NonCopyable
{
public:
	/// A chunk of memory
	class Chunk
	{
	public:
		StackMemoryPool::Implementation m_pool;

		/// Used to identify if the chunk can be deleted
		U32 m_allocationsCount = 0;

		/// Next chunk in the list
		Chunk* m_next = nullptr;
	};

	/// Refcount
	AtomicU32 m_refcount = {1};

	/// User allocation function
	AllocAlignedCallback m_allocCb;

	/// User allocation function data
	void* m_allocCbUserData;

	/// Alignment of allocations
	PtrSize m_alignmentBytes;

	/// The first chunk
	Chunk* m_headChunk = nullptr;

	/// Current chunk to allocate from
	Chunk* m_tailChunk = nullptr;

	/// Fast thread locking
	SpinLock m_lock;

	/// Chunk first chunk size
	PtrSize m_initSize;

	/// Chunk max size
	PtrSize m_maxSize;

	/// Chunk allocation method value
	U32 m_step;

	/// Chunk allocation method
	ChainMemoryPool::ChunkGrowMethod m_method;

	/// Construct
	Implementation(
		AllocAlignedCallback allocCb, 
		void* allocCbUserData,
		PtrSize initialChunkSize,
		PtrSize maxChunkSize,
		ChainMemoryPool::ChunkGrowMethod chunkAllocStepMethod, 
		PtrSize chunkAllocStep, 
		PtrSize alignmentBytes)
	:	m_allocCb(allocCb),
		m_allocCbUserData(allocCbUserData),
		m_alignmentBytes(alignmentBytes), 
		m_initSize(initialChunkSize),
		m_maxSize(maxChunkSize),
		m_step((U32)chunkAllocStep),
		m_method(chunkAllocStepMethod)
	{
		ANKI_ASSERT(m_allocCb);

		// Initial size should be > 0
		ANKI_ASSERT(m_initSize > 0);

		// On fixed step should be 0
		if(m_method == ChainMemoryPool::ChunkGrowMethod::FIXED)
		{
			ANKI_ASSERT(m_step == 0);
		}

		// On fixed initial size is the same as the max
		if(m_method == ChainMemoryPool::ChunkGrowMethod::FIXED)
		{
			ANKI_ASSERT(m_initSize == m_maxSize);
		}

		// On add and mul the max size should be greater than initial
		if(m_method == ChainMemoryPool::ChunkGrowMethod::ADD 
			|| m_method == ChainMemoryPool::ChunkGrowMethod::MULTIPLY)
		{
			ANKI_ASSERT(m_initSize < m_maxSize);
		}
	}

	/// Destroy
	~Implementation()
	{
		Chunk* ch = m_headChunk;
		while(ch)
		{
			Chunk* next = ch->m_next;

			ch->~Chunk();
			m_allocCb(m_allocCbUserData, ch, 0, 0);

			ch = next;
		}
	}

#if 0
	PtrSize computeNewChunkSize(PtrSize allocationSize)
	{
		PtrSize crntMaxSize;
		if(m_method == ChainMemoryPool::ChunkGrowMethod::FIXED)
		{
			crntMaxSize = m_initSize;
		}
		else
		{
			// Get the size of the previous max chunk
			if(m_tailChunk != nullptr)
			{
				// Get the size of previous
				crntMaxSize = m_tailChunk->m_pool.getTotalSize();

				// Increase it
				if(m_method == ChainMemoryPool::ChunkGrowMethod::MULTIPLY)
				{
					crntMaxSize *= m_step;
				}
				else
				{
					ANKI_ASSERT(m_method 
						== ChainMemoryPool::ChunkGrowMethod::ADD);
					crntMaxSize += m_step;
				}
			}
			else
			{
				// No chunks. Choose initial size

				ANKI_ASSERT(m_headChunk == nullptr);
				crntMaxSize = m_initSize;
			}

			ANKI_ASSERT(crntMaxSize > 0);

			// Fix the size
			crntMaxSize = std::min(crntMaxSize, (PtrSize)m_maxSize);
		}

		size = std::max(crntMaxSize, size) + 16;
	}
#endif

	/// Create a new chunk
	Chunk* createNewChunk(PtrSize size) noexcept
	{
		//
		// Calculate preferred size
		//
		
		// Get the size of the next chunk
		PtrSize crntMaxSize;
		if(m_method == ChainMemoryPool::ChunkGrowMethod::FIXED)
		{
			crntMaxSize = m_initSize;
		}
		else
		{
			// Get the size of the previous max chunk
			if(m_tailChunk != nullptr)
			{
				// Get the size of previous
				crntMaxSize = m_tailChunk->m_pool.getTotalSize();

				// Increase it
				if(m_method == ChainMemoryPool::ChunkGrowMethod::MULTIPLY)
				{
					crntMaxSize *= m_step;
				}
				else
				{
					ANKI_ASSERT(m_method 
						== ChainMemoryPool::ChunkGrowMethod::ADD);
					crntMaxSize += m_step;
				}
			}
			else
			{
				// No chunks. Choose initial size

				ANKI_ASSERT(m_headChunk == nullptr);
				crntMaxSize = m_initSize;
			}

			ANKI_ASSERT(crntMaxSize > 0);

			// Fix the size
			crntMaxSize = std::min(crntMaxSize, (PtrSize)m_maxSize);
		}

		size = std::max(crntMaxSize, size) + 16;

		//
		// Create the chunk
		//
		Chunk* chunk = (Chunk*)m_allocCb(
			m_allocCbUserData, nullptr, sizeof(Chunk), alignof(Chunk));

		if(chunk)
		{
			// Construct it
			construct(chunk);
			Error error = chunk->m_pool.create(
				m_allocCb, m_allocCbUserData, size, m_alignmentBytes);

			if(!error)
			{
				// Register it
				if(m_tailChunk)
				{
					m_tailChunk->m_next = chunk;
					m_tailChunk = chunk;
				}
				else
				{
					ANKI_ASSERT(m_headChunk == nullptr);

					m_headChunk = m_tailChunk = chunk;
				}
			}
			else
			{
				destroy(chunk);
				m_allocCb(m_allocCbUserData, chunk, 0, 0);
				chunk = nullptr;
			}
		}
		else
		{
			ANKI_LOGE("Out of memory");
		}
		
		return chunk;
	}

	/// Allocate from chunk
	void* allocateFromChunk(Chunk* ch, PtrSize size, PtrSize alignment) noexcept
	{
		ANKI_ASSERT(ch);
		ANKI_ASSERT(size <= m_maxSize);
		void* mem = ch->m_pool.allocate(size, alignment);

		if(mem)
		{
			++ch->m_allocationsCount;
		}
		else
		{
			// Chunk is full. Need a new one
		}

		return mem;
	}

	/// Allocate memory
	void* allocate(PtrSize size, PtrSize alignment) noexcept
	{
		ANKI_ASSERT(size <= m_maxSize);

		Chunk* ch;
		void* mem = nullptr;

		m_lock.lock();

		// Get chunk
		ch = m_tailChunk;

		// Create new chunk if needed
		if(ch == nullptr 
			|| (mem = allocateFromChunk(ch, size, alignment)) == nullptr)
		{
			// Create new chunk
			ch = createNewChunk(size);

			// Chunk creation failed
			if(ch == nullptr)
			{
				m_lock.unlock();
				return mem;
			}
		}

		if(mem == nullptr)
		{
			mem = allocateFromChunk(ch, size, alignment);
			ANKI_ASSERT(mem != nullptr && "The chunk should have space");
		}

		m_lock.unlock();
		return mem;
	}

	/// Free memory
	Bool free(void* ptr) noexcept
	{
		m_lock.lock();

		// Get the chunk that ptr belongs to
		Chunk* chunk = m_headChunk;
		Chunk* prevChunk = nullptr;
		while(chunk)
		{
			const U8* from = (const U8*)chunk->m_pool.getBaseAddress();
			const U8* to = from + chunk->m_pool.getTotalSize();
			const U8* cptr = (const U8*)ptr;
			if(cptr >= from && cptr < to)
			{
				break;
			}

			prevChunk = chunk;
			chunk = chunk->m_next;
		}

		ANKI_ASSERT(chunk != nullptr 
			&& "Not initialized or ptr is incorrect");

		// Decrease the deallocation refcount and if it's zero delete the chunk
		ANKI_ASSERT(chunk->m_allocationsCount > 0);
		if(--chunk->m_allocationsCount == 0)
		{
			// Chunk is empty. Delete it

			if(prevChunk != nullptr)
			{
				ANKI_ASSERT(m_headChunk != chunk);
				prevChunk->m_next = chunk->m_next;
			}

			if(chunk == m_headChunk)
			{
				ANKI_ASSERT(prevChunk == nullptr);
				m_headChunk = chunk->m_next;
			}

			if(chunk == m_tailChunk)
			{
				m_tailChunk = prevChunk;
			}

			// Finaly delete it
			chunk->~Chunk();
			m_allocCb(m_allocCbUserData, chunk, 0, 0);
		}

		m_lock.unlock();

		return true;
	}

	PtrSize getAllocatedSize() const
	{
		PtrSize sum = 0;
		Chunk* ch = m_headChunk;
		while(ch)
		{
			sum += ch->m_pool.getAllocatedSize();
			ch = ch->m_next;
		}

		return sum;
	}

	PtrSize getChunksCount() const
	{
		PtrSize count = 0;
		Chunk* ch = m_headChunk;
		while(ch)
		{
			++count;
			ch = ch->m_next;
		}

		return count;
	}
};

//==============================================================================
ChainMemoryPool& ChainMemoryPool::operator=(const ChainMemoryPool& other)
{
	clear();

	if(other.m_impl)
	{
		m_impl = other.m_impl;
		++m_impl->m_refcount;
	}

	return *this;
}

//==============================================================================
Error ChainMemoryPool::create(
	AllocAlignedCallback alloc, 
	void* allocUserData,
	PtrSize initialChunkSize,
	PtrSize maxChunkSize,
	ChunkGrowMethod chunkAllocStepMethod, 
	PtrSize chunkAllocStep, 
	PtrSize alignmentBytes)
{
	ANKI_ASSERT(m_impl == nullptr);

	Error error = ErrorCode::NONE;
	m_impl = static_cast<Implementation*>(alloc(allocUserData, nullptr, 
		sizeof(Implementation), alignof(Implementation)));

	if(m_impl)
	{
		construct(
			m_impl,
			alloc, allocUserData,
			initialChunkSize, maxChunkSize, chunkAllocStepMethod, 
			chunkAllocStep,
			alignmentBytes);
	}
	else
	{
		ANKI_LOGE("Out of memory");
		error = ErrorCode::OUT_OF_MEMORY;
	}

	return error;
}

//==============================================================================
void ChainMemoryPool::clear()
{
	if(m_impl)
	{
		U32 refcount = --m_impl->m_refcount;

		if(refcount == 0)
		{
			auto allocCb = m_impl->m_allocCb;
			auto ud = m_impl->m_allocCbUserData;
			ANKI_ASSERT(allocCb);

			m_impl->~Implementation();
			allocCb(ud, m_impl, 0, 0);
		}

		m_impl = nullptr;
	}
}

//==============================================================================
void* ChainMemoryPool::allocate(PtrSize size, PtrSize alignment) noexcept
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->allocate(size, alignment);
}

//==============================================================================
Bool ChainMemoryPool::free(void* ptr) noexcept
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->free(ptr);
}

//==============================================================================
PtrSize ChainMemoryPool::getChunksCount() const
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->getChunksCount();
}

//==============================================================================
PtrSize ChainMemoryPool::getAllocatedSize() const
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->getAllocatedSize();
}

//==============================================================================
U32 ChainMemoryPool::getUsersCount() const
{
	ANKI_ASSERT(m_impl != nullptr);
	return m_impl->m_refcount.load();
}

} // end namespace anki
