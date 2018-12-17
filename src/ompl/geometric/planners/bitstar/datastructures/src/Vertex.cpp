/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2014, University of Toronto
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the University of Toronto nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Authors: Jonathan Gammell */

// My definition:
#include "ompl/geometric/planners/bitstar/datastructures/Vertex.h"

// For std::move
#include <utility>

// For exceptions:
#include "ompl/util/Exception.h"

// BIT*:
// A collection of common helper functions
#include "ompl/geometric/planners/bitstar/datastructures/HelperFunctions.h"
// The ID generator class
#include "ompl/geometric/planners/bitstar/datastructures/IdGenerator.h"
// The cost-helper class:
#include "ompl/geometric/planners/bitstar/datastructures/CostHelper.h"

// Debug macros
#ifdef BITSTAR_DEBUG
    // Debug setting. The id number of a vertex to track. Requires BITSTAR_DEBUG to be defined in BITstar.h
    #define TRACK_VERTEX_ID 0

    /** \brief A helper function to print out every function called on vertex "TRACK_VERTEX_ID" that changes it */
    #define PRINT_VERTEX_CHANGE \
        if (vId_ == TRACK_VERTEX_ID) \
        { \
            std::cout << "vId " << vId_ << ": " << __func__ << "()" << std::endl; \
        }

    /** \brief A debug-only call to assert that the vertex is not pruned. */
    #define ASSERT_NOT_PRUNED this->assertNotPruned();
#else
    #define PRINT_VERTEX_CHANGE
    #define ASSERT_NOT_PRUNED
#endif  // BITSTAR_DEBUG

// An anonymous namespace to hide the instance:
namespace
{
    // Global variables:
    // The initialization flag stating that the ID generator has been created:
    std::once_flag g_IdInited;
    // A pointer to the actual ID generator
    boost::scoped_ptr<ompl::geometric::BITstar::IdGenerator> g_IdGenerator;

    // A function to initialize the ID generator pointer:
    void initIdGenerator()
    {
        g_IdGenerator.reset(new ompl::geometric::BITstar::IdGenerator());
    }

    // A function to get the current ID generator:
    ompl::geometric::BITstar::IdGenerator &getIdGenerator()
    {
        std::call_once(g_IdInited, &initIdGenerator);
        return *g_IdGenerator;
    }
}

namespace ompl
{
    namespace geometric
    {
        /////////////////////////////////////////////////////////////////////////////////////////////
        // Public functions:
        BITstar::Vertex::Vertex(ompl::base::SpaceInformationPtr si, const CostHelper *const costHelpPtr,
                                bool root /*= false*/)
          : vId_(getIdGenerator().getNewId())
          , si_(std::move(si))
          , costHelpPtr_(std::move(costHelpPtr))
          , state_(si_->allocState())
          , isRoot_(root)
          , edgeCost_(costHelpPtr_->infiniteCost())
          , cost_(costHelpPtr_->infiniteCost())
        {
            PRINT_VERTEX_CHANGE

            if (this->isRoot())
            {
                cost_ = costHelpPtr_->identityCost();
            }
            // No else, infinite by default
        }

        BITstar::Vertex::~Vertex()
        {
            PRINT_VERTEX_CHANGE

            // Free the state on destruction
            si_->freeState(state_);
        }

        BITstar::VertexId BITstar::Vertex::getId() const
        {
            ASSERT_NOT_PRUNED

            return vId_;
        }

        ompl::base::State const *BITstar::Vertex::state() const
        {
            ASSERT_NOT_PRUNED

            return state_;
        }

        ompl::base::State *BITstar::Vertex::state()
        {
            PRINT_VERTEX_CHANGE
            ASSERT_NOT_PRUNED

            return state_;
        }

        /////////////////////////////////////////////
        // The vertex's graph properties:
        bool BITstar::Vertex::isRoot() const
        {
            ASSERT_NOT_PRUNED

            return isRoot_;
        }

        bool BITstar::Vertex::hasParent() const
        {
            ASSERT_NOT_PRUNED

            return static_cast<bool>(parentPtr_);
        }

        bool BITstar::Vertex::isInTree() const
        {
            ASSERT_NOT_PRUNED

            return this->isRoot() || this->hasParent();
        }

