/**
 * @file
 * @author Jason Luu
 * @date June 21, 2012
 *
 * @brief  General API for VPR
 *
 * VPR is a CAD tool used to conduct FPGA architecture exploration.  It takes, as input, a technology-mapped netlist and a description of the FPGA architecture being investigated.
 * VPR then generates a packed, placed, and routed FPGA (in .net, .place, and .route files respectively) that implements the input netlist
 *
 * Software tools interfacing to VPR should generally call just the functions defined here
 * For advanced/power users, you can call functions defined elsewhere in VPR or modify the data structures directly at your discretion but be aware that doing so can break the correctness of this tool
 *
 * General Usage:
 * 1. vpr_init
 * 2. vpr_pack
 * 3. vpr_init_pre_place_and_route
 * 4. vpr_place_and_route
 * 5. vpr_free_all
 *
 * If you are a new developer, key files to begin understanding this code base are:
 * 1.  libarchfpga/physical_types.h - Data structures that define the properties of the FPGA architecture
 * 2.  vpr_types.h - Very major file that defines the core data structures used in VPR.  This includes detailed architecture information, user netlist data structures, and data structures that describe the mapping between those two.
 * 3.  globals.h - Defines the global variables used by VPR.
 */

#ifndef VPR_API_H
#define VPR_API_H

#include <vector>
#include "physical_types.h"
#include "vpr_types.h"
#include "read_options.h"
#include "globals.h"
#include "read_xml_arch_file.h"
#include "vpr_utils.h"
#include "place_macro.h"
#include "timing_info_fwd.h"
#include "echo_files.h"
#include "RoutingDelayCalculator.h"

#include "vpr_error.h"

/*
 * Main VPR Operations
 */
void vpr_init(const int argc, const char** argv, t_options* options, t_vpr_setup* vpr_setup, t_arch* arch);
void vpr_initialize_logging();
void vpr_init_with_options(const t_options* options, t_vpr_setup* vpr_setup, t_arch* arch);

bool vpr_flow(t_vpr_setup& vpr_setup, t_arch& arch); //Run the VPR CAD flow

/*
 * Stage operations
 */

/* Packing */

///@brief Perform, load or skip the packing stage
bool vpr_pack_flow(t_vpr_setup& vpr_setup, const t_arch& arch);

///@brief Perform packing
bool vpr_pack(t_vpr_setup& vpr_setup, const t_arch& arch);

///@brief Loads a previous packing
void vpr_load_packing(const t_vpr_setup& vpr_setup, const t_arch& arch);

///@brief Reconstructs a packing and placement solution from a flat placement file
bool vpr_load_flat_placement(t_vpr_setup& vpr_setup, const t_arch& arch);

/* Placement */

///@brief Perform, load or skip the placement stage
bool vpr_place_flow(const Netlist<>& net_list,
                    t_vpr_setup& vpr_setup,
                    const t_arch& arch);

///@brief Perform placement
void vpr_place(const Netlist<>& net_list,
               t_vpr_setup& vpr_setup,
               const t_arch& arch);

///@brief Loads a previous placement
void vpr_load_placement(t_vpr_setup& vpr_setup,
                        const std::vector<t_direct_inf> directs);

/* Routing */

///@brief Perform, load or skip the routing stage
RouteStatus vpr_route_flow(const Netlist<>& net_list,
                           t_vpr_setup& vpr_setup,
                           const t_arch& arch,
                           bool is_flat);

///@brief Perform routing at a fixed channel width)
RouteStatus vpr_route_fixed_W(const Netlist<>& net_list,
                              t_vpr_setup& vpr_setup,
                              const t_arch& arch,
                              int fixed_channel_width,
                              std::shared_ptr<SetupHoldTimingInfo> timing_info,
                              std::shared_ptr<RoutingDelayCalculator> delay_calc,
                              NetPinsMatrix<float>& net_delay,
                              bool is_flat);

///@brief Perform routing to find the minimum channel width
RouteStatus vpr_route_min_W(const Netlist<>& net_list,
                            t_vpr_setup& vpr_setup,
                            const t_arch& arch,
                            std::shared_ptr<SetupHoldTimingInfo> timing_info,
                            std::shared_ptr<RoutingDelayCalculator> delay_calc,
                            NetPinsMatrix<float>& net_delay,
                            bool is_flat);

///@brief Loads a previous routing
RouteStatus vpr_load_routing(t_vpr_setup& vpr_setup,
                             const t_arch& arch,
                             int fixed_channel_width,
                             std::shared_ptr<SetupHoldTimingInfo> timing_info,
                             NetPinsMatrix<float>& net_delay,
                             bool is_flat);

