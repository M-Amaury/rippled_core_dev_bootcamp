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

#ifndef RIPPLE_SHAMAP_SHAMAPLEAFNODE_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPLEAFNODE_H_INCLUDED

#include <xrpld/shamap/SHAMapItem.h>
#include <xrpld/shamap/SHAMapTreeNode.h>

#include <cstdint>

namespace ripple {

class SHAMapLeafNode : public SHAMapTreeNode
{
protected:
    boost::intrusive_ptr<SHAMapItem const> item_;

    SHAMapLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid);

    SHAMapLeafNode(
        boost::intrusive_ptr<SHAMapItem const> item,
        std::uint32_t cowid,
        SHAMapHash const& hash);

public:
    SHAMapLeafNode(SHAMapLeafNode const&) = delete;
    SHAMapLeafNode&
    operator=(SHAMapLeafNode const&) = delete;

    bool
    isLeaf() const final override
    {
        return true;
    }

    bool
    isInner() const final override
    {
        return false;
    }

    void
    invariants(bool is_root = false) const final override;

public:
    boost::intrusive_ptr<SHAMapItem const> const&
    peekItem() const;

    /** Set the item that this node points to and update the node's hash.

        @param i the new item
        @return false if the change was, effectively, a noop (that is, if the
                hash was unchanged); true otherwise.
     */
    bool
    setItem(boost::intrusive_ptr<SHAMapItem const> i);

    std::string
    getString(SHAMapNodeID const&) const final override;
};

}  // namespace ripple

#endif
