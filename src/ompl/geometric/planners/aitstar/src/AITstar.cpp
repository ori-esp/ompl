/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014, University of Oxford
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

// Authors: Marlin Strub
#include <algorithm>
#include <cmath>
#include <string>

#include <iostream>

#include <boost/range/adaptor/reversed.hpp>

#include "ompl/base/objectives/PathLengthOptimizationObjective.h"
#include "ompl/geometric/PathGeometric.h"
#include "ompl/geometric/planners/aitstar/AITstar.h"
#include "ompl/util/Console.h"

using namespace std::string_literals;

namespace ompl
{
    namespace geometric
    {
        AITstar::AITstar(const ompl::base::SpaceInformationPtr &spaceInformation)
          : ompl::base::Planner(spaceInformation, "AITstar")
          , forwardSearchId_(std::make_shared<std::size_t>(1u))
          , backwardSearchId_(std::make_shared<std::size_t>(1u))
          , solutionCost_()
        {
        }

        void AITstar::setup()
        {
            // Call the base-class setup.
            Planner::setup();

            // Check that a problem definition has been set.
            if (static_cast<bool>(Planner::pdef_))
            {
                // If we were given a goal, make sure its of appropriate type.
                if (!(pdef_->getGoal()->hasType(ompl::base::GOAL_SAMPLEABLE_REGION)))
                {
                    OMPL_ERROR("AIT* is currently only implemented for goals that can be cast to "
                               "ompl::base::GOAL_SAMPLEABLE_GOAL_REGION.");
                    setup_ = false;
                    return;
                }

                // Default to path length optimization objective if none has been specified.
                if (!pdef_->hasOptimizationObjective())
                {
                    OMPL_WARN("%s: No optimization objective has been specified. Defaulting to path length.",
                              Planner::getName().c_str());
                    Planner::pdef_->setOptimizationObjective(
                        std::make_shared<ompl::base::PathLengthOptimizationObjective>(Planner::si_));
                }

                // Pull the optimization objective through the problem definition.
                objective_ = pdef_->getOptimizationObjective();

                // Initialize the solution cost to be infinite.
                solutionCost_ = std::make_shared<ompl::base::Cost>(objective_->infiniteCost());

                // Initialize the forward queue.
                forwardQueue_ = std::make_unique<EdgeQueue>([this](const aitstar::Edge &lhs, const aitstar::Edge &rhs) {
                    return std::lexicographical_compare(lhs.getSortKey().cbegin(), lhs.getSortKey().cend(),
                                                        rhs.getSortKey().cbegin(), rhs.getSortKey().cend(),
                                                        [this](const ompl::base::Cost &a, const ompl::base::Cost &b) {
                                                            return objective_->isCostBetterThan(a, b);
                                                        });
                });

                // Initialize the reverse queue.
                backwardQueue_ = std::make_unique<VertexQueue>(
                    [this](const std::pair<std::array<ompl::base::Cost, 2u>, std::shared_ptr<aitstar::Vertex>> &lhs,
                           const std::pair<std::array<ompl::base::Cost, 2u>, std::shared_ptr<aitstar::Vertex>> &rhs) {
                        return std::lexicographical_compare(
                            lhs.first.cbegin(), lhs.first.cend(), rhs.first.cbegin(), rhs.first.cend(),
                            [this](const ompl::base::Cost &a, const ompl::base::Cost &b) {
                                return objective_->isCostBetterThan(a, b);
                            });
                    });

                // Pull the motion validator through the space information.
                motionValidator_ = si_->getMotionValidator();

                // Setup a graph.
                graph_.setup(si_, pdef_, solutionCost_, forwardSearchId_, backwardSearchId_, &pis_);

                // Add the start states.
                while (pis_.haveMoreStartStates())
                {
                    // Get the next start state.
                    const ompl::base::State *startState = pis_.nextStart();

                    // Add it if it's valid.
                    if (static_cast<bool>(startState))
                    {
                        graph_.registerStartState(startState);
                    }
                }

                // Add the goal states.
                while (pis_.haveMoreGoalStates())
                {
                    const auto goalState = pis_.nextGoal();

                    // Add it if it's valid.
                    if (static_cast<bool>(goalState))
                    {
                        graph_.registerGoalState(goalState);
                    }
                }
            }
            else
            {
                // AIT* can't be setup without a problem definition.
                setup_ = false;
                OMPL_WARN("AIT*: Unable to setup without a problem definition.");
            }
        }

