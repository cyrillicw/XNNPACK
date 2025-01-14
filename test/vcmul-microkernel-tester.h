// Copyright 2023 Google LLC
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <limits>
#include <random>
#include <vector>

#include <xnnpack.h>
#include <xnnpack/microfnptr.h>
#include <xnnpack/microparams-init.h>
#include <xnnpack/requantization.h>


class VCMulMicrokernelTester {
 public:
  inline VCMulMicrokernelTester& batch_size(size_t batch_size) {
    assert(batch_size != 0);
    this->batch_size_ = batch_size;
    return *this;
  }

  inline size_t batch_size() const {
    return this->batch_size_;
  }

  inline VCMulMicrokernelTester& inplace_a(bool inplace_a) {
    this->inplace_a_ = inplace_a;
    return *this;
  }

  inline bool inplace_a() const {
    return this->inplace_a_;
  }

  inline VCMulMicrokernelTester& inplace_b(bool inplace_b) {
    this->inplace_b_ = inplace_b;
    return *this;
  }

  inline bool inplace_b() const {
    return this->inplace_b_;
  }

  inline VCMulMicrokernelTester& iterations(size_t iterations) {
    this->iterations_ = iterations;
    return *this;
  }

  inline size_t iterations() const {
    return this->iterations_;
  }

  void Test(xnn_f32_vbinary_ukernel_fn vcmul, xnn_init_f32_default_params_fn init_params = nullptr) const {
    std::random_device random_device;
    auto rng = std::mt19937(random_device());
    std::uniform_real_distribution<float> f32rdist(1.0f, 10.0f);
    std::uniform_real_distribution<float> f32idist(0.01f, 0.1f);

    std::vector<float> a(2 * batch_size() + XNN_EXTRA_BYTES / sizeof(float));
    std::vector<float> b(2 * batch_size() + XNN_EXTRA_BYTES / sizeof(float));
    std::vector<float> y(2 * batch_size() + (inplace_a() || inplace_b() ? XNN_EXTRA_BYTES / sizeof(float) : 0));
    std::vector<double> y_ref(2 * batch_size());
    std::fill(a.begin(), a.end(), std::nanf(""));
    std::fill(b.begin(), b.end(), std::nanf(""));
    for (size_t iteration = 0; iteration < iterations(); iteration++) {
      std::generate_n(a.begin(), batch_size(), [&]() { return f32rdist(rng); });
      std::generate_n(a.begin() + batch_size(), batch_size(), [&]() { return f32idist(rng); });
      std::generate_n(b.begin(), batch_size(), [&]() { return f32rdist(rng); });
      std::generate_n(b.begin() + batch_size(), batch_size(), [&]() { return f32idist(rng); });
      if (inplace_a()) {
        std::copy(a.cbegin(), a.cend(), y.begin());
      } else if (inplace_b()) {
        std::copy(b.cbegin(), b.cend(), y.begin());
      } else {
        std::fill(y.begin(), y.end(), nanf(""));
      }
      const float* a_data = inplace_a() ? y.data() : a.data();
      const float* b_data = inplace_b() ? y.data() : b.data();

      // Compute reference results.
      for (size_t i = 0; i < batch_size(); i++) {
        y_ref[i] = double(a_data[i]) * double(b_data[i]) - double(a_data[i + batch_size()]) * double(b_data[i + batch_size()]);
        y_ref[i + batch_size()] = double(a_data[i]) * double(b_data[i + batch_size()]) + double(a_data[i + batch_size()]) * double(b_data[i]);
      }

      // Prepare parameters.
      xnn_f32_default_params params;
      if (init_params != nullptr) {
        init_params(&params);
      }

      // Call optimized micro-kernel.
      vcmul(batch_size() * sizeof(float), a_data, b_data, y.data(), init_params != nullptr ? &params : nullptr);

      // Verify results.
      for (size_t i = 0; i < batch_size(); i++) {
        EXPECT_NEAR(y[i], y_ref[i], std::abs(y_ref[i]) * 1.0e-4f)
          << "at " << i << " / " << batch_size();
      }
    }
  }

 private:
  size_t batch_size_{1};
  bool inplace_a_{false};
  bool inplace_b_{false};
  size_t iterations_{15};
};
