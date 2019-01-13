// Copyright 2015-2018 Hans Dembinski
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_HISTOGRAM_AXIS_REGULAR_HPP
#define BOOST_HISTOGRAM_AXIS_REGULAR_HPP

#include <boost/container/string.hpp> // default meta data
#include <boost/histogram/axis/base.hpp>
#include <boost/histogram/axis/interval_view.hpp>
#include <boost/histogram/axis/iterator.hpp>
#include <boost/histogram/detail/meta.hpp>
#include <boost/histogram/fwd.hpp>
#include <boost/throw_exception.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace boost {
namespace histogram {
namespace axis {

// two_pi can be found in boost/math, but it is defined here to reduce deps
constexpr double two_pi = 6.283185307179586;

namespace transform {

struct id {
  template <typename T>
  static T forward(T&& x) noexcept {
    return std::forward<T>(x);
  }
  template <typename T>
  static T inverse(T&& x) noexcept {
    return std::forward<T>(x);
  }
};

struct log {
  template <typename T>
  static T forward(T x) {
    return std::log(x);
  }
  template <typename T>
  static T inverse(T x) {
    return std::exp(x);
  }
};

struct sqrt {
  template <typename T>
  static T forward(T x) {
    return std::sqrt(x);
  }
  template <typename T>
  static T inverse(T x) {
    return x * x;
  }
};

struct pow {
  double power = 1;

  explicit pow(double p) : power(p) {}
  pow() = default;

  template <typename T>
  auto forward(T v) const {
    return std::pow(v, power);
  }
  template <typename T>
  auto inverse(T v) const {
    return std::pow(v, 1.0 / power);
  }