        unsigned int BITstar::Vertex::getDepth() const
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            if (!this->isRoot() && !this->hasParent())
            {
                throw ompl::Exception("Attempting to get the depth of a vertex that does not have a parent yet is not "
                                      "root.");
            }
#endif  // BITSTAR_DEBUG

            return depth_;
        }

        BITstar::VertexConstPtr BITstar::Vertex::getParent() const
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            if (!this->hasParent())
            {
                if (this->isRoot())
                {
                    throw ompl::Exception("Attempting to access the parent of the root vertex.");
                }
                else
                {
                    throw ompl::Exception("Attempting to access the parent of a vertex that does not have one.");
                }
            }
#endif  // BITSTAR_DEBUG

            return parentPtr_;
        }

        BITstar::VertexPtr BITstar::Vertex::getParent()
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            if (!this->hasParent())
            {
                if (this->isRoot())
                {
                    throw ompl::Exception("Attempting to access the parent of the root vertex.");
                }
                else
                {
                    throw ompl::Exception("Attempting to access the parent of a vertex that does not have one.");
                }
            }
#endif  // BITSTAR_DEBUG

            return parentPtr_;
        }

        void BITstar::Vertex::addParent(const VertexPtr &newParent, const ompl::base::Cost &edgeInCost)
        {
            PRINT_VERTEX_CHANGE
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert I can take a parent
            if (this->isRoot())
            {
                throw ompl::Exception("Attempting to add a parent to the root vertex, which cannot have a parent.");
            }
            if (this->hasParent())
            {
                throw ompl::Exception("Attempting to add a parent to a vertex that already has one.");
            }
#endif  // BITSTAR_DEBUG

            // Store the parent.
            parentPtr_ = newParent;

            // Store the edge cost.
            edgeCost_ = edgeInCost;

            // Update my cost and that of my children.
            this->updateCostAndDepth(true);
        }

        void BITstar::Vertex::removeParent(bool updateChildCosts)
        {
            PRINT_VERTEX_CHANGE
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert I have a parent
            if (this->isRoot())
            {
                throw ompl::Exception("Attempting to remove the parent of the root vertex, which cannot have a "
                                      "parent.");
            }
            if (!this->hasParent())
            {
                throw ompl::Exception("Attempting to remove the parent of a vertex that does not have a parent.");
            }
#endif  // BITSTAR_DEBUG

            // Clear my parent
            parentPtr_.reset();

            // Update my cost and possibly the cost of my descendants:
            this->updateCostAndDepth(updateChildCosts);
        }

        bool BITstar::Vertex::hasChildren() const
        {
            ASSERT_NOT_PRUNED

            return !childPtrs_.empty();
        }

        void BITstar::Vertex::getChildren(VertexConstPtrVector *children) const
        {
            ASSERT_NOT_PRUNED

            children->clear();

            for (const auto &childWPtr : childPtrs_)
            {
#ifdef BITSTAR_DEBUG
                // Check that the weak pointer hasn't expired
                if (childWPtr.expired())
                {
                    throw ompl::Exception("A (weak) pointer to a child was found to have expired while collecting the "
                                          "children of a vertex.");
                }
#endif  // BITSTAR_DEBUG

                // Lock and push back
                children->push_back(childWPtr.lock());
            }
        }

        void BITstar::Vertex::getChildren(VertexPtrVector *children)
        {
            ASSERT_NOT_PRUNED

            children->clear();

            for (const auto &childWPtr : childPtrs_)
            {
#ifdef BITSTAR_DEBUG
                // Check that the weak pointer hasn't expired
                if (childWPtr.expired())
                {
                    throw ompl::Exception("A (weak) pointer to a child was found to have expired while collecting the "
                                          "children of a vertex.");
                }
#endif  // BITSTAR_DEBUG

                // Lock and push back
                children->push_back(childWPtr.lock());
            }
        }

        void BITstar::Vertex::addChild(const VertexPtr &newChild)
        {
            PRINT_VERTEX_CHANGE
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert that I am this child's parent
            if (newChild->isRoot())
            {
                throw ompl::Exception("Attempted to add a root vertex as a child.");
            }
            if (!newChild->hasParent())
            {
                throw ompl::Exception("Attempted to add child that does not have a listed parent.");
            }
            if (newChild->getParent()->getId() != vId_)
            {
                throw ompl::Exception("Attempted to add someone else's child as mine.");
            }
#endif  // BITSTAR_DEBUG

            // Push back the shared_ptr into the vector of weak_ptrs, this makes a weak_ptr copy
            childPtrs_.push_back(newChild);

            // Leave the costs of the child out of date.
        }

        void BITstar::Vertex::removeChild(const VertexPtr &oldChild)
        {
            PRINT_VERTEX_CHANGE
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert that I am this child's parent
            if (oldChild->isRoot())
            {
                throw ompl::Exception("Attempted to remove a root vertex as a child.");
            }
            if (!oldChild->hasParent())
            {
                throw ompl::Exception("Attempted to remove a child that does not have a listed parent.");
            }
            if (oldChild->getParent()->getId() != vId_)
            {
                throw ompl::Exception("Attempted to remove a child vertex from the wrong parent.");
            }
#endif  // BITSTAR_DEBUG

            // Variables
            // Whether the child has been found (and then deleted);
            bool foundChild;

            // Iterate over the vector of children pointers until the child is found. Iterators make erase easier
            foundChild = false;
            for (auto childIter = childPtrs_.begin(); childIter != childPtrs_.end() && !foundChild;
                 ++childIter)
            {
#ifdef BITSTAR_DEBUG
                // Check that the weak pointer hasn't expired
                if (childIter->expired())
                {
                    throw ompl::Exception("A (weak) pointer to a child was found to have expired while removing a "
                                          "child from a vertex.");
                }
#endif  // BITSTAR_DEBUG

                // Check if this is the child we're looking for
                if (childIter->lock()->getId() == oldChild->getId())
                {
                    // It is, mark as found
                    foundChild = true;

                    // First, clear the entry in the vector
                    childIter->reset();

                    // Then remove that entry from the vector efficiently
                    swapPopBack(childIter, &childPtrs_);
                }
                // No else, move on
            }

            // Leave the costs of the child out of date.

#ifdef BITSTAR_DEBUG
            // Throw if we did not find the child
            if (!foundChild)
            {
                throw ompl::Exception("Attempting to remove a child vertex not present in the vector of children "
                                      "stored in the (supposed) parent vertex.");
            }
#endif  // BITSTAR_DEBUG
        }

        void BITstar::Vertex::blacklistChild(const VertexConstPtr &vertex)
        {
            childIdBlacklist_.emplace(vertex->getId());
        }

        void BITstar::Vertex::whitelistChild(const VertexConstPtr &vertex)
        {
            childIdWhitelist_.emplace(vertex->getId());
        }

        bool BITstar::Vertex::isBlacklistedAsChild(const VertexConstPtr &vertex) const
        {
            return childIdBlacklist_.find(vertex->getId()) != childIdBlacklist_.end();
        }

        bool BITstar::Vertex::isWhitelistedAsChild(const VertexConstPtr &vertex) const
        {
            return childIdWhitelist_.find(vertex->getId()) != childIdWhitelist_.end();
        }

        ompl::base::Cost BITstar::Vertex::getCost() const
        {
            ASSERT_NOT_PRUNED

            return cost_;
        }

        ompl::base::Cost BITstar::Vertex::getEdgeInCost() const
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            if (!this->hasParent())
            {
                throw ompl::Exception("Attempting to access the incoming-edge cost of a vertex without a parent.");
            }
