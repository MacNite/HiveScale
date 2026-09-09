// audio_ring.h — the staging ring between the BLE notify callback and the
// audio upload loop (src/gatt_audio.cpp).
//
// This is a plain data structure with no Arduino, NimBLE or FreeRTOS
// dependency, for two reasons.
//
//   1. It is the one piece of the audio path that two different execution
//      contexts touch, so it is the one piece worth testing exhaustively.
//      host_test/test_audio_ring.cpp exercises wraparound, the full/empty
//      boundary, overrun accounting and the detached state on a host compiler.
//   2. The rules it enforces are easier to state — and to check — with the
//      locking left to the caller. gatt_audio.cpp wraps every call in one
//      spinlock; nothing here takes a lock of its own.
//
// ── Why "detached" is a state and not just a null check ────────────────────
//
// Notifications arrive on the NimBLE host task. On the classic dual-core ESP32
// that task is pinned to core 0 while the upload loop runs on core 1, so a
// notification can be executing *at the same instant* as the teardown that
// frees this ring's storage. A weak link makes that overlap the normal case
// rather than the rare one: the session ends on a stall or a dropped link with
// the node still transmitting, so packets keep arriving all through cleanup.
//
// The previous version of this ring had no such state. Its notify path indexed
// the buffer unconditionally and wrapped with `% size`, and cleanup freed the
// storage and set the size to zero without holding the lock. A notification
// landing in that window did one of three things, all of them a reset:
//
//   * wrote through the freed pointer, corrupting the heap so that some later,
//     entirely innocent malloc faulted walking its own free list (the exact
//     signature heap_diag.h was written to chase);
//   * wrote through a null pointer — StoreProhibited;
//   * reached `% 0`, which on Xtensa raises IntegerDivideByZero. The ESP32-C6
//     is RISC-V, where the ISA defines remainder-by-zero to return the dividend
//     and no trap is taken — which is a large part of why this crashed a 30-pin
//     ESP32 in an apiary and not a C6 on a bench.
//
// So: detach() under the lock makes every subsequent write a counted, harmless
// drop, and it returns the storage for the caller to free once the notify
// source is gone. There is also no division anywhere in this file — indices
// wrap by a single conditional subtraction — so no size can ever trap.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace audioring {

// Single-producer/single-consumer byte ring. One slot is always left empty so
// that head == tail means empty and never "completely full".
//
// The caller serialises every method; none of them is safe to call concurrently
// with another.
class Ring {
 public:
  // Take ownership of `bytes` of storage for the next session and zero the
  // session counters. Passing a null pointer, or fewer than two bytes, leaves
  // the ring detached — there is no capacity in a one-byte ring once the
  // always-empty slot is accounted for.
  void attach(uint8_t* storage, size_t bytes) {
    const bool usable = storage != nullptr && bytes >= 2;
    m_buf = usable ? storage : nullptr;
    m_size = usable ? bytes : 0;
    m_head = 0;
    m_tail = 0;
    m_accepted = 0;
    m_dropped = 0;
    m_dropEvents = 0;
  }

  // Give the storage back and refuse every later write. The counters survive,
  // so a caller that reads them after teardown still sees what was lost during
  // it. Returns the pointer attach() was given, or null when there was none;
  // calling it twice is harmless and returns null the second time.
  uint8_t* detach() {
    uint8_t* storage = m_buf;
    m_buf = nullptr;
    m_size = 0;
    m_head = 0;
    m_tail = 0;
    return storage;
  }

  bool attached() const { return m_buf != nullptr; }

  // Usable capacity, which is one byte less than the storage handed to
  // attach(). Zero while detached — never SIZE_MAX, which is what the old
  // unsigned "size - used - 1" produced on a torn-down ring and which read to
  // the caller as "room for anything".
  size_t capacity() const { return m_size ? m_size - 1 : 0; }

  size_t used() const {
    if (!m_buf) return 0;
    return (m_head >= m_tail) ? (m_head - m_tail) : (m_size - m_tail + m_head);
  }

  size_t freeSpace() const { return m_buf ? capacity() - used() : 0; }

  // All-or-nothing: a packet that does not fit is dropped whole and counted,
  // never half-written. A partial packet would splice two moments of a hive
  // together at a sample boundary and sound like a click with nothing in the
  // stats to explain it.
  //
  // Returns false when the bytes were dropped — because the sink is behind, or
  // because the ring is detached. Both are counted in dropped().
  bool write(const uint8_t* src, size_t len) {
    if (len == 0) return true;
    if (!m_buf || src == nullptr || len > freeSpace()) {
      m_dropped += (uint32_t)len;
      m_dropEvents++;
      return false;
    }
    const size_t toEnd = m_size - m_head;
    const size_t first = (toEnd < len) ? toEnd : len;
    memcpy(m_buf + m_head, src, first);
    if (first < len) memcpy(m_buf, src + first, len - first);
    m_head = advance(m_head, len);
    m_accepted += (uint32_t)len;
    return true;
  }

  // Drain up to `max` bytes. Returns 0 when empty or detached — normal, not an
  // error.
  size_t read(uint8_t* dst, size_t max) {
    if (!m_buf || dst == nullptr || max == 0) return 0;
    size_t n = used();
    if (n > max) n = max;
    if (n == 0) return 0;
    const size_t toEnd = m_size - m_tail;
    const size_t first = (toEnd < n) ? toEnd : n;
    memcpy(dst, m_buf + m_tail, first);
    if (first < n) memcpy(dst + first, m_buf, n - first);
    m_tail = advance(m_tail, n);
    return n;
  }

  // Bytes this ring accepted since attach(), and bytes it had to drop. The
  // pair is what tells a slow sink from a lossy link in the session report.
  uint32_t accepted() const { return m_accepted; }
  uint32_t dropped() const { return m_dropped; }

  // How many separate writes were refused. This, not dropped(), is the number
  // of audible seams: one refused packet is one discontinuity regardless of how
  // many bytes it carried, and the session report counts seams alongside the
  // sequence gaps observed on the air.
  uint32_t dropEvents() const { return m_dropEvents; }

 private:
  // No modulo: `by` never exceeds capacity() and `index` is always below
  // m_size, so the sum is below 2 * m_size and one subtraction wraps it. That
  // is not a micro-optimisation — it is what keeps a detached ring (size 0)
  // from trapping on Xtensa, and it takes the per-byte divide out of a
  // spinlocked region that runs with interrupts disabled.
  size_t advance(size_t index, size_t by) const {
    index += by;
    return (index >= m_size) ? (index - m_size) : index;
  }

  uint8_t* m_buf = nullptr;
  size_t m_size = 0;
  size_t m_head = 0;
  size_t m_tail = 0;
  uint32_t m_accepted = 0;
  uint32_t m_dropped = 0;
  uint32_t m_dropEvents = 0;
};

}  // namespace audioring
