#ifndef RR_GRAPH_INTERPOSER_H
#define RR_GRAPH_INTERPOSER_H

#include "rr_graph_view.h"
#include "rr_graph_builder.h"
#include "vpr_types.h"
#include <vector>
#include <map>

#ifdef INTERPOSER_BASED_ARCHITECTURE

/**
 * @brief Main entry point for TRUE VTR 7 interposer functionality
 */
void modify_rr_graph_for_interposer_based_arch(RRGraphBuilder& rr_graph_builder,
                                               const RRGraphView& rr_graph,
                                               const t_chan_width& chan_width,
                                               const enum e_directionality directionality);

/**
 * @brief TRUE VTR 7: Find all CHANY wires that cross interposer cuts
 */
void find_all_CHANY_wires_that_cross_the_interposer_vtr7(const RRGraphView& rr_graph,
                                                         std::vector<RRNodeId>& rr_nodes_that_cross);

/**
 * @brief TRUE VTR 7: Build reverse map for edge management (CRITICAL for fixing segfault)
 */
void alloc_and_build_reverse_map_vtr7(const RRGraphView& rr_graph);

/**
 * @brief TRUE VTR 7: Split crossing wire with complete edge transfer (FIXES SEGFAULT)
 */
RRNodeId split_crossing_wire_with_edge_transfer_vtr7(RRGraphBuilder& rr_graph_builder,
                                                    const RRGraphView& rr_graph,
                                                    RRNodeId original_node_id,
                                                    int cut_pos);

/**
 * @brief TRUE VTR 7: Create interposer node at cut location
 */
RRNodeId create_interposer_node_vtr7_real(RRGraphBuilder& rr_graph_builder,
                                          int x, int cut_pos, int track, 
                                          Direction direction);

/**
 * @brief TRUE VTR 7: Expand RR graph with wire splitting and interposer nodes
 */
void expand_rr_graph_vtr7(RRGraphBuilder& rr_graph_builder,
                         const RRGraphView& rr_graph,
                         const std::vector<RRNodeId>& rr_nodes_that_cross,
                         const std::map<short, short>& delayed_switch_map);

/**
 * @brief VTR 7: Create RR connection (ESSENTIAL for edge management)
 */
void create_rr_connection_vtr7(RRGraphBuilder& rr_graph_builder,
                              RRNodeId src_node,
                              RRNodeId dst_node, 
                              short switch_index);

/**
 * @brief VTR 7: Delete RR connection (ESSENTIAL for edge management)
 */
void delete_rr_connection_vtr7(RRGraphBuilder& rr_graph_builder, 
                              const RRGraphView& rr_graph,
                              RRNodeId src_node, 
                              RRNodeId dst_node);

/**
 * @brief Create delayed switches with increased delay
 */
std::map<short, short> create_delayed_switches(RRGraphBuilder& rr_graph_builder, 
                                              const DeviceContext& device_ctx, 
                                              int delay_increase);

/**
 * @brief Allocate and build reverse mapping for fanin tracking
 */
void alloc_and_build_reverse_map(const RRGraphView& rr_graph);

/**
 * @brief Free reverse mapping data structure
 */
void free_reverse_map();

// VTR 7 ARCHITECTURAL CONSISTENCY FUNCTIONS
RRNodeId split_crossing_wire_vtr7_construction_safe(RRGraphBuilder& rr_graph_builder,
                                                   const RRGraphView& rr_graph,
                                                   RRNodeId original_node_id,
                                                   int cut_pos,
                                                   int nodes_per_chan);

void create_interposer_nodes_vtr7_construction_safe(RRGraphBuilder& rr_graph_builder,
                                                   const RRGraphView& rr_graph,
                                                   int nodes_per_chan,
                                                   const std::vector<int>& cuts);

void expand_rr_graph_vtr7_construction_safe(RRGraphBuilder& rr_graph_builder,
                                           const RRGraphView& rr_graph,
                                           const std::vector<RRNodeId>& rr_nodes_that_cross,
                                           int nodes_per_chan);

void validate_architectural_consistency_vtr7(const RRGraphView& rr_graph, const std::vector<int>& cuts);

#endif /* INTERPOSER_BASED_ARCHITECTURE */

#endif /* RR_GRAPH_INTERPOSER_H */ 