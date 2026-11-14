#include "Sha256Verifier.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace Zenvra::Services::Installation::Verification
{

namespace
{

constexpr std::array<uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t choose(uint32_t e, uint32_t f, uint32_t g) { return (e & f) ^ (~e & g); }
inline uint32_t majority(uint32_t a, uint32_t b, uint32_t c) { return (a & b) ^ (a & c) ^ (b & c); }
inline uint32_t sig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t sig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t theta0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t theta1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

class Sha256Context
{
public:
    Sha256Context() { reset(); }

    void reset()
    {
        m_state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        m_datalen = 0;
        m_bitlen = 0;
    }

    void update(const uint8_t* data, std::size_t length)
    {
        for (std::size_t i = 0; i < length; ++i)
        {
            m_data[m_datalen++] = data[i];
            if (m_datalen == 64)
            {
                transform();
                m_bitlen += 512;
                m_datalen = 0;
            }
        }
    }

    std::string finalize()
    {
        std::size_t i = m_datalen;
        if (m_datalen < 56)
        {
            m_data[i++] = 0x80;
            while (i < 56) m_data[i++] = 0x00;
        }
        else
        {
            m_data[i++] = 0x80;
            while (i < 64) m_data[i++] = 0x00;
            transform();
            std::memset(m_data.data(), 0, 56);
        }

        m_bitlen += m_datalen * 8;
        for (int b = 7; b >= 0; --b)
        {
            m_data[63 - b] = static_cast<uint8_t>((m_bitlen >> (b * 8)) & 0xff);
        }
        transform();

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint32_t word : m_state)
        {
            oss << std::setw(8) << word;
        }
        return oss.str();
    }

private:
    void transform()
    {
        std::array<uint32_t, 64> m{};
        for (std::size_t i = 0, j = 0; i < 16; ++i, j += 4)
        {
            m[i] = (static_cast<uint32_t>(m_data[j]) << 24) |
                   (static_cast<uint32_t>(m_data[j + 1]) << 16) |
                   (static_cast<uint32_t>(m_data[j + 2]) << 8) |
                   (static_cast<uint32_t>(m_data[j + 3]));
        }
        for (std::size_t i = 16; i < 64; ++i)
        {
            m[i] = theta1(m[i - 2]) + m[i - 7] + theta0(m[i - 15]) + m[i - 16];
        }

        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];
        uint32_t f = m_state[5];
        uint32_t g = m_state[6];
        uint32_t h = m_state[7];

        for (std::size_t i = 0; i < 64; ++i)
        {
            uint32_t t1 = h + sig1(e) + choose(e, f, g) + K[i] + m[i];
            uint32_t t2 = sig0(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    std::array<uint32_t, 8> m_state{};
    std::array<uint8_t, 64> m_data{};
    std::size_t m_datalen{0};
    uint64_t m_bitlen{0};
};

} // namespace

std::string Sha256Verifier::compute_hash_bytes(const void* data, std::size_t size)
{
    if (!data && size > 0)
        return {};

    Sha256Context ctx;
    ctx.update(static_cast<const uint8_t*>(data), size);
    return ctx.finalize();
}

std::string Sha256Verifier::compute_hash_file(const std::filesystem::path& file_path)
{
    std::ifstream stream(file_path, std::ios::binary);
    if (!stream.is_open())
        return {};

    Sha256Context ctx;
    std::array<char, 65536> buffer{};
    while (stream.read(buffer.data(), buffer.size()) || stream.gcount() > 0)
    {
        ctx.update(reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<std::size_t>(stream.gcount()));
    }

    return ctx.finalize();
}

bool Sha256Verifier::verify_file(const std::filesystem::path& file_path, std::string_view expected_sha256)
{
    if (expected_sha256.empty())
        return false;

    std::string actual = compute_hash_file(file_path);
    if (actual.size() != expected_sha256.size())
        return false;

    // Case-insensitive comparison
    return std::equal(actual.begin(), actual.end(), expected_sha256.begin(), expected_sha256.end(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

} // namespace Zenvra::Services::Installation::Verification

