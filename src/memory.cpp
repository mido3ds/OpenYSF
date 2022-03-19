#include "containers.hpp"

namespace memory {
	struct TmpAllocator_ : public std::pmr::memory_resource {
		std::unique_ptr<Arena> arena_;

		inline TmpAllocator_() { reset(); }

		inline void reset() {
			arena_ = std::make_unique<Arena>();
		}

		virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override {
			return arena_->allocate(bytes, alignment);
		}

		virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
			arena_->deallocate(p, bytes, alignment);
		}

		virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
			return this == &other;
		}
	};

	thread_local TmpAllocator_ tmp_allocator_;

	Allocator* tmp() {
		return &tmp_allocator_;
	}

	void reset_tmp() {
		tmp_allocator_.reset();
	}
}