/* Analysis */

///@brief Perform or skips the analysis stage
bool vpr_analysis_flow(const Netlist<>& net_list,
                       t_vpr_setup& vpr_setup,
                       const t_arch& Arch,
                       const RouteStatus& route_status,
                       bool is_flat);

///@brief Perform post-implementation analysis
void vpr_analysis(const Netlist<>& net_list,
                  t_vpr_setup& vpr_setup,
                  const t_arch& Arch,
                  const RouteStatus& route_status,
                  bool is_flat);

/* Device creating */

///@brief Create the device (grid + rr graph)
void vpr_create_device(t_vpr_setup& vpr_setup, const t_arch& Arch);

///@brief Create the device grid
void vpr_create_device_grid(const t_vpr_setup& vpr_setup, const t_arch& Arch);

///@brief Create routing graph at specified channel width
void vpr_create_rr_graph(t_vpr_setup& vpr_setup, const t_arch& arch, int chan_width, bool is_flat);

void vpr_init_graphics(const t_vpr_setup& vpr_setup, const t_arch& arch, bool is_flat);
void vpr_init_server(const t_vpr_setup& vpr_setup);

void vpr_close_graphics(const t_vpr_setup& vpr_setup);

void vpr_setup_clock_networks(t_vpr_setup& vpr_setup, const t_arch& Arch);

void vpr_setup_noc(const t_vpr_setup& vpr_setup, const t_arch& arch);
void vpr_setup_noc_routing_algorithm(const std::string& noc_routing_algorithm_name);

#ifdef INTERPOSER_BASED_ARCHITECTURE
void vpr_setup_interposer_cut_locations(const t_vpr_setup& vpr_setup, const t_arch& arch);
#endif

void vpr_free_vpr_data_structures(t_arch& Arch, t_vpr_setup& vpr_setup);
void vpr_free_all(t_arch& Arch,
                  t_vpr_setup& vpr_setup);

/* Display general info to user */
void vpr_print_title();
void vpr_print_args(int argc, const char** argv);

/****************************************************************************************************
 * Advanced functions
 *  Used when you need fine-grained control over VPR that the main VPR operations do not enable
 ****************************************************************************************************/

///@brief Read in user options
void vpr_read_options(const int argc, const char** argv, t_options* options);

///@brief Read in arch and circuit
void vpr_setup_vpr(t_options* Options,
                   const bool TimingEnabled,
                   const bool readArchFile,
                   t_file_name_opts* FileNameOpts,
                   t_arch* Arch,
                   t_model** user_models,
                   t_model** library_models,
                   t_netlist_opts* NetlistOpts,
                   t_packer_opts* PackerOpts,
                   t_placer_opts* PlacerOpts,
                   t_ap_opts* APOpts,
                   t_router_opts* RouterOpts,
                   t_analysis_opts* AnalysisOpts,
                   t_noc_opts* NocOpts,
                   t_server_opts* ServerOpts,
                   t_det_routing_arch* RoutingArch,
                   std::vector<t_lb_type_rr_node>** PackerRRGraph,
                   std::vector<t_segment_inf>& Segments,
                   t_timing_inf* Timing,
                   bool* ShowGraphics,
                   int* GraphPause,
                   bool* SaveGraphics,
                   std::string* GraphicsCommands,
                   t_power_opts* PowerOpts,
                   t_vpr_setup* vpr_setup);

///@brief Check inputs are reasonable
void vpr_check_arch(const t_arch& Arch);

///@brief Verify settings don't conflict or otherwise not make sense
void vpr_check_setup(const t_packer_opts& PackerOpts,
                     const t_placer_opts& PlacerOpts,
                     const t_ap_opts& APOpts,
                     const t_router_opts& RouterOpts,
                     const t_server_opts& ServerOpts,
                     const t_det_routing_arch& RoutingArch,
                     const std::vector<t_segment_inf>& Segments,
                     const t_timing_inf& Timing,
                     const t_chan_width_dist& Chans);

///@brief Show current setup
void vpr_show_setup(const t_vpr_setup& vpr_setup);
void vpr_power_estimation(const t_vpr_setup& vpr_setup,
                          const t_arch& Arch,
                          const SetupTimingInfo& timing_info,
                          const RouteStatus& route_status);

///@brief Output file names management
void vpr_alloc_and_load_output_file_names(const char* default_name);
void vpr_set_output_file_name(enum e_output_files ename, const char* name, const char* default_name);
char* vpr_get_output_file_name(enum e_output_files ename);

///@brief Prints user file or internal errors for VPR
void vpr_print_error(const VprError& vpr_error);

#endif