        ompl::base::PlannerStatus AITstar::solve(const ompl::base::PlannerTerminationCondition &terminationCondition)
        {
            // Ensure the planner is setup.
            Planner::checkValidity();
            if (!Planner::setup_)
            {
                OMPL_WARN("AIT*: Failed to setup and thus solve can not do anything meaningful.");
                return ompl::base::PlannerStatus::StatusType::ABORT;
            }

            if (!graph_.hasAStartState())
            {
                OMPL_WARN("AIT*: No solution can be found as no start states are available");
                return ompl::base::PlannerStatus::StatusType::INVALID_START;
            }

            // If the graph currently does not have a goal state, we must wait until we get one.
            if (!graph_.hasAGoalState())
            {
                graph_.updateStartAndGoalStates(terminationCondition, &pis_);
            }

            // If the graph still doesn't have a goal after waiting there's nothing to solve.
            if (!graph_.hasAGoalState())
            {
                OMPL_WARN("AIT*: No solution can be found as no start states are available");
                return ompl::base::PlannerStatus::StatusType::INVALID_GOAL;
            }

            OMPL_INFORM("%s: Searching for a solution to the given planning problem.", Planner::getName().c_str());

            // If this is the first time solve is called, populate the backward queue.
            if (numIterations_ == 0u)
            {
                for (const auto &goal : graph_.getGoalVertices())
                {
                    // Set the cost to come from the goal to identity cost.
                    goal->setCostToComeFromGoal(objective_->identityCost());

                    // Create an element for the queue.
                    std::pair<std::array<ompl::base::Cost, 2u>, std::shared_ptr<aitstar::Vertex>> element(
                        std::array<ompl::base::Cost, 2u>(
                            {computeCostToGoToStartHeuristic(goal), ompl::base::Cost(0.0)}),
                        goal);

                    // Insert the element into the queue and set the corresponding pointer.
                    auto backwardQueuePointer = backwardQueue_->insert(element);
                    goal->setBackwardQueuePointer(backwardQueuePointer);
                }
            }

            // Iterate to solve the problem.
            while (!terminationCondition && !(objective_->isFinite(*solutionCost_) && stopOnFindingInitialSolution_))
            {
                iterate();
            }

            if (std::isfinite(solutionCost_->value()))
            {
                return ompl::base::PlannerStatus::StatusType::EXACT_SOLUTION;
            }
            else
            {
                return ompl::base::PlannerStatus::StatusType::TIMEOUT;
            }
        }

        ompl::base::Cost AITstar::bestCost() const
        {
            return *solutionCost_;
        }

        void AITstar::getPlannerData(base::PlannerData &data) const
        {
            // base::PlannerDataVertex takes a raw pointer to a state. I want to guarantee, that the state lives as long
            // as the program lives.
            static std::set<
                std::shared_ptr<aitstar::Vertex>,
                std::function<bool(const std::shared_ptr<aitstar::Vertex> &, const std::shared_ptr<aitstar::Vertex> &)>>
                liveStates([](const auto &lhs, const auto &rhs) { return lhs->getId() < rhs->getId(); });

            // Fill the planner progress properties.
            Planner::getPlannerData(data);

            // Get the vertices.
            auto vertices = graph_.getVertices();

            // Add the vertices and edges.
            for (const auto &vertex : vertices)
            {
                // Add the vertex to the live states.
                liveStates.insert(vertex);

                // Add the vertex as the right kind of vertex.
                if (graph_.isStart(vertex))
                {
                    data.addStartVertex(ompl::base::PlannerDataVertex(vertex->getState(), vertex->getId()));
                }
                else if (graph_.isGoal(vertex))
                {
                    data.addGoalVertex(ompl::base::PlannerDataVertex(vertex->getState(), vertex->getId()));
                }
                else
                {
                    data.addVertex(ompl::base::PlannerDataVertex(vertex->getState(), vertex->getId()));
                }

                // If it has a parent, add the corresponding edge.
                if (vertex->hasForwardParent())
                {
                    data.addEdge(ompl::base::PlannerDataVertex(vertex->getState(), vertex->getId()),
                                 ompl::base::PlannerDataVertex(vertex->getForwardParent()->getState(),
                                                               vertex->getForwardParent()->getId()));
                }
            }
        }

        void AITstar::setBatchSize(std::size_t batchSize)
        {
            batchSize_ = batchSize;
        }

        void AITstar::setRewireFactor(double rewireFactor)
        {
            graph_.setRewireFactor(rewireFactor);
        }

        void AITstar::enablePruning(bool prune)
        {
            isPruningEnabled_ = prune;
        }

        void AITstar::setRepairBackwardSearch(bool repairBackwardSearch)
        {
            repairBackwardSearch_ = repairBackwardSearch;
        }

        void AITstar::setStopOnFindingInitialSolution(bool stopOnFindingInitialSolution)
        {
            stopOnFindingInitialSolution_ = stopOnFindingInitialSolution;
        }

        void AITstar::rebuildForwardQueue()
        {
            std::vector<aitstar::Edge> edges;
            forwardQueue_->getContent(edges);
            for (const auto &edge : edges)
            {
                edge.getChild()->resetForwardQueueIncomingLookup();
                edge.getParent()->resetForwardQueueOutgoingLookup();
            }
            forwardQueue_->clear();

            if (haveAllVerticesBeenProcessed(edges))
            {
                for (auto &edge : edges)
                {
                    insertOrUpdateInForwardQueue(aitstar::Edge(edge.getParent(), edge.getChild(),
                                                               computeSortKey(edge.getParent(), edge.getChild())));
                }
            }
            else
            {
                edgesToBeInserted_ = edges;
                performBackwardSearchIteration_ = true;
            }
        }

        void AITstar::rebuildBackwardQueue()
        {
            std::vector<KeyVertexPair> content;
            backwardQueue_->getContent(content);
            backwardQueue_->clear();

            for (auto &vertex : content)
            {
                // Compute the sort key for the vertex queue.
                std::pair<std::array<ompl::base::Cost, 2u>, std::shared_ptr<aitstar::Vertex>> element(
                    computeSortKey(vertex.second), vertex.second);
                auto backwardQueuePointer = backwardQueue_->insert(element);
                element.second->setBackwardQueuePointer(backwardQueuePointer);
            }
        }

