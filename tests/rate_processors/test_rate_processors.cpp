#include "RateProcessors.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <libtcspc/tcspc.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace {

using namespace tcspc;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

using abstime_type = default_numeric_traits::abstime_type;

constexpr std::size_t N = 2;
using bins = tcspc_ext::bin_counts_event<N>;
using rates = tcspc_ext::rate_event<N>;
using det = detection_event<>;
using treached = time_reached_event<>;
using misc = empty_test_event<0>;

// Channel 5 -> 0, 6 -> 1, 7 -> 2 (out of range); others unrouted.
auto test_router() {
    return channel_router(std::array{std::pair{5, std::size_t(0)},
                                     std::pair{6, std::size_t(1)},
                                     std::pair{7, std::size_t(2)}});
}

} // namespace

TEST_CASE("count_in_bins rejects nonpositive width") {
    for (abstime_type w : {abstime_type(0), abstime_type(-1)}) {
        auto make = [w] {
            return tcspc_ext::count_in_bins<det, N>(
                test_router(), arg::bin_width{w}, sink_all());
        };
        CHECK_THROWS_AS(make(), std::invalid_argument);
    }
}

TEST_CASE("count_in_bins") {
    using out_events = type_list<bins, det, treached, misc>;
    auto ctx = context::create();
    auto in =
        feed_input(feed_as::const_lvalue,
                   tcspc_ext::count_in_bins<det, N>(
                       test_router(), arg::bin_width<abstime_type>{10},
                       capture_output<out_events>(
                           ctx->tracker<capture_output_accessor>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<out_events>(feed_as::const_lvalue, ctx, "out");

    SECTION("counts by routed index; bin closes at boundary") {
        in.handle(det{100, 5});
        REQUIRE(out.check(det{100, 5}));
        in.handle(det{103, 6});
        REQUIRE(out.check(det{103, 6}));
        in.handle(misc{});
        REQUIRE(out.check(misc{}));
        in.handle(det{104, 5});
        REQUIRE(out.check(det{104, 5}));
        in.handle(det{105, 7}); // Index >= N
        REQUIRE(out.check(det{105, 7}));
        in.handle(det{106, 8}); // Unrouted
        REQUIRE(out.check(det{106, 8}));
        in.handle(det{109, 6});
        REQUIRE(out.check(det{109, 6}));
        in.handle(det{110, 5});
        REQUIRE(out.check(bins{110, 10, {2, 2}}));
        REQUIRE(out.check(det{110, 5}));
        in.handle(det{119, 5});
        REQUIRE(out.check(det{119, 5}));
        in.handle(det{120, 6});
        REQUIRE(out.check(bins{120, 10, {2, 0}}));
        REQUIRE(out.check(det{120, 6}));
        // No time elapsed in the current bin.
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("other abstime events advance bins but are not counted") {
        in.handle(treached{100});
        REQUIRE(out.check(treached{100}));
        in.handle(det{105, 5});
        REQUIRE(out.check(det{105, 5}));
        in.handle(treached{112});
        REQUIRE(out.check(bins{110, 10, {1, 0}}));
        REQUIRE(out.check(treached{112}));
        in.flush();
        REQUIRE(out.check(bins{112, 2, {0, 0}}));
        REQUIRE(out.check_flushed());
    }

    SECTION("gap yields one zero-count span and keeps alignment") {
        in.handle(det{100, 5});
        REQUIRE(out.check(det{100, 5}));
        in.handle(det{103, 6});
        REQUIRE(out.check(det{103, 6}));
        in.handle(det{147, 5});
        REQUIRE(out.check(bins{110, 10, {1, 1}}));
        REQUIRE(out.check(bins{140, 30, {0, 0}}));
        REQUIRE(out.check(det{147, 5}));
        in.handle(det{150, 6});
        REQUIRE(out.check(bins{150, 10, {1, 0}}));
        REQUIRE(out.check(det{150, 6}));
        in.handle(det{155, 6});
        REQUIRE(out.check(det{155, 6}));
        in.flush();
        REQUIRE(out.check(bins{155, 5, {0, 2}}));
        REQUIRE(out.check_flushed());
    }

    SECTION("gap ending exactly on a bin edge") {
        in.handle(det{100, 5});
        REQUIRE(out.check(det{100, 5}));
        in.handle(det{130, 5});
        REQUIRE(out.check(bins{110, 10, {1, 0}}));
        REQUIRE(out.check(bins{130, 20, {0, 0}}));
        REQUIRE(out.check(det{130, 5}));
        in.handle(det{140, 6});
        REQUIRE(out.check(bins{140, 10, {1, 0}}));
        REQUIRE(out.check(det{140, 6}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("flush with no events") {
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("exponential_moving_rate rejects nonpositive time constant") {
    for (double tau : {0.0, -1.0}) {
        auto make = [tau] {
            return tcspc_ext::exponential_moving_rate<N>(
                tcspc_ext::arg::time_constant{tau}, sink_all());
        };
        CHECK_THROWS_AS(make(), std::invalid_argument);
    }
}

TEST_CASE("exponential_moving_rate") {
    using out_events = type_list<rates, treached>;
    auto ctx = context::create();
    auto in =
        feed_input(feed_as::const_lvalue,
                   tcspc_ext::exponential_moving_rate<N>(
                       tcspc_ext::arg::time_constant{100.0},
                       capture_output<out_events>(
                           ctx->tracker<capture_output_accessor>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<out_events>(feed_as::const_lvalue, ctx, "out");

    SECTION("first bin gives its own rate; other events pass through") {
        in.handle(treached{3});
        REQUIRE(out.check(treached{3}));
        in.handle(bins{10, 0, {1, 1}}); // Ignored
        in.handle(bins{10, 10, {5, 0}});
        auto const r = out.pop<rates>();
        CHECK(r.abstime == 10);
        CHECK_THAT(r.rates[0], WithinRel(0.5, 1e-12));
        CHECK(r.rates[1] == 0.0);
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("steady input gives the true rate") {
        for (abstime_type t = 10; t <= 10'000; t += 10) {
            in.handle(bins{t, 10, {3, 7}});
            auto const r = out.pop<rates>();
            CHECK(r.abstime == t);
            CHECK_THAT(r.rates[0], WithinRel(0.3, 1e-9));
            CHECK_THAT(r.rates[1], WithinRel(0.7, 1e-9));
        }
    }

    SECTION("step change approaches new rate with time constant tau") {
        abstime_type t = 0;
        for (int i = 0; i < 2000; ++i) {
            in.handle(bins{++t, 1, {0, 1}});
            (void)out.pop<rates>();
        }
        for (int i = 0; i < 99; ++i) {
            in.handle(bins{++t, 1, {1, 0}});
            (void)out.pop<rates>();
        }
        in.handle(bins{++t, 1, {1, 0}});
        auto const r = out.pop<rates>();
        CHECK_THAT(r.rates[0], WithinRel(1.0 - std::exp(-1.0), 1e-6));
        CHECK_THAT(r.rates[1], WithinRel(std::exp(-1.0), 1e-6));
    }

    SECTION("zero-count span decays rate according to its duration") {
        abstime_type t = 0;
        for (int i = 0; i < 1000; ++i) {
            t += 10;
            in.handle(bins{t, 10, {5, 0}});
            (void)out.pop<rates>();
        }
        t += 50;
        in.handle(bins{t, 50, {0, 0}});
        auto const r = out.pop<rates>();
        CHECK(r.abstime == t);
        CHECK_THAT(r.rates[0], WithinRel(0.5 * std::exp(-0.5), 1e-9));
        CHECK_THAT(r.rates[1], WithinAbs(0.0, 1e-15));
    }
}
