#include "lhaheader.h"
#include <format>

static const char* const short_month_names[12] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
};

std::string lha_date_str(uint16_t t)
{
    const auto d = (t & 0x1f);
    const auto m = ((t >> 5) & 15);
    const auto y = 1980 + (t >> 9);

    return std::format("{:02d}-{}-{:02d}", d, m >= 1 && m <= 12 ? short_month_names[m - 1] : "???", y % 100);
}

std::string lha_time_str(uint16_t t)
{
    return std::format("{:02d}:{:02d}:{:02d}", t >> 11, (t >> 5) & 0x3f, 2 * (t & 0x1f));
}

void lha_header_set_dos_time(LhaHeader& hdr, int64_t t)
{
    static constexpr int64_t seconds_per_day = 24 * 60 * 60;
    static constexpr int64_t epoch_offset = -2440588 * seconds_per_day; // 2440588 is JDN for 1970-01-01

    constexpr auto y = 4716;
    constexpr auto j = 1401;
    constexpr auto m = 2;
    constexpr auto n = 12;
    constexpr auto r = 4;
    constexpr auto p = 1461;
    constexpr auto v = 3;
    constexpr auto u = 5;
    constexpr auto s = 153;
    constexpr auto w = 2;
    constexpr auto B = 274277;
    constexpr auto C = -38;

    const auto J = (t - epoch_offset) / seconds_per_day;
    const auto f = J + j + (((4 * J + B) / 146097) * 3) / 4 + C;
    const auto e = r * f + v;
    const auto g = (e % p) / r;
    const auto h = u * g + w;
    const auto D = (h % s) / u + 1;
    const auto M = (h / s + m) % n + 1;
    const auto Y = e / p - y + (n + m - M) / n;

    t %= seconds_per_day;
    const auto hrs = t / 3600;
    t %= 3600;
    const auto min = t / 60;
    const auto sec = t % 60;

    hdr.mod_time = (uint16_t)(hrs << 11 | min << 5 | sec >> 1);
    hdr.mod_date = (uint16_t)((Y - 1980) << 9 | M << 5 | D);
}

void lha_header_convert_unix_to_dos_time(LhaHeader& hdr)
{
    lha_header_set_dos_time(hdr, hdr.mod_date << 16 | hdr.mod_time);
}
