#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace msgpack::rpc {

// Bounded lock-free multi-producer, single-consumer queue.
// Producers may call push/emplace concurrently.
// Exactly one consumer may call pop/drain methods.
template <typename T, std::size_t Capacity>
class concurrent_queue {
  static_assert(Capacity != 0, "Capacity must be greater than zero");
  static_assert(std::has_single_bit(Capacity),
                "Capacity must be a power of two");

 public:
  concurrent_queue() noexcept {
    for (std::size_t index = 0; index != Capacity; ++index) {
      slots_[index].sequence.store(index, std::memory_order_relaxed);
    }
  }

  concurrent_queue(const concurrent_queue&) = delete;
  auto operator=(const concurrent_queue&) -> concurrent_queue& = delete;
  concurrent_queue(concurrent_queue&&) = delete;
  auto operator=(concurrent_queue&&) -> concurrent_queue& = delete;

  ~concurrent_queue() { clear(); }

  [[nodiscard]] static constexpr auto capacity() noexcept -> std::size_t {
    return Capacity;
  }

  [[nodiscard]] auto approximate_size() const noexcept -> std::size_t {
    const auto head = enqueue_pos_.load(std::memory_order_relaxed);
    const auto tail = dequeue_pos_.load(std::memory_order_relaxed);
    return head >= tail ? (head - tail) : 0;
  }

  template <typename... Args>
    requires std::constructible_from<T, Args...>
  auto try_emplace(Args&&... args) -> bool {
    auto reservation = reserve_slot();
    if (!reservation) {
      return false;
    }

    auto [slot, position] = *reservation;
    std::construct_at(slot_ptr(*slot), std::forward<Args>(args)...);
    slot->sequence.store(position + 1, std::memory_order_release);
    return true;
  }

  auto try_push(const T& value) -> bool
    requires std::copy_constructible<T>
  {
    return try_emplace(value);
  }

  auto try_push(T&& value) -> bool
    requires std::move_constructible<T>
  {
    return try_emplace(std::move(value));
  }

  [[nodiscard]] auto try_pop() -> std::optional<T> {
    auto position = dequeue_pos_.load(std::memory_order_relaxed);
    auto* slot = &slots_[position & k_mask];
    const auto sequence = slot->sequence.load(std::memory_order_acquire);
    const auto difference = static_cast<std::intptr_t>(sequence) -
                            static_cast<std::intptr_t>(position + 1);
    if (difference < 0) {
      return std::nullopt;
    }

    if (difference > 0) {
      return std::nullopt;
    }

    auto value = std::optional<T>{std::move(*slot_ptr(*slot))};
    std::destroy_at(slot_ptr(*slot));
    slot->sequence.store(position + Capacity, std::memory_order_release);
    dequeue_pos_.store(position + 1, std::memory_order_relaxed);
    return value;
  }

  template <typename Fn>
  auto drain(Fn&& fn, std::size_t limit = static_cast<std::size_t>(-1))
      -> std::size_t {
    std::size_t count = 0;
    while (count != limit) {
      auto value = try_pop();
      if (!value) {
        break;
      }

      fn(std::move(*value));
      ++count;
    }
    return count;
  }

  auto clear() noexcept -> void {
    while (discard_one()) {
    }
  }

 private:
  struct slot {
    std::atomic<std::size_t> sequence{};
    alignas(T) std::byte storage[sizeof(T)]{};
  };

  static constexpr std::size_t k_mask = Capacity - 1;

  [[nodiscard]] static auto slot_ptr(slot& value) noexcept -> T* {
    return std::launder(reinterpret_cast<T*>(value.storage));
  }

  [[nodiscard]] auto reserve_slot() noexcept
      -> std::optional<std::pair<slot*, std::size_t>> {
    auto position = enqueue_pos_.load(std::memory_order_relaxed);

    for (;;) {
      auto* slot = &slots_[position & k_mask];
      const auto sequence = slot->sequence.load(std::memory_order_acquire);
      const auto difference = static_cast<std::intptr_t>(sequence) -
                              static_cast<std::intptr_t>(position);

      if (difference == 0) {
        if (enqueue_pos_.compare_exchange_weak(
                position, position + 1, std::memory_order_relaxed,
                std::memory_order_relaxed)) {
          return std::pair{slot, position};
        }
        continue;
      }

      if (difference < 0) {
        return std::nullopt;
      }

      position = enqueue_pos_.load(std::memory_order_relaxed);
    }
  }

  auto discard_one() noexcept -> bool {
    auto position = dequeue_pos_.load(std::memory_order_relaxed);
    auto* slot = &slots_[position & k_mask];
    const auto sequence = slot->sequence.load(std::memory_order_acquire);
    const auto difference = static_cast<std::intptr_t>(sequence) -
                            static_cast<std::intptr_t>(position + 1);
    if (difference != 0) {
      return false;
    }

    std::destroy_at(slot_ptr(*slot));
    slot->sequence.store(position + Capacity, std::memory_order_release);
    dequeue_pos_.store(position + 1, std::memory_order_relaxed);
    return true;
  }

  slot slots_[Capacity]{};
  alignas(std::hardware_destructive_interference_size)
      std::atomic<std::size_t> enqueue_pos_{0};
  alignas(std::hardware_destructive_interference_size)
      std::atomic<std::size_t> dequeue_pos_{0};
};

}  // namespace msgpack::rpc
