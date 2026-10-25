// Вместо boost/numeric/interval/hw_rounding.hpp из Boost 1.84 (этот путь
// стоит в поиске раньше префикса).
//
// Boost.Interval нужен управляемый режим округления процессора, а в
// WebAssembly его нет: Boost для wasm объявляет BOOST_NO_FENV_H, и исходный
// заголовок останавливает компиляцию через #error. CGAL 6 подключает его
// через Surface_sweep_2 (Geometry/VoronoiUtilsCgal.cpp) ради рациональных
// дуг, которыми libslic3r не пользуется.
//
// Здесь те же объявления, но смена режима округления бросает исключение:
// если интервалы Boost когда-нибудь понадобятся, ошибка будет громкой,
// а не тихо неверный результат.
#ifndef BOOST_NUMERIC_INTERVAL_HW_ROUNDING_HPP
#define BOOST_NUMERIC_INTERVAL_HW_ROUNDING_HPP

#include <stdexcept>

#include <boost/config.hpp>
#include <boost/numeric/interval/rounding.hpp>
#include <boost/numeric/interval/rounded_arith.hpp>

namespace boost {
namespace numeric {
namespace interval_lib {

namespace detail {
struct wasm_no_rounding_control
{
    typedef int rounding_mode;
    [[noreturn]] static void unsupported()
    {
        throw std::runtime_error("Boost.Interval: directed rounding is not available in WebAssembly");
    }
    static void set_rounding_mode(rounding_mode) { unsupported(); }
    static void get_rounding_mode(rounding_mode &mode) { mode = 0; }
    static void downward() { unsupported(); }
    static void upward() { unsupported(); }
    static void to_nearest() {}
    static void tonearest() {}
};
} // namespace detail

template<> struct rounding_control<float> : detail::wasm_no_rounding_control
{
    static float force_rounding(float const &r) { return r; }
};
template<> struct rounding_control<double> : detail::wasm_no_rounding_control
{
    static double force_rounding(double const &r) { return r; }
};
template<> struct rounding_control<long double> : detail::wasm_no_rounding_control
{
    static long double force_rounding(long double const &r) { return r; }
};

template<> struct rounded_math<float> : save_state<rounded_arith_opp<float>> {};
template<> struct rounded_math<double> : save_state<rounded_arith_opp<double>> {};
template<> struct rounded_math<long double> : save_state<rounded_arith_opp<long double>> {};

} // namespace interval_lib
} // namespace numeric
} // namespace boost

#endif // BOOST_NUMERIC_INTERVAL_HW_ROUNDING_HPP
