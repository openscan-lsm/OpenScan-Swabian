#pragma once

#include <libtcspc/arg_wrappers.hpp>
#include <libtcspc/int_types.hpp>
#include <libtcspc/introspect.hpp>
#include <libtcspc/numeric_traits.hpp>
#include <libtcspc/processor.hpp>
#include <libtcspc/route.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Processors written in libtcspc style, intended for possible upstreaming into
// libtcspc. Only the public libtcspc API is used.
namespace tcspc_ext {

namespace arg {

/**
 * \brief Function argument wrapper for time constant parameter.
 */
template <typename T> struct time_constant {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit constexpr time_constant(T arg) : value(arg) {}
};

} // namespace arg

/**
 * \brief Event carrying per-index event counts over a time span.
 *
 * \tparam N number of counters
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`
 */
template <std::size_t N,
          typename NumericTraits = tcspc::default_numeric_traits>
struct bin_counts_event {
    /** \brief The end time of the span. */
    typename NumericTraits::abstime_type abstime;

    /** \brief The duration of the span (positive). */
    typename NumericTraits::abstime_type duration;

    /** \brief The counts during the span. */
    std::array<tcspc::u64, N> counts;

    /** \brief Equality comparison operator. */
    friend auto operator==(bin_counts_event const &lhs,
                           bin_counts_event const &rhs) noexcept
        -> bool = default;

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, bin_counts_event const &e)
        -> std::ostream & {
        s << "bin_counts(" << e.abstime << ", " << e.duration << ", {";
        for (std::size_t i = 0; i < N; ++i)
            s << (i > 0 ? ", " : "") << e.counts[i];
        return s << "})";
    }
};

/**
 * \brief Event carrying per-index event rates.
 *
 * \tparam N number of rates
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`
 */
template <std::size_t N,
          typename NumericTraits = tcspc::default_numeric_traits>
struct rate_event {
    /** \brief The time at which the rates were estimated. */
    typename NumericTraits::abstime_type abstime;

    /** \brief The rates, in counts per abstime unit. */
    std::array<double, N> rates;

    /** \brief Equality comparison operator. */
    friend auto operator==(rate_event const &lhs,
                           rate_event const &rhs) noexcept -> bool = default;

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, rate_event const &e)
        -> std::ostream & {
        s << "rate(" << e.abstime << ", {";
        for (std::size_t i = 0; i < N; ++i)
            s << (i > 0 ? ", " : "") << e.rates[i];
        return s << "})";
    }
};

namespace internal {

template <typename Event>
concept abstime_stamped = requires { Event::abstime; };

template <typename Event, std::size_t N, typename NumericTraits,
          typename Router, typename Downstream>
    requires tcspc::router_for<Router, Event> &&
             tcspc::processor<Downstream, bin_counts_event<N, NumericTraits>>
class count_in_bins {
    using abstime_type = typename NumericTraits::abstime_type;
    using bin_event = bin_counts_event<N, NumericTraits>;

    abstime_type width;
    bool started = false;
    abstime_type bin_start = 0;
    abstime_type bin_end = 0;
    abstime_type last_abstime = 0;
    std::array<tcspc::u64, N> counts{};

    Router router;
    Downstream downstream;

    void advance_to(abstime_type t) {
        if (not started) {
            bin_start = t;
            bin_end = t + width;
            started = true;
        } else if (t >= bin_end) {
            downstream.handle(bin_event{bin_end, bin_end - bin_start, counts});
            counts = {};
            bin_start = bin_end;
            bin_end += width;
            if (t >= bin_end) {
                auto const new_start =
                    bin_start + ((t - bin_start) / width) * width;
                downstream.handle(
                    bin_event{new_start, new_start - bin_start, {}});
                bin_start = new_start;
                bin_end = new_start + width;
            }
        }
        last_abstime = t;
    }

  public:
    explicit count_in_bins(Router router,
                           tcspc::arg::bin_width<abstime_type> width,
                           Downstream downstream)
        : width(width.value), router(std::move(router)),
          downstream(std::move(downstream)) {
        if (this->width <= 0)
            throw std::invalid_argument(
                "count_in_bins bin_width must be positive");
    }

    [[nodiscard]] auto introspect_node() const -> tcspc::processor_info {
        return tcspc::processor_info(this, "count_in_bins");
    }

