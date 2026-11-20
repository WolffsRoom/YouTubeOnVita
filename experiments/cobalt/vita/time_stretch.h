/*
 * WSOLA time stretching for playback speed (0.25x .. 2x) without pitch
 * change. Stereo S16 in/out, 1024-frame Hann frames at a 512-frame output
 * hop (the windows sum to 1). Each hop the analysis position advances by
 * rate * 512 input frames; the frame start is moved within +-128 frames to
 * the point that best continues the previous output (normalised
 * cross-correlation on a mono, 2x-decimated 256-frame span), which avoids
 * the phasing artefacts of plain overlap-add.
 */
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

class WsolaStretcher {
 public:
  static constexpr int kFrame = 1024;   // analysis/synthesis frame
  static constexpr int kHop = 512;      // output frames per hop
  static constexpr int kSearch = 128;   // +- search range (input frames)
  static constexpr int kCorr = 256;     // similarity span
  static constexpr int kFifo = 16384;   // input frames buffered

  WsolaStretcher() {
    for (int i = 0; i < kFrame; i++)
      window_[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265f * (float)i / (float)kFrame);
    Reset();
  }

  void Reset() {
    fifo_len_ = 0;
    pos_ = (float)kSearch;  // leave room to search backwards
    have_natural_ = false;
    memset(acc_, 0, sizeof(acc_));
  }

  // Input frames the FIFO must hold before the next hop can be produced.
  int NeededForHop() const { return (int)pos_ + kSearch + kFrame; }
  int Buffered() const { return fifo_len_; }
  int Space() const { return kFifo - fifo_len_; }

  void Push(const int16_t* stereo, int frames) {
    if (frames > Space()) frames = Space();
    for (int i = 0; i < frames; i++) {
      left_[fifo_len_ + i] = stereo[2 * i];
      right_[fifo_len_ + i] = stereo[2 * i + 1];
    }
    fifo_len_ += frames;
  }

  // Produces kHop output frames. Returns the input frames this hop advanced
  // (its media duration), or -1 if more input is needed.
  int Hop(float rate, int16_t* out) {
    if (fifo_len_ < NeededForHop()) return -1;
    int nominal = (int)pos_;
    int best = 0;
    if (have_natural_) {
      float best_score = -1e30f;
      for (int d = -kSearch; d <= kSearch; d += 2) {
        float dot = 0.f, energy = 1e-3f;
        int base = nominal + d;
        for (int k = 0; k < kCorr; k += 2) {
          float m = left_[base + k] + right_[base + k];
          dot += m * natural_[k / 2];
          energy += m * m;
        }
        float score = dot / sqrtf(energy);
        if (score > best_score) { best_score = score; best = d; }
      }
    }
    int start = nominal + best;
    for (int i = 0; i < kFrame; i++) {
      acc_[2 * i] += left_[start + i] * window_[i];
      acc_[2 * i + 1] += right_[start + i] * window_[i];
    }
    for (int i = 0; i < kHop; i++) {
      out[2 * i] = Clamp(acc_[2 * i]);
      out[2 * i + 1] = Clamp(acc_[2 * i + 1]);
    }
    memmove(acc_, acc_ + 2 * kHop, sizeof(float) * 2 * (kFrame - kHop));
    memset(acc_ + 2 * (kFrame - kHop), 0, sizeof(float) * 2 * kHop);
    // What naturally follows the chosen frame: the next hop should match it.
    for (int k = 0; k < kCorr; k += 2)
      natural_[k / 2] = left_[start + kHop + k] + right_[start + kHop + k];
    have_natural_ = true;
    float advance = rate * (float)kHop;
    float before = pos_;
    pos_ += advance;
    int consumed = (int)pos_ - (int)before;
    // Drop input that can no longer be reached.
    int drop = (int)pos_ - kSearch;
    if (drop > 4096) {
      memmove(left_, left_ + drop, sizeof(float) * (fifo_len_ - drop));
      memmove(right_, right_ + drop, sizeof(float) * (fifo_len_ - drop));
      fifo_len_ -= drop;
      pos_ -= (float)drop;
    }
    return consumed;
  }

 private:
  static int16_t Clamp(float v) {
    if (v > 32767.f) return 32767;
    if (v < -32768.f) return -32768;
    return (int16_t)v;
  }
  float window_[kFrame];
  float left_[kFifo], right_[kFifo];
  float acc_[2 * kFrame];
  float natural_[kCorr / 2];
  int fifo_len_;
  float pos_;
  bool have_natural_;
};
