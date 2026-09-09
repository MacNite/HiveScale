// test_audio_ring.cpp — host-level tests for the audio staging ring.
//
// Exercises the EXACT production structure (audio_ring.h) that src/gatt_audio.cpp
// puts between the NimBLE notify callback and the TLS upload loop. The cases
// that matter are the ones a bench test never reaches: the wrap boundary, the
// full/empty boundary, and above all the DETACHED state, which is what a
// notification arriving during teardown hits. On a dual-core ESP32 that
// notification runs on the other core while cleanup() is freeing the storage,
// and before this state existed it wrote through the freed pointer (heap
// corruption), through null (StoreProhibited) or reached `% 0` (Xtensa
// IntegerDivideByZero) — three different ways to reset a hub mid-recording.
//
// No Arduino toolchain needed. Build & run:
//   g++ -std=gnu++17 -I../include -o test_audio_ring test_audio_ring.cpp && ./test_audio_ring
#include <cstdio>
#include <cstring>
#include <vector>

#include "audio_ring.h"

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond) do { \
    gChecks++; \
    if (!(cond)) { gFailures++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

using audioring::Ring;

// A PCM-ish pattern, so a mis-ordered or spliced read is visible rather than
// merely "the right number of bytes".
static std::vector<uint8_t> pattern(size_t len, uint8_t seed) {
  std::vector<uint8_t> v(len);
  for (size_t i = 0; i < len; i++) v[i] = (uint8_t)(seed + i);
  return v;
}

int main() {
  // ── Detached is the default, and every operation on it is inert ───────────
  {
    Ring r;
    uint8_t out[8] = {0};
    const std::vector<uint8_t> in = pattern(4, 1);

    CHECK(!r.attached());
    CHECK(r.capacity() == 0);
    CHECK(r.used() == 0);
    // The old code computed free as `size - used - 1` in unsigned arithmetic,
    // which on a torn-down ring underflowed to SIZE_MAX and read as "room for
    // anything" — so the notify path went on to index a freed buffer.
    CHECK(r.freeSpace() == 0);
    CHECK(!r.write(in.data(), in.size()));
    CHECK(r.read(out, sizeof(out)) == 0);
    CHECK(r.dropped() == 4);       // dropped audio is counted, never silent
    CHECK(r.dropEvents() == 1);    // ...as one seam, whatever it carried
    CHECK(r.accepted() == 0);
    CHECK(r.detach() == nullptr); // nothing to give back
  }

  // ── attach() reports honest capacity and hands the storage back ──────────
  {
    uint8_t storage[64];
    Ring r;
    r.attach(storage, sizeof(storage));
    CHECK(r.attached());
    CHECK(r.capacity() == 63);  // one slot stays empty to separate full/empty
    CHECK(r.freeSpace() == 63);
    CHECK(r.detach() == storage);
    CHECK(!r.attached());
    CHECK(r.detach() == nullptr);  // idempotent: cleanup may run twice
  }

  // ── Storage too small to hold anything leaves the ring detached ──────────
  {
    uint8_t one[1];
    Ring r;
    r.attach(one, 1);
    CHECK(!r.attached());
    CHECK(r.capacity() == 0);
    r.attach(nullptr, 4096);
    CHECK(!r.attached());
  }

  // ── Round trip preserves byte order ──────────────────────────────────────
  {
    uint8_t storage[64];
    Ring r;
    r.attach(storage, sizeof(storage));
    const std::vector<uint8_t> in = pattern(30, 7);
    CHECK(r.write(in.data(), in.size()));
    CHECK(r.used() == 30);
    CHECK(r.accepted() == 30);

    uint8_t out[64] = {0};
    CHECK(r.read(out, sizeof(out)) == 30);
    CHECK(std::memcmp(out, in.data(), 30) == 0);
    CHECK(r.used() == 0);
    CHECK(r.read(out, sizeof(out)) == 0);
  }

  // ── A short read leaves the remainder, in order ──────────────────────────
  {
    uint8_t storage[64];
    Ring r;
    r.attach(storage, sizeof(storage));
    const std::vector<uint8_t> in = pattern(20, 100);
    CHECK(r.write(in.data(), in.size()));

    uint8_t out[8] = {0};
    CHECK(r.read(out, sizeof(out)) == 8);
    CHECK(std::memcmp(out, in.data(), 8) == 0);
    CHECK(r.used() == 12);

    uint8_t rest[32] = {0};
    CHECK(r.read(rest, sizeof(rest)) == 12);
    CHECK(std::memcmp(rest, in.data() + 8, 12) == 0);
  }

  // ── Wraparound: a write split across the end of the storage ──────────────
  {
    uint8_t storage[16];
    Ring r;
    r.attach(storage, sizeof(storage));  // capacity 15

    const std::vector<uint8_t> first = pattern(12, 0);
    CHECK(r.write(first.data(), first.size()));
    uint8_t drain[12] = {0};
    CHECK(r.read(drain, sizeof(drain)) == 12);  // head=12, tail=12

    // 10 bytes from head 12: 4 to the end of the storage, 6 wrapped.
    const std::vector<uint8_t> second = pattern(10, 200);
    CHECK(r.write(second.data(), second.size()));
    CHECK(r.used() == 10);

    uint8_t out[10] = {0};
    CHECK(r.read(out, sizeof(out)) == 10);
    CHECK(std::memcmp(out, second.data(), 10) == 0);
    CHECK(r.used() == 0);
  }

  // ── The full/empty boundary: capacity bytes fit, one more does not ───────
  {
    uint8_t storage[16];
    Ring r;
    r.attach(storage, sizeof(storage));
    const std::vector<uint8_t> exact = pattern(15, 3);
    CHECK(r.write(exact.data(), exact.size()));
    CHECK(r.used() == 15);
    CHECK(r.freeSpace() == 0);

    const uint8_t one = 0xAA;
    CHECK(!r.write(&one, 1));
    CHECK(r.dropped() == 1);
    CHECK(r.dropEvents() == 1);
    CHECK(r.used() == 15);  // the full ring is unchanged by the refused write

    uint8_t out[15] = {0};
    CHECK(r.read(out, sizeof(out)) == 15);
    CHECK(std::memcmp(out, exact.data(), 15) == 0);
  }

  // ── An over-long packet is dropped WHOLE, never half-written ─────────────
  {
    uint8_t storage[16];
    Ring r;
    r.attach(storage, sizeof(storage));
    const std::vector<uint8_t> keep = pattern(10, 50);
    CHECK(r.write(keep.data(), keep.size()));

    const std::vector<uint8_t> tooBig = pattern(9, 150);  // only 5 bytes free
    CHECK(!r.write(tooBig.data(), tooBig.size()));
    CHECK(r.dropped() == 9);
    CHECK(r.dropEvents() == 1);
    CHECK(r.used() == 10);

    uint8_t out[16] = {0};
    CHECK(r.read(out, sizeof(out)) == 10);
    CHECK(std::memcmp(out, keep.data(), 10) == 0);  // no fragment spliced in
  }

  // ── Sustained traffic wraps many times without drifting ──────────────────
  {
    uint8_t storage[1024];
    Ring r;
    r.attach(storage, sizeof(storage));

    // 240 B is one notification at MTU 247; 500 of them is ~4 s of 16 kHz PCM,
    // which wraps this ring well over a hundred times.
    uint8_t next = 0;
    size_t total = 0;
    for (int i = 0; i < 500; i++) {
      std::vector<uint8_t> pkt(240);
      for (size_t j = 0; j < pkt.size(); j++) pkt[j] = next++;
      CHECK(r.write(pkt.data(), pkt.size()));

      uint8_t out[240] = {0};
      CHECK(r.read(out, sizeof(out)) == 240);
      CHECK(std::memcmp(out, pkt.data(), 240) == 0);
      total += 240;
    }
    CHECK(r.accepted() == total);
    CHECK(r.dropped() == 0);
    CHECK(r.dropEvents() == 0);
    CHECK(r.used() == 0);
  }

  // ── Teardown mid-stream: this is the crash the ring exists to prevent ────
  {
    uint8_t storage[256];
    Ring r;
    r.attach(storage, sizeof(storage));
    const std::vector<uint8_t> live = pattern(100, 11);
    CHECK(r.write(live.data(), live.size()));

    // cleanup() detaches under the lock and frees the storage afterwards.
    CHECK(r.detach() == storage);

    // A notification that was already in flight on the NimBLE host task now
    // lands. It must touch nothing: no write through the pointer the caller is
    // about to free, no wrap arithmetic on a zero size.
    const std::vector<uint8_t> late = pattern(240, 77);
    CHECK(!r.write(late.data(), late.size()));
    CHECK(!r.write(late.data(), late.size()));
    // Two refused notifications are two seams and 480 lost bytes. The session
    // report sums seams with the sequence gaps seen on the air, so this has to
    // stay a packet count and not become a byte count.
    CHECK(r.dropEvents() == 2);
    CHECK(r.dropped() == 480);
    CHECK(r.accepted() == 100);  // the session's own count survives teardown
    CHECK(r.used() == 0);
    CHECK(r.freeSpace() == 0);

    uint8_t out[240] = {0};
    CHECK(r.read(out, sizeof(out)) == 0);

    // The storage is untouched by everything above — the caller may free it.
    CHECK(std::memcmp(storage, live.data(), 100) == 0);
  }

  // ── Re-attaching for the next session starts clean ───────────────────────
  {
    uint8_t storage[64];
    Ring r;
    r.attach(storage, sizeof(storage));
    const uint8_t junk[4] = {1, 2, 3, 4};
    CHECK(r.write(junk, sizeof(junk)));
    const std::vector<uint8_t> tooBig = pattern(200, 0);
    CHECK(!r.write(tooBig.data(), tooBig.size()));
    CHECK(r.dropped() == 200);

    r.attach(storage, sizeof(storage));
    CHECK(r.used() == 0);
    CHECK(r.accepted() == 0);
    CHECK(r.dropped() == 0);
    CHECK(r.dropEvents() == 0);
    CHECK(r.freeSpace() == 63);
  }

  std::printf("%s: %d checks, %d failures\n", gFailures ? "FAILED" : "ok",
              gChecks, gFailures);
  return gFailures ? 1 : 0;
}