    [[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E>
        requires tcspc::handler_for<Downstream, std::remove_cvref_t<E>>
    void handle(E &&event) {
        using EE = std::remove_cvref_t<E>;
        if constexpr (abstime_stamped<EE>) {
            advance_to(event.abstime);
            if constexpr (std::is_convertible_v<EE, Event>) {
                Event const &ev = event;
                auto const idx = router(ev);
                if (idx < N)
                    ++counts[idx];
            }
        }
        downstream.handle(std::forward<E>(event));
    }

    void flush() {
        if (started && last_abstime > bin_start)
            downstream.handle(
                bin_event{last_abstime, last_abstime - bin_start, counts});
        downstream.flush();
    }
};

template <std::size_t N, typename NumericTraits, typename Downstream>
    requires tcspc::processor<Downstream, rate_event<N, NumericTraits>>
class exponential_moving_rate {
    using abstime_type = typename NumericTraits::abstime_type;
    using bin_event = bin_counts_event<N, NumericTraits>;

    double tau;
    abstime_type alpha_duration = 0;
    double alpha = 0.0;
    std::array<double, N> sums{};
    double weight = 0.0;

    Downstream downstream;

  public:
    explicit exponential_moving_rate(arg::time_constant<double> tau,
                                     Downstream downstream)
        : tau(tau.value), downstream(std::move(downstream)) {
        if (not(this->tau > 0.0))
            throw std::invalid_argument(
                "exponential_moving_rate time_constant must be positive");
    }

    [[nodiscard]] auto introspect_node() const -> tcspc::processor_info {
        return tcspc::processor_info(this, "exponential_moving_rate");
    }

    [[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void handle(bin_event const &event) {
        if (event.duration <= 0)
            return;
        if (event.duration != alpha_duration) {
            alpha = std::exp(-double(event.duration) / tau);
            alpha_duration = event.duration;
        }
        auto const dur = double(event.duration);
        rate_event<N, NumericTraits> out{event.abstime, {}};
        weight = alpha * weight + (1.0 - alpha);
        for (std::size_t i = 0; i < N; ++i) {
            sums[i] = alpha * sums[i] +
                      (1.0 - alpha) * double(event.counts[i]) / dur;
            out.rates[i] = sums[i] / weight;
        }
        downstream.handle(std::move(out));
    }

    template <typename E>
        requires(not std::is_same_v<std::remove_cvref_t<E>, bin_event>) &&
                tcspc::handler_for<Downstream, std::remove_cvref_t<E>>
    void handle(E &&event) {
        downstream.handle(std::forward<E>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that counts events in fixed-width time bins.
 *
 * Every event that has an `abstime` field (whether or not it is an \p Event)
 * advances time. Bins are half-open intervals `[start, start + width)`; the
 * first bin starts at the first abstime seen, and later bin edges are always
 * at `first + k * width` for integer `k`.
 *
 * Events convertible to \p Event are routed with \p router; if the resulting
 * index is less than \p N, the corresponding count is incremented. Other
 * indices (including `std::size_t(-1)`, "no match") are not counted.
 *
 * When an event arrives at or after the end of the current bin, a
 * `tcspc_ext::bin_counts_event` is emitted for the completed bin before the
 * event is handled (and passed through); thus abstime order is preserved,
 * provided that the input is monotonic. If the event is beyond the end of the
 * following bin as well, a single zero-count `tcspc_ext::bin_counts_event`
 * covering all of the skipped whole bins is also emitted (not one per empty
 * bin). The event is then counted in the bin that contains it.
 *
 * On flush, if any time has elapsed in the current (partial) bin, a
 * `tcspc_ext::bin_counts_event` ending at the last abstime seen is emitted.
 *
 * \tparam Event the event type to count
 *
 * \tparam N the number of counters
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`
 *
 * \tparam Router type of router (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param router router mapping \p Event to counter index (for example,
 * `tcspc::channel_router`)
 *
 * \param width the bin width; must be positive
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: emit `tcspc_ext::bin_counts_event` for any bins completed; count
 *   by routed index; pass through
 * - All other types with `abstime` field: emit
 *   `tcspc_ext::bin_counts_event` for any bins completed; pass through
 * - All other types: pass through with no action
 * - Flush: emit `tcspc_ext::bin_counts_event` for the partial bin, if
 *   nonempty in time; pass through
 */
template <typename Event, std::size_t N,
          typename NumericTraits = tcspc::default_numeric_traits,
          typename Router, typename Downstream>
auto count_in_bins(
    Router router,
    tcspc::arg::bin_width<typename NumericTraits::abstime_type> width,
    Downstream downstream) {
    return internal::count_in_bins<Event, N, NumericTraits, Router,
                                   Downstream>(std::move(router), width,
                                               std::move(downstream));
}

/**
 * \brief Create a processor that converts binned counts to exponential moving
 * average rates.
 *
 * Each `tcspc_ext::bin_counts_event` updates, for each index \e i, an
 * exponential moving average of the rate `counts[i] / duration`, weighted by
 * `alpha = exp(-duration / tau)` so that spans of different duration are
 * handled correctly. The average is bias-corrected (divided by the total
 * weight, which approaches 1 after a few \p tau), so that the first emitted
 * rate is just that of the first span, with no ramp-up from zero.
 *
 * \tparam N the number of counters
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param tau the time constant, in abstime units; must be positive
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc_ext::bin_counts_event<N, NumericTraits>`: if duration is positive,
 *   update the averages and emit `tcspc_ext::rate_event<N, NumericTraits>`
 *   with the same abstime and rates in counts per abstime unit; not passed
 *   through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <std::size_t N,
          typename NumericTraits = tcspc::default_numeric_traits,
          typename Downstream>
auto exponential_moving_rate(arg::time_constant<double> tau,
                             Downstream downstream) {
    return internal::exponential_moving_rate<N, NumericTraits, Downstream>(
        tau, std::move(downstream));
}

} // namespace tcspc_ext