  bool operator==(const pow& o) const noexcept { return power == o.power; }
};

} // namespace transform

/** Axis for equidistant intervals on the real line.
 *
 * The most common binning strategy.
 * Very fast. Binning is a O(1) operation.
 */
template <class RealType, class Transform, class MetaData, option Options>
class regular : public base<MetaData, Options>,
                public iterator_mixin<regular<RealType, Transform, MetaData, Options>>,
                protected Transform {
  static_assert(!test(Options, option::circular) || !test(Options, option::underflow),
                "circular axis cannot have underflow");
  using base_type = base<MetaData, Options>;

public:
  using value_type = RealType;
  using transform_type = Transform;
  using metadata_type = MetaData;

private:
  using unit_type = detail::get_unit_type<value_type>;
  using internal_type = detail::get_scale_type<value_type>;

public:
  /** Construct n bins over real transformed range [begin, end).
   *
   * \param trans    transform instance to use.
   * \param n        number of bins.
   * \param start    low edge of first bin.
   * \param stop     high edge of last bin.
   * \param metadata description of the axis.
   */
  regular(transform_type trans, unsigned n, value_type start, value_type stop,
          metadata_type m = {})
      : base_type(n, std::move(m))
      , transform_type(std::move(trans))
      , min_(this->forward(detail::get_scale(start)))
      , delta_(this->forward(detail::get_scale(stop)) - min_) {
    if (!std::isfinite(min_) || !std::isfinite(delta_))
      BOOST_THROW_EXCEPTION(
          std::invalid_argument("forward transform of start or stop invalid"));
    if (delta_ == 0)
      BOOST_THROW_EXCEPTION(std::invalid_argument("range of axis is zero"));
  }

  /** Construct n bins over real range [begin, end).
   *
   * \param n        number of bins.
   * \param start    low edge of first bin.
   * \param stop     high edge of last bin.
   * \param metadata description of the axis.
   */
  regular(unsigned n, value_type start, value_type stop, metadata_type m = {})
      : regular({}, n, start, stop, std::move(m)) {}

  /// Constructor used by algorithm::reduce to shrink and rebin (not for users).
  regular(const regular& src, int begin, int end, unsigned merge)
      : base_type((end - begin) / merge, src.metadata())
      , transform_type(src.transform())
      , min_(this->forward(detail::get_scale(src.value(begin))))
      , delta_(this->forward(detail::get_scale(src.value(end))) - min_) {
    BOOST_ASSERT((end - begin) % merge == 0);
    if (test(Options, option::circular) && !(begin == 0 && end == src.size()))
      BOOST_THROW_EXCEPTION(std::invalid_argument("cannot shrink circular axis"));
  }

  regular() = default;

  /// Returns instance of the transform type
  const transform_type& transform() const noexcept { return *this; }

  /// Returns the bin index for the passed argument.
  int operator()(value_type x) const noexcept {
    // Runs in hot loop, please measure impact of changes
    auto z = (this->forward(x / unit_type()) - min_) / delta_;
    if (test(Options, option::circular)) {
      if (std::isfinite(z)) {
        z -= std::floor(z);
        return static_cast<int>(z * base_type::size());
      }
    } else {
      if (z < 1) {
        if (z >= 0)
          return static_cast<int>(z * base_type::size());
        else
          return -1;
      }
    }
    return base_type::size(); // also returned if x is NaN
  }

  // /// Returns index and shift (if axis has grown) for the passed argument.
  // template <option O = Options, class = std::enable_if_t<test(O, option::growth)>>
  // auto update(value_type x) {
  //   auto z = (this->forward(x / unit_type()) - min_) / delta_;
  //   if (std::isfinite(z)) {
  //     if (0 <= z) {
  //       if (z < 1) return std::make_pair(static_cast<int>(z * base_type::size()), 0);
  //       const auto i = static_cast<int>(z * base_type::size());
  //       const auto shift = i - base_type::size() + 1;
  //       delta_ /= base_type::size();
  //       base_type::grow(shift);
  //       delta_ *= base_type::size();
  //       return std::make_pair(i, shift);
  //     }
  //     z = std::floor(z);
  //     const auto i = static_cast<int>(z * base_type::size());
  //     min_ += delta_ * z;
  //     delta_ /= base_type::size();
  //     base_type::grow(-i);
  //     delta_ *= base_type::size();
  //     return std::make_pair(0, i);
  //   }
  //   BOOST_THROW_EXCEPTION(std::invalid_argument("argument is not finite"));
  //   return std::make_pair(0, 0);
  // }

  /// Returns axis value for fractional index.
  value_type value(double i) const noexcept {
    auto z = i / base_type::size();
    if (!test(Options, option::circular) && z < 0.0)
      z = -std::numeric_limits<internal_type>::infinity() * delta_;
    else if (test(Options, option::circular) || z <= 1.0)
      z = (1.0 - z) * min_ + z * (min_ + delta_);
    else {
      z = std::numeric_limits<internal_type>::infinity() * delta_;
    }
    return this->inverse(z) * unit_type();
  }

  /// Access bin at index
  decltype(auto) operator[](int idx) const noexcept {
    return interval_view<regular>(*this, idx);
  }

  bool operator==(const regular& o) const noexcept {
    return base_type::operator==(o) &&
           detail::relaxed_equal(transform(), o.transform()) && min_ == o.min_ &&
           delta_ == o.delta_;
  }

  bool operator!=(const regular& o) const noexcept { return !operator==(o); }

  template <class Archive>
  void serialize(Archive&, unsigned);

private:
  internal_type min_, delta_;
};

#if __cpp_deduction_guides >= 201606

template <class T>
regular(unsigned, T, T)->regular<detail::convert_integer<T, double>>;

template <class T>
regular(unsigned, T, T, const char*)->regular<detail::convert_integer<T, double>>;

template <class T, class M>
regular(unsigned, T, T, M)->regular<detail::convert_integer<T, double>, transform::id, M>;

template <class Tr, class T>
regular(Tr, unsigned, T, T)->regular<detail::convert_integer<T, double>, Tr>;

template <class Tr, class T>
regular(Tr, unsigned, T, T, const char*)->regular<detail::convert_integer<T, double>, Tr>;

template <class Tr, class T, class M>
regular(Tr, unsigned, T, T, M)->regular<detail::convert_integer<T, double>, Tr, M>;

#endif

} // namespace axis
} // namespace histogram
} // namespace boost

#endif