#endif  // BITSTAR_DEBUG

            return edgeCost_;
        }

        bool BITstar::Vertex::isPruned() const
        {
            return isPruned_;
        }

        void BITstar::Vertex::markPruned()
        {
            PRINT_VERTEX_CHANGE
            ASSERT_NOT_PRUNED

            isPruned_ = true;
        }

        void BITstar::Vertex::markUnpruned()
        {
            PRINT_VERTEX_CHANGE

            isPruned_ = false;
        }
        /////////////////////////////////////////////

        /////////////////////////////////////////////
        // Functions for the vertex's SearchQueue data
        /////////////////////////
        // Vertex queue info:
        BITstar::SearchQueue::VertexQueueIter BITstar::Vertex::getVertexQueueIter() const
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert available
            if (!hasVertexQueueIter_)
            {
                throw ompl::Exception("Attempting to access an iterator to the vertex queue before one is set.");
            }
#endif  // BITSTAR_DEBUG

            return vertexQueueIter_;
        }

        void BITstar::Vertex::setVertexQueueIter(const SearchQueue::VertexQueueIter& newPtr)
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert not already set
            if (hasVertexQueueIter_)
            {
                throw ompl::Exception("Attempting to change an iterator to the vertex queue.");
            }
#endif  // BITSTAR_DEBUG

            // Record that it's set
            hasVertexQueueIter_ = true;

            // Store
            vertexQueueIter_ = newPtr;
        }

        void BITstar::Vertex::clearVertexQueueIter()
        {
            ASSERT_NOT_PRUNED

            // Reset to unset
            hasVertexQueueIter_ = false;
        }

        bool BITstar::Vertex::hasVertexQueueEntry() const
        {
            ASSERT_NOT_PRUNED

            return hasVertexQueueIter_;
        }
        /////////////////////////

        /////////////////////////
        // Edge queue info (incoming edges):
        void BITstar::Vertex::insertInEdgeQueueInLookup(const SearchQueue::EdgeQueueElemPtr& newInPtr, unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

            // Conditionally clear any existing lookups
            this->clearLookupsIfOutdated(vertexQueueResetNum);

#ifdef BITSTAR_DEBUG
            // Assert that this edge is NOT _from_ this vertex
            if (newInPtr->data.second.first->getId() == vId_)
            {
                throw ompl::Exception("Attempted to add a cyclic incoming queue edge.");
            }
            // Assert that this edge is _to_ this vertex
            if (newInPtr->data.second.second->getId() != vId_)
            {
                throw ompl::Exception("Attempted to add an incoming queue edge to the wrong vertex.");
            }
            // Assert that an edge from this source does not already exist
            for (const auto &elemPtrs : edgeQueueInLookup_)
            {
                if (newInPtr->data.second.first->getId() == elemPtrs->data.second.first->getId())
                {
                    throw ompl::Exception("Attempted to add a second edge to the queue from a single source vertex.");
                }
            }
#endif  // BITSTAR_DEBUG

            // Push back
            edgeQueueInLookup_.push_back(newInPtr);
        }

        void BITstar::Vertex::removeFromEdgeQueueInLookup(const SearchQueue::EdgeQueueElemPtr &elemToDelete, unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert that the edge queue entries we have are of the same set as the one we're seeking to delete.
            // If so, there's no point clearing them, as then we'd be trying to remove an edge that doesn't exist which would be an error.
            if (vertexQueueResetNum != vertexQueueResetNum_)
            {
                throw ompl::Exception("Attempted to remove an incoming queue edge added under a different expansion id.");
            }
#endif  // BITSTAR_DEBUG

            // Variable
            // Element found
            bool found = false;

            // Iterate through the list and find the address of the element to delete
            for (auto iterToDelete = edgeQueueInLookup_.begin(); iterToDelete != edgeQueueInLookup_.end() && !found; ++iterToDelete)
            {
                // Is it the element we're looking for? Source id
                if ((*iterToDelete)->data.second.first->getId() == elemToDelete->data.second.first->getId())
                {
                    // Remove by iterator
                    this->removeFromEdgeQueueInLookup(iterToDelete);

                    // Mark as found
                    found = true;
                }
                // No else, try the next
            }

#ifdef BITSTAR_DEBUG
            if (!found)
            {
                throw ompl::Exception("Attempted to remove an edge not in the incoming lookup.");
            }
#endif  // BITSTAR_DEBUG
        }

        void BITstar::Vertex::removeFromEdgeQueueInLookup(const SearchQueue::EdgeQueueElemPtrVector::const_iterator& constIterToDelete, unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert that the edge queue entries we have are of the same set as the one we're seeking to delete.
            // If so, there's no point clearing them, as then we'd be trying to remove an edge that doesn't exist which would be an error.
            if (vertexQueueResetNum != vertexQueueResetNum_)
            {
                throw ompl::Exception("Attempted to remove an incoming queue edge added under a different expansion id.");
            }
#endif  // BITSTAR_DEBUG

            // Remove a non-const version of the given iterator
            // (trick from https://stackoverflow.com/a/10669041/1442500)
            this->removeFromEdgeQueueInLookup(edgeQueueInLookup_.erase(constIterToDelete, constIterToDelete));
        }

        void BITstar::Vertex::clearEdgeQueueInLookup()
        {
            ASSERT_NOT_PRUNED

            edgeQueueInLookup_.clear();
        }

        BITstar::SearchQueue::EdgeQueueElemPtrVector::const_iterator BITstar::Vertex::edgeQueueInLookupConstBegin(unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

            // Conditionally clear any existing lookups
            this->clearLookupsIfOutdated(vertexQueueResetNum);

            return edgeQueueInLookup_.cbegin();
        }

        BITstar::SearchQueue::EdgeQueueElemPtrVector::const_iterator BITstar::Vertex::edgeQueueInLookupConstEnd(unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

            // Conditionally clear any existing lookups
            this->clearLookupsIfOutdated(vertexQueueResetNum);

            return edgeQueueInLookup_.cend();
        }

        unsigned int BITstar::Vertex::edgeQueueInLookupSize(unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

            // Conditionally clear any existing lookups
            this->clearLookupsIfOutdated(vertexQueueResetNum);

            return edgeQueueInLookup_.size();
        }
        /////////////////////////

        /////////////////////////
        // Edge queue info (outgoing edges):
        void BITstar::Vertex::insertInEdgeQueueOutLookup(const SearchQueue::EdgeQueueElemPtr& newOutPtr, unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

            // Conditionally clear any existing lookups
            this->clearLookupsIfOutdated(vertexQueueResetNum);

#ifdef BITSTAR_DEBUG
            // Assert that this edge is _from_ this vertex
            if (newOutPtr->data.second.first->getId() != vId_)
            {
                throw ompl::Exception("Attempted to add an outgoing queue edge to the wrong vertex.");
            }
            // Assert that this edge is NOT _to_ this vertex
            if (newOutPtr->data.second.second->getId() == vId_)
            {
                throw ompl::Exception("Attempted to add a cyclic outgoing queue edge.");
            }
            // Assert that an edge to this target does not already exist
            for (const auto &elemPtrs : edgeQueueOutLookup_)
            {
                if (newOutPtr->data.second.second->getId() == elemPtrs->data.second.second->getId())
                {
                    throw ompl::Exception("Attempted to add a second edge to the queue to a single target vertex.");
                }
            }
#endif  // BITSTAR_DEBUG

            // Push back
            edgeQueueOutLookup_.push_back(newOutPtr);
        }

        void BITstar::Vertex::removeFromEdgeQueueOutLookup(const SearchQueue::EdgeQueueElemPtr &elemToDelete, unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert that the edge queue entries we have are of the same set as the one we're seeking to delete.
            // If so, there's no point clearing them, as then we'd be trying to remove an edge that doesn't exist which would be an error.
            if (vertexQueueResetNum != vertexQueueResetNum_)
            {
                throw ompl::Exception("Attempted to remove an incoming queue edge added under a different expansion id.");
            }
#endif  // BITSTAR_DEBUG

            // Variable
            // Element found
            bool found = false;

            // Iterate through the list and find the address of the element to delete
            for (auto iterToDelete = edgeQueueOutLookup_.begin(); iterToDelete != edgeQueueOutLookup_.end() && !found; ++iterToDelete)
            {
                // Is it the element we're looking for? Source id
                if ((*iterToDelete)->data.second.second->getId() == elemToDelete->data.second.second->getId())
                {
                    // Remove by iterator
                    this->removeFromEdgeQueueOutLookup(iterToDelete);

                    // Mark as found
                    found = true;
                }
                // No else, try the next
            }

#ifdef BITSTAR_DEBUG
            if (!found)
            {
                throw ompl::Exception("Attempted to remove an edge not in the outgoing lookup.");
            }
#endif  // BITSTAR_DEBUG
        }

        void BITstar::Vertex::removeFromEdgeQueueOutLookup(const SearchQueue::EdgeQueueElemPtrVector::const_iterator& constIterToDelete, unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

#ifdef BITSTAR_DEBUG
            // Assert that the edge queue entries we have are of the same set as the one we're seeking to delete.
            // If so, there's no point clearing them, as then we'd be trying to remove an edge that doesn't exist which would be an error.
            if (vertexQueueResetNum != vertexQueueResetNum_)
            {
                throw ompl::Exception("Attempted to remove an outgoing queue edge added under a different expansion id.");
            }
#endif  // BITSTAR_DEBUG

            // Remove a non-const version of the given iterator
            // (trick from https://stackoverflow.com/a/10669041/1442500)
            this->removeFromEdgeQueueOutLookup(edgeQueueOutLookup_.erase(constIterToDelete, constIterToDelete));
        }

        void BITstar::Vertex::clearEdgeQueueOutLookup()
        {
            ASSERT_NOT_PRUNED

            edgeQueueOutLookup_.clear();
        }

        BITstar::SearchQueue::EdgeQueueElemPtrVector::const_iterator BITstar::Vertex::edgeQueueOutLookupConstBegin(unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

            // Conditionally clear any existing lookups
            this->clearLookupsIfOutdated(vertexQueueResetNum);

            return edgeQueueOutLookup_.cbegin();
        }

        BITstar::SearchQueue::EdgeQueueElemPtrVector::const_iterator BITstar::Vertex::edgeQueueOutLookupConstEnd(unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

            // Conditionally clear any existing lookups
            this->clearLookupsIfOutdated(vertexQueueResetNum);

            return edgeQueueOutLookup_.cend();
        }

        unsigned int BITstar::Vertex::edgeQueueOutLookupSize(unsigned int vertexQueueResetNum)
        {
            ASSERT_NOT_PRUNED

            // Conditionally clear any existing lookups
            this->clearLookupsIfOutdated(vertexQueueResetNum);

            return edgeQueueOutLookup_.size();
        }
        /////////////////////////
        /////////////////////////////////////////////
        /////////////////////////////////////////////////////////////////////////////////////////////

        /////////////////////////////////////////////////////////////////////////////////////////////
        // Protected functions:
        void BITstar::Vertex::updateCostAndDepth(bool cascadeUpdates /*= true*/)
        {
            PRINT_VERTEX_CHANGE
            ASSERT_NOT_PRUNED

            if (this->isRoot())
            {
                // Am I root? -- I don't really know how this would ever be called, but ok.
                cost_ = costHelpPtr_->identityCost();
                depth_ = 0u;
            }
            else if (!this->hasParent())
            {
                // Am I disconnected?
                cost_ = costHelpPtr_->infiniteCost();

                // Set the depth to 0u, getDepth will throw in this condition
                depth_ = 0u;

#ifdef BITSTAR_DEBUG
                // Assert that I have not been asked to cascade this bad data to my children:
                if (this->hasChildren() && cascadeUpdates)
                {
                    throw ompl::Exception("Attempting to update descendants' costs and depths of a vertex that does "
                                          "not have a parent and is not root. This information would therefore be "
                                          "gibberish.");
                }
#endif  // BITSTAR_DEBUG
            }
            else
            {
                // I have a parent, so my cost is my parent cost + my edge cost to the parent
                cost_ = costHelpPtr_->combineCosts(parentPtr_->getCost(), edgeCost_);

                // I am one more than my parent's depth:
                depth_ = (parentPtr_->getDepth() + 1u);
            }

            // Am I updating my children?
            if (cascadeUpdates)
            {
                // Now, iterate over my vector of children and tell each one to update its own damn cost:
                for (auto &childWPtr : childPtrs_)
                {
#ifdef BITSTAR_DEBUG
                    // Check that it hasn't expired
                    if (childWPtr.expired())
                    {
                        throw ompl::Exception("A (weak) pointer to a child has was found to have expired while "
                                              "updating the costs and depths of descendant vertices.");
                    }
#endif  // BITSTAR_DEBUG

                    // Get a lock and tell the child to update:
                    childWPtr.lock()->updateCostAndDepth(true);
                }
            }
            // No else, do not update the children. I hope the caller knows what they're doing.
        }
        /////////////////////////////////////////////////////////////////////////////////////////////

        /////////////////////////////////////////////////////////////////////////////////////////////
        // Private functions:

        void BITstar::Vertex::removeFromEdgeQueueInLookup(const SearchQueue::EdgeQueueElemPtrVector::iterator &iterToDelete)
        {
#ifdef BITSTAR_DEBUG
            // Store the source id of the edge we're removing
            VertexId rmSrc = (*iterToDelete)->data.second.first->getId();
            // Assert that this edge is NOT _from_ this vertex
            if (rmSrc == vId_)
            {
                throw ompl::Exception("Attempted to remove a cyclic incoming queue edge.");
            }
            // Assert that this edge is _to_ this vertex
            if ((*iterToDelete)->data.second.second->getId() != vId_)
            {
                throw ompl::Exception("Attempted to remove an incoming queue edge from the wrong vertex.");
            }
            // Assert that it could exist
            if (edgeQueueInLookup_.empty())
            {
                throw ompl::Exception("Attempted to remove an incoming queue edge from a vertex with an empty list.");
            }
            // Assert that this edge actually exists
            bool found = false;
            for (SearchQueue::EdgeQueueElemPtrVector::iterator ptrIter = edgeQueueInLookup_.begin(); ptrIter != edgeQueueInLookup_.end() && !found; ++ptrIter)
            {
                found = ((*ptrIter)->data.second.first->getId() == rmSrc);
            }
            if (!found)
            {
                throw ompl::Exception("Attempted to remove an edge not in the incoming lookup.");
            }
#endif  // BITSTAR_DEBUG

            // Clear our entry in the list
            *iterToDelete = nullptr;

            // Remove it efficiently
            swapPopBack(iterToDelete, &edgeQueueInLookup_);

#ifdef BITSTAR_DEBUG
            // Assert that it's now gone.
            for (const auto &edgePtr : edgeQueueInLookup_)
            {
                if (edgePtr->data.second.first->getId() == rmSrc)
                {
                    throw ompl::Exception("Failed to remove the designated edge in the incoming lookup.");
                }
            }
#endif  // BITSTAR_DEBUG
        }

        void BITstar::Vertex::removeFromEdgeQueueOutLookup(const SearchQueue::EdgeQueueElemPtrVector::iterator &iterToDelete)
        {
#ifdef BITSTAR_DEBUG
            // Store the target id of the edge we're removing
            VertexId rmTrgt = (*iterToDelete)->data.second.second->getId();
            // Assert that this edge is _from_ this vertex
            if ((*iterToDelete)->data.second.first->getId() != vId_)
            {
                throw ompl::Exception("Attempted to remove an outgoing queue edge from the wrong vertex.");
            }
            // Assert that this edge is NOT _to_ this vertex
            if (rmTrgt == vId_)
            {
                throw ompl::Exception("Attempted to remove a cyclic outgoing queue edge.");
            }
            // Assert that it could exist
            if (edgeQueueOutLookup_.empty())
            {
                throw ompl::Exception("Attempted to remove an outgoing queue edge from a vertex with an empty list.");
            }
            // Assert that this edge actually exists
            bool found = false;
            for (SearchQueue::EdgeQueueElemPtrVector::iterator ptrIter = edgeQueueOutLookup_.begin(); ptrIter != edgeQueueOutLookup_.end() && !found; ++ptrIter)
            {
                found = ((*ptrIter)->data.second.second->getId() == rmTrgt);
            }
            if (!found)
            {
                throw ompl::Exception("Attempted to remove an edge not in the outgoing lookup.");
            }
#endif  // BITSTAR_DEBUG

            // Clear our entry in the list
            *iterToDelete = nullptr;

            // Remove it efficiently
            swapPopBack(iterToDelete, &edgeQueueOutLookup_);

#ifdef BITSTAR_DEBUG
            // Assert that it's now gone.
            for (const auto &edgePtr : edgeQueueOutLookup_)
            {
                if (edgePtr->data.second.second->getId() == rmTrgt)
                {
                    throw ompl::Exception("Failed to remove the designated edge in the outgoing lookup.");
                }
            }
#endif  // BITSTAR_DEBUG
        }

        void BITstar::Vertex::clearLookupsIfOutdated(unsigned int vertexQueueResetNum)
        {
            // Clean up any old lookups
            if (vertexQueueResetNum != vertexQueueResetNum_)
            {
                // Clear the existing entries
                this->clearEdgeQueueInLookup();
                this->clearEdgeQueueOutLookup();

                // Update the counter
                vertexQueueResetNum_ = vertexQueueResetNum;
            }
            // No else, this is the same pass through the vertex queue
        }

        void BITstar::Vertex::assertNotPruned() const
        {
            if (isPruned_)
            {
                std::cout << std::endl << "vId: " << vId_ << std::endl;
                throw ompl::Exception("Attempting to access a pruned vertex.");
            }
        }
        /////////////////////////////////////////////////////////////////////////////////////////////
    }  // geometric
}  // ompl
