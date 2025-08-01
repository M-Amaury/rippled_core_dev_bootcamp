//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_PUBLICKEY_H_INCLUDED
#define RIPPLE_PROTOCOL_PUBLICKEY_H_INCLUDED

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/STExchange.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/json_get_or_throw.h>
#include <xrpl/protocol/tokens.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>

namespace ripple {

/** A public key.

    Public keys are used in the public-key cryptography
    system used to verify signatures attached to messages.

    The format of the public key is Ripple specific,
    information needed to determine the cryptosystem
    parameters used is stored inside the key.

    As of this writing two systems are supported:

        secp256k1
        ed25519

    secp256k1 public keys consist of a 33 byte
    compressed public key, with the lead byte equal
    to 0x02 or 0x03.

    The ed25519 public keys consist of a 1 byte
    prefix constant 0xED, followed by 32 bytes of
    public key data.
*/
class PublicKey
{
protected:
    // All the constructed public keys are valid, non-empty and contain 33
    // bytes of data.
    static constexpr std::size_t size_ = 33;
    std::uint8_t buf_[size_];  // should be large enough

public:
    using const_iterator = std::uint8_t const*;

public:
    PublicKey() = delete;

    PublicKey(PublicKey const& other);
    PublicKey&
    operator=(PublicKey const& other);

    /** Create a public key.

        Preconditions:
            publicKeyType(slice) != std::nullopt
    */
    explicit PublicKey(Slice const& slice);

    std::uint8_t const*
    data() const noexcept
    {
        return buf_;
    }

    std::size_t
    size() const noexcept
    {
        return size_;
    }

    const_iterator
    begin() const noexcept
    {
        return buf_;
    }

    const_iterator
    cbegin() const noexcept
    {
        return buf_;
    }

    const_iterator
    end() const noexcept
    {
        return buf_ + size_;
    }

    const_iterator
    cend() const noexcept
    {
        return buf_ + size_;
    }

    Slice
    slice() const noexcept
    {
        return {buf_, size_};
    }

    operator Slice() const noexcept
    {
        return slice();
    }
};

/** Print the public key to a stream.
 */
std::ostream&
operator<<(std::ostream& os, PublicKey const& pk);

inline bool
operator==(PublicKey const& lhs, PublicKey const& rhs)
{
    return std::memcmp(lhs.data(), rhs.data(), rhs.size()) == 0;
}

inline bool
operator<(PublicKey const& lhs, PublicKey const& rhs)
{
    return std::lexicographical_compare(
        lhs.data(),
        lhs.data() + lhs.size(),
        rhs.data(),
        rhs.data() + rhs.size());
}

template <class Hasher>
void
hash_append(Hasher& h, PublicKey const& pk)
{
    h(pk.data(), pk.size());
}

template <>
struct STExchange<STBlob, PublicKey>
{
    explicit STExchange() = default;

    using value_type = PublicKey;

    static void
    get(std::optional<value_type>& t, STBlob const& u)
    {
        t.emplace(Slice(u.data(), u.size()));
    }

    static std::unique_ptr<STBlob>
    set(SField const& f, PublicKey const& t)
    {
        return std::make_unique<STBlob>(f, t.data(), t.size());
    }
};

//------------------------------------------------------------------------------

inline std::string
toBase58(TokenType type, PublicKey const& pk)
{
    return encodeBase58Token(type, pk.data(), pk.size());
}

template <>
std::optional<PublicKey>
parseBase58(TokenType type, std::string const& s);

enum class ECDSACanonicality { canonical, fullyCanonical };

/** Determines the canonicality of a signature.

    A canonical signature is in its most reduced form.
    For example the R and S components do not contain
    additional leading zeroes. However, even in
    canonical form, (R,S) and (R,G-S) are both
    valid signatures for message M.

    Therefore, to prevent malleability attacks we
    define a fully canonical signature as one where:

        R < G - S

    where G is the curve order.

    This routine returns std::nullopt if the format
    of the signature is invalid (for example, the
    points are encoded incorrectly).

    @return std::nullopt if the signature fails
            validity checks.

    @note Only the format of the signature is checked,
          no verification cryptography is performed.
*/
std::optional<ECDSACanonicality>
ecdsaCanonicality(Slice const& sig);

/** Returns the type of public key.

    @return std::nullopt If the public key does not
            represent a known type.
*/
/** @{ */
[[nodiscard]] std::optional<KeyType>
publicKeyType(Slice const& slice);

[[nodiscard]] inline std::optional<KeyType>
publicKeyType(PublicKey const& publicKey)
{
    return publicKeyType(publicKey.slice());
}
/** @} */

/** Verify a secp256k1 signature on the digest of a message. */
[[nodiscard]] bool
verifyDigest(
    PublicKey const& publicKey,
    uint256 const& digest,
    Slice const& sig,
    bool mustBeFullyCanonical = true) noexcept;

/** Verify a signature on a message.
    With secp256k1 signatures, the data is first hashed with
    SHA512-Half, and the resulting digest is signed.
*/
[[nodiscard]] bool
verify(
    PublicKey const& publicKey,
    Slice const& m,
    Slice const& sig,
    bool mustBeFullyCanonical = true) noexcept;

/** Calculate the 160-bit node ID from a node public key. */
NodeID
calcNodeID(PublicKey const&);

// VFALCO This belongs in AccountID.h but
//        is here because of header issues
AccountID
calcAccountID(PublicKey const& pk);

}  // namespace ripple

//------------------------------------------------------------------------------

namespace Json {
template <>
inline ripple::PublicKey
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    std::string const b58 = getOrThrow<std::string>(v, field);
    if (auto pubKeyBlob = strUnHex(b58); publicKeyType(makeSlice(*pubKeyBlob)))
    {
        return PublicKey{makeSlice(*pubKeyBlob)};
    }
    for (auto const tokenType :
         {TokenType::NodePublic, TokenType::AccountPublic})
    {
        if (auto const pk = parseBase58<PublicKey>(tokenType, b58))
            return *pk;
    }
    Throw<JsonTypeMismatchError>(field.getJsonName(), "PublicKey");
}
}  // namespace Json

#endif