        std::vector<aitstar::Edge> AITstar::getEdgesInQueue() const
        {
            std::vector<aitstar::Edge> edges;
            forwardQueue_->getContent(edges);
            return edges;
        }

        std::vector<std::shared_ptr<aitstar::Vertex>> AITstar::getVerticesInQueue() const
        {
            // Get the content from the queue.
            std::vector<std::pair<std::array<ompl::base::Cost, 2u>, std::shared_ptr<aitstar::Vertex>>> content;
            backwardQueue_->getContent(content);

            // Return the vertices.
            std::vector<std::shared_ptr<aitstar::Vertex>> vertices;
            for (const auto &pair : content)
            {
                vertices.emplace_back(pair.second);
            }
            return vertices;
        }

        aitstar::Edge AITstar::getNextEdgeInQueue() const
        {
            if (!forwardQueue_->empty())
            {
                return forwardQueue_->top()->data;
            }

            return {};
        }

        std::shared_ptr<aitstar::Vertex> AITstar::getNextVertexInQueue() const
        {
            if (!backwardQueue_->empty())
            {
                return backwardQueue_->top()->data.second;
            }

            return {};
        }

        std::vector<std::shared_ptr<aitstar::Vertex>> AITstar::getVerticesInBackwardSearchTree() const
        {
            // Get all vertices from the graph.
            auto vertices = graph_.getVertices();

            // Erase the vertices that are not in the backward search tree.
            vertices.erase(std::remove_if(vertices.begin(), vertices.end(),
                                          [this](const std::shared_ptr<aitstar::Vertex> &vertex) {
                                              return !graph_.isGoal(vertex) && !vertex->hasBackwardParent();
                                          }),
                           vertices.end());
            return vertices;
        }

        void AITstar::iterate()
        {
            // Keep track of the number of iterations.
            ++numIterations_;

            // If there are vertices in the backward search queue, process one of them.
            if (performBackwardSearchIteration_)
            {
                if (!backwardQueue_->empty())
                {
                    performBackwardSearchIteration();
                }
                else
                {
                    for (const auto &edge : edgesToBeInserted_)
                    {
                        if (haveAllVerticesBeenProcessed(edge))
                        {
                            insertOrUpdateInForwardQueue(aitstar::Edge(
                                edge.getParent(), edge.getChild(), computeSortKey(edge.getParent(), edge.getChild())));
                        }
                    }
                    edgesToBeInserted_.clear();
                    performBackwardSearchIteration_ = false;
                }
            }
            else
            {
                if (!isForwardSearchStartedOnBatch_)
                {
                    // Remember that we've started the forward search on this batch.
                    isForwardSearchStartedOnBatch_ = true;

                    // If no start vertex has finite cost to come from the goal, there is no need to start the
                    // forward search.
                    std::vector<aitstar::Edge> outgoingStartEdges;
                    for (const auto &start : graph_.getStartVertices())
                    {
                        if (objective_->isFinite(start->getCostToComeFromGoal()))
                        {
                            // Add the outgoing edges of all start vertices to the queue.
                            for (const auto &start : graph_.getStartVertices())
                            {
                                const auto outgoingEdges = getOutgoingEdges(start);
                                outgoingStartEdges.insert(outgoingStartEdges.end(), outgoingEdges.begin(),
                                                          outgoingEdges.end());
                            }
                        }
                    }
                    if (haveAllVerticesBeenProcessed(outgoingStartEdges))
                    {
                        for (const auto &edge : outgoingStartEdges)
                        {
                            insertOrUpdateInForwardQueue(edge);
                        }
                    }
                    else
                    {
                        assert(edgesToBeInserted_.empty());
                        edgesToBeInserted_ = outgoingStartEdges;
                        performBackwardSearchIteration_ = true;
                    }
                }
                else if (forwardQueueMustBeRebuilt_)
                {
                    rebuildForwardQueue();
                    forwardQueueMustBeRebuilt_ = false;
                }
                else if (!forwardQueue_->empty())
                {
                    performForwardSearchIteration();
                }
                else  // If both queues are empty, add new samples.
                {
                    // Clear the backward queue.
                    std::vector<std::pair<std::array<ompl::base::Cost, 2u>, std::shared_ptr<aitstar::Vertex>>>
                        backwardQueue;
                    backwardQueue_->getContent(backwardQueue);
                    for (const auto &element : backwardQueue)
                    {
                        element.second->resetBackwardQueuePointer();
                    }
                    backwardQueue_->clear();

                    // This constitutes a new backward search.
                    ++(*backwardSearchId_);

                    // Clear the forward queue.
                    std::vector<aitstar::Edge> forwardQueue;
                    forwardQueue_->getContent(forwardQueue);
                    for (const auto &element : forwardQueue)
                    {
                        element.getChild()->resetForwardQueueIncomingLookup();
                        element.getParent()->resetForwardQueueOutgoingLookup();
                    }
                    forwardQueue_->clear();
                    ++(*forwardSearchId_);

                    // Clear the cache of edges to be inserted.
                    edgesToBeInserted_.clear();

                    // Add new start and goal states if necessary.
                    if (pis_.haveMoreStartStates() || pis_.haveMoreGoalStates())
                    {
                        graph_.updateStartAndGoalStates(ompl::base::plannerAlwaysTerminatingCondition(), &pis_);
                    }

                    // Remove useless samples from the graph.
                    if (isPruningEnabled_)
                    {
                        graph_.prune();
                    }

                    // Add new samples to the graph.
                    auto newVertices = graph_.addSamples(batchSize_);

                    // Add the goals to the backward queue.
                    for (const auto &goal : graph_.getGoalVertices())
                    {
                        auto backwardQueuePointer = backwardQueue_->insert(std::make_pair(computeSortKey(goal), goal));
                        goal->setBackwardQueuePointer(backwardQueuePointer);
                        goal->setCostToComeFromGoal(objective_->identityCost());
                    }

                    // This is a new batch, so the searches haven't been started.
                    isForwardSearchStartedOnBatch_ = false;

                    // We will start with a backward iteration.
                    performBackwardSearchIteration_ = true;
                }
            }
        }

