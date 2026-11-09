// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtCore/qhashfunctions.h>

#include <cstdint>
#include <cstring>
#include <limits>

namespace {
void UnsignedOverflow()
{
    volatile std::uint64_t value{std::numeric_limits<std::uint64_t>::max()};
    volatile auto result = value + 1;
    (void)result;
}

void SignedOverflow()
{
    volatile std::int64_t value{std::numeric_limits<std::int64_t>::max()};
    volatile auto result = value + 1;
    (void)result;
}

void Truncation()
{
    volatile std::uint64_t value{std::numeric_limits<std::uint64_t>::max()};
    volatile std::uint32_t result = value;
    (void)result;
}
} // namespace

namespace QHashPrivate {
// These controls verify that the rule matches neither neighboring functions
// nor other sanitizer checks within a function named hash.
void hashNeighbor()
{
    volatile std::uint64_t value{std::numeric_limits<std::uint64_t>::max()};
    volatile auto result = value + 1;
    (void)result;
}

void hash()
{
    volatile std::int64_t value{std::numeric_limits<std::int64_t>::max()};
    volatile auto result = value + 1;
    (void)result;
}
} // namespace QHashPrivate

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "qt-hash") == 0) {
        volatile quint64 value{std::numeric_limits<quint64>::max()};
        volatile auto result = qHash(static_cast<quint64>(value));
        (void)result;
    } else if (std::strcmp(argv[1], "unsigned") == 0) {
        UnsignedOverflow();
    } else if (std::strcmp(argv[1], "signed") == 0) {
        SignedOverflow();
    } else if (std::strcmp(argv[1], "truncation") == 0) {
        Truncation();
    } else if (std::strcmp(argv[1], "neighbor") == 0) {
        QHashPrivate::hashNeighbor();
    } else if (std::strcmp(argv[1], "hash-signed") == 0) {
        QHashPrivate::hash();
    } else {
        return 2;
    }
    return 0;
}
