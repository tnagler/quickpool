#pragma once
#include <atomic>
#include <functional>
#include <memory>

namespace quickpool {

namespace memory {

// forward declarations
template<typename T>
struct Block;

template<typename T>
struct Mempool;

// Storage slot.
template<typename T>
struct Slot
{
    char storage[sizeof(T)];
    Block<T>* mother_block;

    void operator()()
    {
        reinterpret_cast<T*>(&storage)->operator()();
        mother_block->free_one();
    }
};

// Block of task slots.
template<typename T>
struct Block
{
    std::atomic_size_t idx{ 0 };
    std::atomic_size_t num_freed{ 0 };
    Block<T>* next{ nullptr };
    Block<T>* prev{ nullptr };
    const size_t size;

    std::unique_ptr<Slot<T>[]> slots;
    Mempool<T>* mother_pool;

    Block(size_t size = 1000)
      : size{ size }
      , slots{ std::unique_ptr<Slot<T>[]>(new Slot<T>[size]) }
    {
        for (size_t i = 0; i < size; ++i) {
            slots[i].mother_block = this;
        }
    }

    Slot<T>* get_slot() { return ++idx <= size ? &slots[idx - 1] : nullptr; }

    void free_one()
    {
        if (++num_freed == size)
            this->reset();
    }

    void reset()
    {
        num_freed = 0;
        idx = 0;
    }
};

template<typename T>
struct Mempool
{
    Block<T>* head;
    Block<T>* tail;
    const size_t block_size;

    Mempool(size_t block_size = 1000)
      : head{ new Block<T>(block_size) }
      , tail{ head }
      , block_size{ block_size }
    {}

    template<typename... Args>
    Slot<T>* allocate(Args&&... args)
    {
        Slot<T>* slot = get_slot();
        new (&slot->storage) T(std::forward<T>(args)...);
        return slot;
    }

    Slot<T>* get_slot()
    {
        // try to get memory slot in current block.
        if (auto slot = head->get_slot())
            return slot;

        // see if there's a free block ahead
        if (head->next != nullptr) {
            head = head->next;
            return head->get_slot();
        }

        // see if there are free'd blocks to collect
        auto old_tail = tail;
        while (reinterpret_cast<std::atomic_size_t*>(tail) == 0) {
            // cast b/c pointer offset tail->next might be wrong
            tail = tail->next;
        }
        if (tail != old_tail) {
            tail->prev->next = nullptr; // detach range [old_tail, tail)
            tail->prev = nullptr;       // ...
            this->set_head(old_tail);   // move to head
            return head->get_slot();
        }

        // create a new block and put and end of list.
        this->set_head(new Block<T>(block_size));
        return head->get_slot();
    }

    void set_head(Block<T>* block)
    {
        block->prev = head;
        head->next = block;
        head = block;
    }

    void reset()
    {
        auto block = head;
        do {
            block->reset();
            block = block->next;
        } while (block != nullptr);
        head = tail;
    }

    ~Mempool()
    {
        while (tail->next) {
            tail = tail->next;
            delete tail->prev;
        }
        delete tail;
    }
};

} // end namespace memory

} // end namespace quickpool