        void AITstar::performForwardSearchIteration()
        {
            assert(edgesToBeInserted_.empty());
            // Get the most promising edge.
            auto &edge = forwardQueue_->top()->data;
            auto parent = edge.getParent();
            auto child = edge.getChild();
            assert(child->hasBackwardParent() || graph_.isGoal(child));
            assert(parent->hasBackwardParent() || graph_.isGoal(parent));
            child->removeFromForwardQueueIncomingLookup(forwardQueue_->top());
            parent->removeFromForwardQueueOutgoingLookup(forwardQueue_->top());
            forwardQueue_->pop();

            // If this is edge can not possibly improve our solution, the search is done.
            auto edgeCost = objective_->motionCostHeuristic(parent->getState(), child->getState());
            auto parentCostToGoToGoal = objective_->combineCosts(edgeCost, child->getCostToGoToGoal());
            auto pathThroughEdgeCost = objective_->combineCosts(parent->getCostToComeFromStart(), parentCostToGoToGoal);

            if (!objective_->isCostBetterThan(pathThroughEdgeCost, *solutionCost_))
            {
                if (objective_->isFinite(pathThroughEdgeCost) ||
                    !objective_->isFinite(computeBestCostToComeFromGoalOfAnyStart()))
                {
                    std::vector<aitstar::Edge> edges;
                    forwardQueue_->getContent(edges);
                    for (const auto &edge : edges)
                    {
                        edge.getChild()->resetForwardQueueIncomingLookup();
                        edge.getParent()->resetForwardQueueOutgoingLookup();
                    }
                    forwardQueue_->clear();
                }
                else
                {
                    performBackwardSearchIteration_ = true;
                }
            }
            else if (child->hasForwardParent() && child->getForwardParent()->getId() == parent->getId())
            {
                // This is a freebie, just insert the outgoing edges of the child.
                if (!child->hasBeenExpandedDuringCurrentForwardSearch())
                {
                    auto edges = getOutgoingEdges(child);
                    if (haveAllVerticesBeenProcessed(edges))
                    {
                        for (const auto &edge : edges)
                        {
                            insertOrUpdateInForwardQueue(edge);
                        }
                    }
                    else
                    {
                        edgesToBeInserted_ = edges;
                        performBackwardSearchIteration_ = true;
                        return;
                    }
                }
            }
            else if (objective_->isCostBetterThan(child->getCostToComeFromStart(),
                                                  objective_->combineCosts(parent->getCostToComeFromStart(),
                                                                           objective_->motionCostHeuristic(
                                                                               parent->getState(), child->getState()))))
            {
                // If the edge cannot improve the cost to come to the child, we're done processing it.
                return;
            }
            else if (parent->isWhitelistedAsChild(child) ||
                     motionValidator_->checkMotion(parent->getState(), child->getState()))
            {
                // Remember that this is a good edge.
                parent->whitelistAsChild(child);

                // Compute the edge cost.
                auto edgeCost = objective_->motionCost(parent->getState(), child->getState());

                // Check if the edge can improve the cost to come to the child.
                if (objective_->isCostBetterThan(objective_->combineCosts(parent->getCostToComeFromStart(), edgeCost),
                                                 child->getCostToComeFromStart()))
                {
                    // It can, so we rewire the child.
                    child->setForwardParent(parent, edgeCost);

                    // Add it to the children of the parent.
                    parent->addToForwardChildren(child);

                    // Share the good news with the whole branch.
                    child->updateCostOfForwardBranch();

                    // Check if the solution can benefit from this.
                    updateSolution();

                    // Insert the child's outgoing edges into the queue, if it hasn't been expanded yet.
                    if (!child->hasBeenExpandedDuringCurrentForwardSearch())
                    {
                        auto edges = getOutgoingEdges(child);
                        if (haveAllVerticesBeenProcessed(edges))
                        {
                            for (const auto &edge : edges)
                            {
                                insertOrUpdateInForwardQueue(edge);
                            }
                        }
                        else
                        {
                            edgesToBeInserted_ = edges;
                            performBackwardSearchIteration_ = true;
                            return;
                        }
                    }
                }
            }
            else
            {
                // This child should be blacklisted.
                parent->blacklistAsChild(child);

                // If desired, now is the time to repair the backward search.
                if (repairBackwardSearch_)
                {
                    if (parent->hasBackwardParent() && parent->getBackwardParent()->getId() == child->getId())
                    {
                        // The parent was connected to the child through an invalid edge.
                        parent->setCostToComeFromGoal(objective_->infiniteCost());
                        parent->setExpandedCostToComeFromGoal(objective_->infiniteCost());
                        parent->resetBackwardParent();
                        child->removeFromBackwardChildren(parent->getId());

                        // This also affects all children of this vertex.
                        invalidateCostToComeFromGoalOfBackwardBranch(parent);

                        // If any of these children are in the backward queue, their sort key is outdated.
                        rebuildBackwardQueue();

                        // The parent's cost-to-come needs to be updated. This places children in open.
                        backwardSearchUpdateVertex(parent);

                        // If the reverse queue is empty, this means we have to add new samples.
                        if (backwardQueue_->empty())
                        {
                            std::vector<aitstar::Edge> edges;
                            forwardQueue_->getContent(edges);
                            for (const auto &edge : edges)
                            {
                                edge.getChild()->resetForwardQueueIncomingLookup();
                                edge.getParent()->resetForwardQueueOutgoingLookup();
                            }
                            forwardQueue_->clear();
                        }
                        else
                        {
                            performBackwardSearchIteration_ = true;
                        }
                    }
                }
            }
        }

