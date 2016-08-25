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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/shamap/SHAMap.h>

namespace ripple {

// This code is used to compare another node's transaction tree
// to our own. It returns a map containing all items that are different
// between two SHA maps. It is optimized not to descend down tree
// branches with the same branch hash. A limit can be passed so
// that we will abort early if a node sends a map to us that
// makes no sense at all. (And our sync algorithm will avoid
// synchronizing matching branches too.)

bool SHAMap::walkBranch (SHAMapAbstractNode* node,
                         std::shared_ptr<SHAMapItem const> const& otherMapItem,
                         bool isFirstMap,Delta& differences, int& maxCount) const
{
    // Walk a branch of a SHAMap that's matched by an empty branch or single item in the other map
    std::stack <SHAMapAbstractNode*, std::vector<SHAMapAbstractNode*>> nodeStack;
    nodeStack.push (node);

    bool emptyBranch = !otherMapItem;

    while (!nodeStack.empty ())
    {
        node = nodeStack.top ();
        nodeStack.pop ();

        if (node->isInner ())
        {
            // This is an inner node, add all non-empty branches
            auto inner = static_cast<SHAMapInnerNode*>(node);
            for (int i = 0; i < 16; ++i)
                if (!inner->isEmptyBranch (i))
                    nodeStack.push ({descendThrow (inner, i)});
        }
        else
        {
            // This is a leaf node, process its item
            auto item = static_cast<SHAMapTreeNode*>(node)->peekItem();

            if (emptyBranch || (item->key() != otherMapItem->key()))
            {
                // unmatched
                if (isFirstMap)
                    differences.insert (std::make_pair (item->key(),
                        DeltaRef (item, std::shared_ptr<SHAMapItem const> ())));
                else
                    differences.insert (std::make_pair (item->key(),
                        DeltaRef (std::shared_ptr<SHAMapItem const> (), item)));

                if (--maxCount <= 0)
                    return false;
            }
            else if (item->peekData () != otherMapItem->peekData ())
            {
                // non-matching items with same tag
                if (isFirstMap)
                    differences.insert (std::make_pair (item->key(),
                                                DeltaRef (item, otherMapItem)));
                else
                    differences.insert (std::make_pair (item->key(),
                                                DeltaRef (otherMapItem, item)));

                if (--maxCount <= 0)
                    return false;

                emptyBranch = true;
            }
            else
            {
                // exact match
                emptyBranch = true;
            }
        }
    }

    if (!emptyBranch)
    {
        // otherMapItem was unmatched, must add
        if (isFirstMap) // this is first map, so other item is from second
            differences.insert(std::make_pair(otherMapItem->key(),
                                              DeltaRef(std::shared_ptr<SHAMapItem const>(),
                                                       otherMapItem)));
        else
            differences.insert(std::make_pair(otherMapItem->key(),
                DeltaRef(otherMapItem, std::shared_ptr<SHAMapItem const>())));

        if (--maxCount <= 0)
            return false;
    }

    return true;
}

bool
SHAMap::compare (SHAMap const& otherMap,
                 Delta& differences, int maxCount) const
{
    // compare two hash trees, add up to maxCount differences to the difference table
    // return value: true=complete table of differences given, false=too many differences
    // throws on corrupt tables or missing nodes
    // CAUTION: otherMap is not locked and must be immutable

    assert (isValid () && otherMap.isValid ());

    if (getHash () == otherMap.getHash ())
        return true;

    using StackEntry = std::pair <SHAMapAbstractNode*, SHAMapAbstractNode*>;
    std::stack <StackEntry, std::vector<StackEntry>> nodeStack; // track nodes we've pushed

    nodeStack.push ({root_.get(), otherMap.root_.get()});
    while (!nodeStack.empty ())
    {
        SHAMapAbstractNode* ourNode = nodeStack.top().first;
        SHAMapAbstractNode* otherNode = nodeStack.top().second;
        nodeStack.pop ();

        if (!ourNode || !otherNode)
        {
            assert (false);
            Throw<SHAMapMissingNode> (type_, uint256 ());
        }

        if (ourNode->isLeaf () && otherNode->isLeaf ())
        {
            // two leaves
            auto ours = static_cast<SHAMapTreeNode*>(ourNode);
            auto other = static_cast<SHAMapTreeNode*>(otherNode);
            if (ours->peekItem()->key() == other->peekItem()->key())
            {
                if (ours->peekItem()->peekData () != other->peekItem()->peekData ())
                {
                    differences.insert (std::make_pair (ours->peekItem()->key(),
                                                 DeltaRef (ours->peekItem (),
                                                 other->peekItem ())));
                    if (--maxCount <= 0)
                        return false;
                }
            }
            else
            {
                differences.insert (std::make_pair(ours->peekItem()->key(),
                                                   DeltaRef(ours->peekItem(),
                                                   std::shared_ptr<SHAMapItem const>())));
                if (--maxCount <= 0)
                    return false;

                differences.insert(std::make_pair(other->peekItem()->key(),
                    DeltaRef(std::shared_ptr<SHAMapItem const>(), other->peekItem ())));
                if (--maxCount <= 0)
                    return false;
            }
        }
        else if (ourNode->isInner () && otherNode->isLeaf ())
        {
            auto ours = static_cast<SHAMapInnerNode*>(ourNode);
            auto other = static_cast<SHAMapTreeNode*>(otherNode);
            if (!walkBranch (ours, other->peekItem (),
                    true, differences, maxCount))
                return false;
        }
        else if (ourNode->isLeaf () && otherNode->isInner ())
        {
            auto ours = static_cast<SHAMapTreeNode*>(ourNode);
            auto other = static_cast<SHAMapInnerNode*>(otherNode);
            if (!otherMap.walkBranch (other, ours->peekItem (),
                                       false, differences, maxCount))
                return false;
        }
        else if (ourNode->isInner () && otherNode->isInner ())
        {
            auto ours = static_cast<SHAMapInnerNode*>(ourNode);
            auto other = static_cast<SHAMapInnerNode*>(otherNode);
            for (int i = 0; i < 16; ++i)
                if (ours->getChildHash (i) != other->getChildHash (i))
                {
                    if (other->isEmptyBranch (i))
                    {
                        // We have a branch, the other tree does not
                        SHAMapAbstractNode* iNode = descendThrow (ours, i);
                        if (!walkBranch (iNode,
                                         std::shared_ptr<SHAMapItem const> (), true,
                                         differences, maxCount))
                            return false;
                    }
                    else if (ours->isEmptyBranch (i))
                    {
                        // The other tree has a branch, we do not
                        SHAMapAbstractNode* iNode =
                            otherMap.descendThrow(other, i);
                        if (!otherMap.walkBranch (iNode,
                                                   std::shared_ptr<SHAMapItem const>(),
                                                   false, differences, maxCount))
                            return false;
                    }
                    else // The two trees have different non-empty branches
                        nodeStack.push ({descendThrow (ours, i),
                                        otherMap.descendThrow (other, i)});
                }
        }
        else
            assert (false);
    }

    return true;
}

void SHAMap::walkMap (std::vector<SHAMapMissingNode>& missingNodes, int maxMissing) const
{
    if (!root_->isInner ())  // root_ is only node, and we have it
        return;

    SHAMapSyncFilter* filter = nullptr;
    int max = maxMissing;
    // copy from getMissingNodes
    std::uint32_t generation = f_.fullbelow().getGeneration();
    int const maxDefer = f_.db().getDesiredAsyncReadCount ();

    // Track the missing hashes we have found so far
    std::set <SHAMapHash> missingHashes;

    while (1)
    {
        std::vector <std::tuple <SHAMapInnerNode*, int, SHAMapNodeID>> deferredReads;
        deferredReads.reserve (maxDefer + 16);

        using StackEntry = std::tuple<SHAMapInnerNode*, SHAMapNodeID, int, int, bool>;
        std::stack <StackEntry, std::vector<StackEntry>> stack;

        // Traverse the map without blocking

        auto node = static_cast<SHAMapInnerNode*>(root_.get());
        SHAMapNodeID nodeID;

        // The firstChild value is selected randomly so if multiple threads
        // are traversing the map, each thread will start at a different
        // (randomly selected) inner node.  This increases the likelihood
        // that the two threads will produce different request sets (which is
        // more efficient than sending identical requests).
        int firstChild = rand() % 256;
        int currentChild = 0;
        bool fullBelow = true;

        do
        {
            while (currentChild < 16)
            {
                int branch = (firstChild + currentChild++) % 16;
                if (!node->isEmptyBranch (branch))
                {
                    auto const& childHash = node->getChildHash (branch);

                    if (missingHashes.count (childHash) != 0)
                    {
                        fullBelow = false;
                    }
                    else if (! backed_ || ! f_.fullbelow().touch_if_exists (childHash.as_uint256()))
                    {
                        SHAMapNodeID childID = nodeID.getChildNodeID (branch);
                        bool pending = false;
                        auto d = descendAsync (node, branch, childID, filter, pending);

                        if (!d)
                        {
                            if (!pending)
                            { // node is not in the database
                                //nodeIDs.push_back (childID);
                                //hashes.push_back (childHash.as_uint256());
                                missingNodes.emplace_back (type_, childHash.as_uint256());

                                if (--max <= 0)
                                    return;
                            }
                            else
                            {
                                // read is deferred
                                deferredReads.emplace_back (node, branch, childID);
                            }

                            fullBelow = false; // This node is not known full below
                        }
                        else if (d->isInner() &&
                                 !static_cast<SHAMapInnerNode*>(d)->isFullBelow(generation))
                        {
                            stack.push (std::make_tuple (node, nodeID,
                                  firstChild, currentChild, fullBelow));

                            // Switch to processing the child node
                            node = static_cast<SHAMapInnerNode*>(d);
                            nodeID = childID;
                            firstChild = rand() % 256;
                            currentChild = 0;
                            fullBelow = true;
                        }
                    }
                }
            }

            // We are done with this inner node (and thus all of its children)

            if (fullBelow)
            { // No partial node encountered below this node
                node->setFullBelowGen (generation);
                if (backed_)
                    f_.fullbelow().insert (node->getNodeHash ().as_uint256());
            }

            if (stack.empty ())
                node = nullptr; // Finished processing the last node, we are done
            else
            { // Pick up where we left off (above this node)
                bool was;
                std::tie(node, nodeID, firstChild, currentChild, was) = stack.top ();
                fullBelow = was && fullBelow; // was and still is
                stack.pop ();
            }

        }
        while ((node != nullptr) && (deferredReads.size () <= maxDefer));

        // If we didn't defer any reads, we're done
        if (deferredReads.empty ())
            break;

        auto const before = std::chrono::steady_clock::now();
        f_.db().waitReads();
        auto const after = std::chrono::steady_clock::now();

        auto const elapsed = std::chrono::duration_cast
            <std::chrono::milliseconds> (after - before);
        auto const count = deferredReads.size ();

        // Process all deferred reads
        int hits = 0;
        for (auto const& node : deferredReads)
        {
            auto parent = std::get<0>(node);
            auto branch = std::get<1>(node);
            auto const& nodeID = std::get<2>(node);
            auto const& nodeHash = parent->getChildHash (branch);

            auto nodePtr = fetchNodeNT(nodeID, nodeHash, filter);
            if (nodePtr)
            {
                ++hits;
                if (backed_)
                    canonicalize (nodeHash, nodePtr);
                nodePtr = parent->canonicalizeChild (branch, std::move(nodePtr));
            }
            else if ((max > 0) && (missingHashes.insert (nodeHash).second))
            {
                //nodeIDs.push_back (nodeID);
                //hashes.push_back (nodeHash.as_uint256());
                missingNodes.emplace_back (type_, nodeHash.as_uint256 ());

                --max;
            }
        }

        auto const process_time = std::chrono::duration_cast
            <std::chrono::milliseconds> (std::chrono::steady_clock::now() - after);

        if ((count > 50) || (elapsed.count() > 50))
            journal_.debug << "getMissingNodes reads " <<
                count << " nodes (" << hits << " hits) in "
                << elapsed.count() << " + " << process_time.count()  << " ms";

        if (max <= 0)
            return;

    }
    return;

    using StackEntry = std::shared_ptr<SHAMapInnerNode>;
    std::stack <StackEntry, std::vector <StackEntry>> nodeStack;

    nodeStack.push (std::static_pointer_cast<SHAMapInnerNode>(root_));

    while (!nodeStack.empty ())
    {
        std::shared_ptr<SHAMapInnerNode> node = std::move (nodeStack.top());
        nodeStack.pop ();

        for (int i = 0; i < 16; ++i)
        {
            if (!node->isEmptyBranch (i))
            {
                std::shared_ptr<SHAMapAbstractNode> nextNode = descendNoStore (node, i);

                if (nextNode)
                {
                    if (nextNode->isInner ())
                        nodeStack.push(
                            std::static_pointer_cast<SHAMapInnerNode>(nextNode));
                }
                else
                {
                    missingNodes.emplace_back (type_, node->getChildHash (i));
                    if (--maxMissing <= 0)
                        return;
                }
            }
        }
    }
}

} // ripple