        void AITstar::performBackwardSearchIteration()
        {
            assert(!backwardQueue_->empty());

            // Get the most promising vertex.
            auto vertex = backwardQueue_->top()->data.second;

            // Remove it from the queue.
            backwardQueue_->pop();
            vertex->resetBackwardQueuePointer();

            // The open queue should not contain consistent vertices.
            assert((!objective_->isFinite(vertex->getCostToComeFromGoal()) &&
                    !objective_->isFinite(vertex->getExpandedCostToComeFromGoal())) ||
                   (!objective_->isCostEquivalentTo(vertex->getCostToComeFromGoal(),
                                                    vertex->getExpandedCostToComeFromGoal())));

            // If any goal is underconsistent, we need to continue.
            bool underconsistentStart{false};
            for (const auto &start : graph_.getStartVertices())
            {
                if (objective_->isCostBetterThan(start->getExpandedCostToComeFromGoal(),
                                                 start->getCostToComeFromGoal()))
                {
                    underconsistentStart = true;
                    break;
                }
            }

            // If there is currently no reason to think this vertex can be on an optimal path, clear the queue.
            if (edgesToBeInserted_.empty() &&
                ((!underconsistentStart &&
                  !objective_->isCostBetterThan(objective_->combineCosts(vertex->getCostToComeFromGoal(),
                                                                         computeCostToGoToStartHeuristic(vertex)),
                                                *solutionCost_)) ||
                 objective_->isCostBetterThan(
                     ompl::base::Cost(computeBestCostToComeFromGoalOfAnyStart().value() + 1e-6), *solutionCost_)))
            {
                // This invalidates the cost-to-go estimate of the forward search.
                performBackwardSearchIteration_ = false;
                forwardQueueMustBeRebuilt_ = true;
                return;
            }

            // Check if the vertex is overconsistent. g(s) < v(s).
            if (objective_->isCostBetterThan(vertex->getCostToComeFromGoal(), vertex->getExpandedCostToComeFromGoal()))
            {
                // Register the expansion of this vertex.
                vertex->registerExpansionDuringBackwardSearch();
            }
            else
            {
                // Register the expansion of this vertex.
                vertex->registerExpansionDuringBackwardSearch();
                vertex->setExpandedCostToComeFromGoal(objective_->infiniteCost());
                backwardSearchUpdateVertex(vertex);
            }

            // Update all successors. Start with the backward search children, because if this vertex
            // becomes the parent of a neighbor, that neighbor would be updated again as part of the
            // backward children.
            for (const auto &child : vertex->getBackwardChildren())
            {
                backwardSearchUpdateVertex(child);
            }

            // We can now process the neighbors.
            for (const auto &neighbor : graph_.getNeighbors(vertex))
            {
                if (neighbor->getId() != vertex->getId() && !neighbor->isBlacklistedAsChild(vertex) &&
                    !vertex->isBlacklistedAsChild(neighbor))
                {
                    backwardSearchUpdateVertex(neighbor);
                }
            }

            // We also need to update the forward search children.
            for (const auto &child : vertex->getForwardChildren())
            {
                backwardSearchUpdateVertex(child);
            }

            // We also need to update the forward search parent if it exists.
            if (vertex->hasForwardParent())
            {
                backwardSearchUpdateVertex(vertex->getForwardParent());
            }

            if (!edgesToBeInserted_.empty())
            {
                if (haveAllVerticesBeenProcessed(edgesToBeInserted_))
                {
                    for (std::size_t i = 0u; i < edgesToBeInserted_.size(); ++i)
                    {
                        auto &edge = edgesToBeInserted_.at(i);
                        edge.setSortKey(computeSortKey(edge.getParent(), edge.getChild()));
                        insertOrUpdateInForwardQueue(edge);
                    }
                    edgesToBeInserted_.clear();
                }
            }
        }

        void AITstar::backwardSearchUpdateVertex(const std::shared_ptr<aitstar::Vertex> &vertex)
        {
            if (!graph_.isGoal(vertex))
            {
                // Get the best parent for this vertex.
                auto bestParent = vertex->getBackwardParent();
                auto bestCost =
                    vertex->hasBackwardParent() ? vertex->getCostToComeFromGoal() : objective_->infiniteCost();

                // Check all neighbors as defined by the graph.
                for (const auto &neighbor : graph_.getNeighbors(vertex))
                {
                    if (neighbor->getId() != vertex->getId() && !neighbor->isBlacklistedAsChild(vertex) &&
                        !vertex->isBlacklistedAsChild(neighbor))
                    {
                        auto edgeCost = objective_->motionCostHeuristic(neighbor->getState(), vertex->getState());
                        auto parentCost = objective_->combineCosts(neighbor->getExpandedCostToComeFromGoal(), edgeCost);
                        if (objective_->isCostBetterThan(parentCost, bestCost))
                        {
                            bestParent = neighbor;
                            bestCost = parentCost;
                        }
                    }
                }

                // Check all children this vertex holds in the forward search.
                for (const auto &forwardChild : vertex->getForwardChildren())
                {
                    auto edgeCost = objective_->motionCostHeuristic(forwardChild->getState(), vertex->getState());
                    auto parentCost = objective_->combineCosts(forwardChild->getExpandedCostToComeFromGoal(), edgeCost);

                    if (objective_->isCostBetterThan(parentCost, bestCost))
                    {
                        bestParent = forwardChild;
                        bestCost = parentCost;
                    }
                }

                // Check the parent of this vertex in the forward search.
                if (vertex->hasForwardParent())
                {
                    auto forwardParent = vertex->getForwardParent();
                    auto edgeCost = objective_->motionCostHeuristic(forwardParent->getState(), vertex->getState());
                    auto parentCost =
                        objective_->combineCosts(forwardParent->getExpandedCostToComeFromGoal(), edgeCost);

                    if (objective_->isCostBetterThan(parentCost, bestCost))
                    {
                        bestParent = forwardParent;
                        bestCost = parentCost;
                    }
                }

                // Check the parent of this vertex in the backward search.
                if (vertex->hasBackwardParent())
                {
                    auto backwardParent = vertex->getBackwardParent();
                    auto edgeCost = objective_->motionCostHeuristic(backwardParent->getState(), vertex->getState());
                    auto parentCost =
                        objective_->combineCosts(backwardParent->getExpandedCostToComeFromGoal(), edgeCost);

                    if (objective_->isCostBetterThan(parentCost, bestCost))
                    {
                        bestParent = backwardParent;
                        bestCost = parentCost;
                    }
                }

                // If this vertex is now disconnected, take special care.
                if (!objective_->isFinite(bestCost))
                {
                    // Reset the backward parent if the vertex has one.
                    if (vertex->hasBackwardParent())
                    {
                        vertex->getBackwardParent()->removeFromBackwardChildren(vertex->getId());
                        vertex->resetBackwardParent();
                    }

                    // Invalidate the branch in the reverse search tree that is rooted at this vertex.
                    vertex->setCostToComeFromGoal(objective_->infiniteCost());
                    vertex->setExpandedCostToComeFromGoal(objective_->infiniteCost());
                    auto affectedVertices = vertex->invalidateBackwardBranch();

                    // Remove the affected edges from the forward queue, placing them in the edge cache.
                    for (const auto &affectedVertex : affectedVertices)
                    {
                        auto forwardQueueIncomingLookup = affectedVertex.lock()->getForwardQueueIncomingLookup();
                        for (const auto &element : forwardQueueIncomingLookup)
                        {
                            edgesToBeInserted_.emplace_back(element->data);
                            forwardQueue_->remove(element);
                            element->data.getParent()->removeFromForwardQueueOutgoingLookup(element);
                        }
                        affectedVertex.lock()->resetForwardQueueIncomingLookup();

                        auto forwardQueueOutgoingLookup = affectedVertex.lock()->getForwardQueueOutgoingLookup();
                        for (const auto &element : forwardQueueOutgoingLookup)
                        {
                            edgesToBeInserted_.emplace_back(element->data);
                            forwardQueue_->remove(element);
                            element->data.getChild()->removeFromForwardQueueIncomingLookup(element);
                        }
                        affectedVertex.lock()->resetForwardQueueOutgoingLookup();
                    }

                    // Remove appropriate edges from the forward queue that target the root of the branch.
                    auto vertexForwardQueueIncomingLookup = vertex->getForwardQueueIncomingLookup();
                    for (const auto &element : vertexForwardQueueIncomingLookup)
                    {
                        auto &edge = element->data;
                        auto it = std::find_if(affectedVertices.begin(), affectedVertices.end(),
                                               [edge](const auto &affectedVertex) {
                                                   return affectedVertex.lock()->getId() == edge.getParent()->getId();
                                               });
                        if (it != affectedVertices.end())
                        {
                            edgesToBeInserted_.emplace_back(element->data);
                            vertex->removeFromForwardQueueIncomingLookup(element);
                            element->data.getParent()->removeFromForwardQueueOutgoingLookup(element);
                            forwardQueue_->remove(element);
                        }
                    }

                    // Remove appropriate edges from the forward queue that target the root of the branch.
                    auto vertexForwardQueueOutgoingLookup = vertex->getForwardQueueOutgoingLookup();
                    for (const auto &element : vertexForwardQueueOutgoingLookup)
                    {
                        edgesToBeInserted_.emplace_back(element->data);
                        vertex->removeFromForwardQueueOutgoingLookup(element);
                        element->data.getChild()->removeFromForwardQueueIncomingLookup(element);
                        forwardQueue_->remove(element);
                    }

                    // Check update the invalidated vertices and insert them in open if they become connected to the
                    // tree.
                    for (const auto &affectedVertex : affectedVertices)
                    {
                        auto affectedVertexPtr = affectedVertex.lock();

                        backwardSearchUpdateVertex(affectedVertexPtr);
                        if (affectedVertex.lock()->hasBackwardParent())
                        {
                            insertOrUpdateInBackwardQueue(affectedVertexPtr);
                            affectedVertexPtr->setExpandedCostToComeFromGoal(objective_->infiniteCost());
                        }
                    }

                    return;
                }

                // Update the backward parent.
                vertex->setBackwardParent(bestParent);

                // Update the children of the parent.
                bestParent->addToBackwardChildren(vertex);

                // Set the cost to come from the goal.
                vertex->setCostToComeFromGoal(bestCost);

                // If this has made the vertex inconsistent, insert or update it in the open queue.
                if (!objective_->isCostEquivalentTo(vertex->getCostToComeFromGoal(),
                                                    vertex->getExpandedCostToComeFromGoal()))
                {
                    insertOrUpdateInBackwardQueue(vertex);
                }
                else
                {
                    // Remove this vertex from the queue if it is in the queue.
                    auto backwardQueuePointer = vertex->getBackwardQueuePointer();
                    if (backwardQueuePointer)
                    {
                        backwardQueue_->remove(backwardQueuePointer);
                        vertex->resetBackwardQueuePointer();
                    }
                }
            }
        }

        void AITstar::insertOrUpdateInBackwardQueue(const std::shared_ptr<aitstar::Vertex> &vertex)
        {
            // Get the pointer to the element in the queue.
            auto element = vertex->getBackwardQueuePointer();

            // Update it if it is in the queue.
            if (element)
            {
                element->data.first = computeSortKey(vertex);
                backwardQueue_->update(element);
            }
            else  // Insert it into the queue otherwise.
            {
                // Compute the sort key for the vertex queue.
                std::pair<std::array<ompl::base::Cost, 2u>, std::shared_ptr<aitstar::Vertex>> element(
                    computeSortKey(vertex), vertex);

                // Insert the vertex into the queue, storing the corresponding pointer.
                auto backwardQueuePointer = backwardQueue_->insert(element);
                vertex->setBackwardQueuePointer(backwardQueuePointer);
            }
        }

        void AITstar::insertOrUpdateInForwardQueue(const aitstar::Edge &edge)
        {
            // Check if the edge is already in the queue and can be updated.
            auto lookup = edge.getChild()->getForwardQueueIncomingLookup();
            auto it = std::find_if(lookup.begin(), lookup.end(), [&edge](const auto element) {
                return element->data.getParent()->getId() == edge.getParent()->getId();
            });

            if (it != lookup.end())
            {
                assert(std::find_if(edge.getParent()->getForwardQueueOutgoingLookup().begin(),
                                    edge.getParent()->getForwardQueueOutgoingLookup().end(),
                                    [&edge](const auto element) {
                                        return element->data.getChild()->getId() == edge.getChild()->getId();
                                    }) != edge.getParent()->getForwardQueueOutgoingLookup().end());
                (*it)->data.setSortKey(edge.getSortKey());
                forwardQueue_->update(*it);
            }
            else
            {
                auto element = forwardQueue_->insert(edge);
                edge.getParent()->addToForwardQueueOutgoingLookup(element);
                edge.getChild()->addToForwardQueueIncomingLookup(element);
            }
        }

        std::vector<std::shared_ptr<aitstar::Vertex>>
        AITstar::getReversePath(const std::shared_ptr<aitstar::Vertex> &vertex) const
        {
            std::vector<std::shared_ptr<aitstar::Vertex>> reversePath;
            auto current = vertex;
            while (!graph_.isStart(current))
            {
                reversePath.emplace_back(current);
                current = current->getForwardParent();
            }
            reversePath.emplace_back(current);
            return reversePath;
        }

        std::array<ompl::base::Cost, 3u> AITstar::computeSortKey(const std::shared_ptr<aitstar::Vertex> &parent,
                                                                 const std::shared_ptr<aitstar::Vertex> &child) const
        {
            // Compute the sort key [g_T(start) + c_hat(start, neighbor) + h_hat(neighbor), g_T(start) +
            // c_hat(start, neighbor), g_T(start)].
            ompl::base::Cost edgeCostHeuristic = objective_->motionCostHeuristic(parent->getState(), child->getState());
            return {
                objective_->combineCosts(objective_->combineCosts(parent->getCostToComeFromStart(), edgeCostHeuristic),
                                         child->getCostToGoToGoal()),
                objective_->combineCosts(edgeCostHeuristic, child->getCostToGoToGoal()),
                parent->getCostToComeFromStart()};
        }

        std::array<ompl::base::Cost, 2u> AITstar::computeSortKey(const std::shared_ptr<aitstar::Vertex> &vertex) const
        {
            // LPA* sort key is [min(g(x), v(x)) + h(x); min(g(x), v(x))].
            return {objective_->combineCosts(objective_->betterCost(vertex->getCostToComeFromGoal(),
                                                                    vertex->getExpandedCostToComeFromGoal()),
                                             computeCostToGoToStartHeuristic(vertex)),
                    objective_->betterCost(vertex->getCostToComeFromGoal(), vertex->getExpandedCostToComeFromGoal())};
        }

        std::vector<aitstar::Edge> AITstar::getOutgoingEdges(const std::shared_ptr<aitstar::Vertex> &vertex) const
        {
            // Prepare the return variable.
            std::vector<aitstar::Edge> outgoingEdges;

            // Register that this vertex is expanded on the current search.
            vertex->registerExpansionDuringForwardSearch();

            // Insert the edges to the current children.
            for (const auto &child : vertex->getForwardChildren())
            {
                outgoingEdges.emplace_back(vertex, child, computeSortKey(vertex, child));
            }

            // Insert the edges to the current neighbors.
            for (const auto &neighbor : graph_.getNeighbors(vertex))
            {
                // We do not want self loops.
                if (vertex->getId() == neighbor->getId())
                {
                    continue;
                }

                // If the neighbor is the backward parent, it will explicitly be added later.
                if (vertex->hasBackwardParent() && neighbor->getId() == vertex->getBackwardParent()->getId())
                {
                    continue;
                }

                // We do not want blacklisted edges.
                if (neighbor->isBlacklistedAsChild(vertex) || vertex->isBlacklistedAsChild(neighbor))
                {
                    continue;
                }

                // If none of the above tests caught on, we can insert the edge.
                outgoingEdges.emplace_back(vertex, neighbor, computeSortKey(vertex, neighbor));
            }

            // Insert the edge to the backward search parent.
            if (vertex->hasBackwardParent())
            {
                const auto &backwardParent = vertex->getBackwardParent();
                outgoingEdges.emplace_back(vertex, backwardParent, computeSortKey(vertex, backwardParent));
            }

            return outgoingEdges;
        }

        bool AITstar::haveAllVerticesBeenProcessed(const std::vector<aitstar::Edge> &edges) const
        {
            // TODO: Think whether it is sufficient to test for backward parents.
            for (const auto &edge : edges)
            {
                if ((!edge.getChild()->hasBackwardParent() && !graph_.isGoal(edge.getChild())) ||
                    (!edge.getParent()->hasBackwardParent() && !graph_.isGoal(edge.getParent())))
                {
                    return false;
                }
            }

            return true;
        }

        bool AITstar::haveAllVerticesBeenProcessed(const aitstar::Edge &edge) const
        {
            // TODO: Think whether it is sufficient to test for backward parents.
            if ((!edge.getChild()->hasBackwardParent() && !graph_.isGoal(edge.getChild())) ||
                (!edge.getParent()->hasBackwardParent() && !graph_.isGoal(edge.getParent())))
            {
                return false;
            }

            return true;
        }

        void AITstar::updateSolution()
        {
            // Check if any of the goals have a cost to come less than the current solution cost.
            for (const auto &goal : graph_.getGoalVertices())
            {
                if (objective_->isCostBetterThan(goal->getCostToComeFromStart(), *solutionCost_))
                {
                    // Remember the incumbent cost.
                    *solutionCost_ = goal->getCostToComeFromStart();

                    // Create a path.
                    auto path = std::make_shared<ompl::geometric::PathGeometric>(Planner::si_);
                    auto reversePath = getReversePath(goal);
                    for (const auto &vertex : boost::adaptors::reverse(reversePath))
                    {
                        path->append(vertex->getState());
                    }

                    // Convert the path to a solution.
                    ompl::base::PlannerSolution solution(path);
                    solution.setPlannerName(Planner::name_);

                    // Set the optimized flag.
                    solution.optimized_ = objective_->isSatisfied(*solutionCost_);

                    // Let the problem definition know that a new solution exists.
                    Planner::pdef_->addSolutionPath(solution);
                }
            }
        }

        ompl::base::Cost AITstar::computeCostToGoToStartHeuristic(const std::shared_ptr<aitstar::Vertex> &vertex) const
        {
            // We need to loop over all start vertices and see which is the closest one.
            ompl::base::Cost bestCost = objective_->infiniteCost();
            for (const auto &start : graph_.getStartVertices())
            {
                bestCost = objective_->betterCost(
                    bestCost, objective_->motionCostHeuristic(vertex->getState(), start->getState()));
            }
            return bestCost;
        }

        ompl::base::Cost AITstar::computeCostToGoToGoalHeuristic(const std::shared_ptr<aitstar::Vertex> &vertex) const
        {
            // We need to loop over all goal vertices and see which is the closest one.
            ompl::base::Cost bestCost = objective_->infiniteCost();
            for (const auto &goal : graph_.getGoalVertices())
            {
                bestCost = objective_->betterCost(
                    bestCost, objective_->motionCostHeuristic(vertex->getState(), goal->getState()));
            }
            return bestCost;
        }

        ompl::base::Cost AITstar::computeBestCostToComeFromGoalOfAnyStart() const
        {
            // We need to loop over all start vertices and see which is the closest one.
            ompl::base::Cost bestCost = objective_->infiniteCost();
            for (const auto &start : graph_.getStartVertices())
            {
                bestCost = objective_->betterCost(bestCost, start->getCostToComeFromGoal());
            }
            return bestCost;
        }

        void AITstar::invalidateCostToComeFromGoalOfBackwardBranch(const std::shared_ptr<aitstar::Vertex> &vertex)
        {
            // Update the cost of all backward children and remove from open.
            for (const auto &child : vertex->getBackwardChildren())
            {
                invalidateCostToComeFromGoalOfBackwardBranch(child);
                child->setCostToComeFromGoal(objective_->infiniteCost());
                child->setExpandedCostToComeFromGoal(objective_->infiniteCost());
                auto backwardQueuePointer = child->getBackwardQueuePointer();
                if (backwardQueuePointer)
                {
                    backwardQueue_->remove(backwardQueuePointer);
                    child->resetBackwardQueuePointer();
                }
            }
        }

    }  // namespace geometric
}  // namespace ompl
