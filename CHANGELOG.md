# VTR Change Log
<!-- 
This file documents user-facing changes between releases of the VTR
project. The goal is to concicely communicate to end users what is new
or has changed in a particular release. It should *NOT* just be a dump
of the commit log, as that is far too detailed. Most code re-factoring
does not warrant a change log entry unless it has a significant impact
on the end users (e.g. substantial performance improvements).

Each release's change log should include headings (where releveant) with
bullet points listing what was: 
  - added           (new feature)
  - changed         (change to existing feature behaviour)
  - fixed           (bug fix)
  - deprecated      (features planned for future removal)
  - removed         (previous features which have been removed)

Changes which have landed in the master/trunk but not been released
should be included in the 'Unreleased' section and moved to the releveant
releases' section when released.

In the case of release candidates (e.g. v8.0.0-rc1) the current
set of unreleased changes should be moved under that heading. Any
subsequent fixes to the release candidate would be placed under
'Unreleased', eventually moving into the next release candidate's
heading (e.g. v8.0.0-rc2) when created. Note this means the change log for
subsequent release candidates (e.g. rc2) would only contain new changes
not included in previous release candidates (e.g. rc1).  When the final
(non-release candidate) release is made (e.g. v8.0.0) the change log
should contain all the relevant changes compared to the last non-release
candidate release (e.g. v7.0.0). That is, it should be the concatenation
of the unreleased and any previous release candidates change logs.
-->

_Note that changes from release candidates (e.g. v8.0.0-rc1, v8.0.0-rc2) are included/repeated in the final release (e.g. v8.0.0) change log._

## Unreleased
_The following are changes which have been implemented in the VTR master branch but have not yet been included in an official release._

### Added

### Changed

### Fixed

### Deprecated

### Removed


## v9.0.0 - 2024-12-23

### Added
  * Support for Advanced Architectures:
    * 3D FPGA and RAD architectures.
    * Architectures with hard Networks-on-Chip (NoCs).
    * Distinct horizontal and vertical channel widths and types.
    * Diagonal routing wires and other complex wire shapes (L-shaped, T-shaped, ....).
  
  * New Benchmark Suites:
    * Koios: A deep-learning-focused benchmark suite with various design sizes.
    * Hermes: Benchmarks utilizing hard NoCs.
    * TitanNew: Large benchmarks targeting the Stratix 10 architecture.

  * Commercial FPGAs Architecture Captures:
    * Intel's Stratix 10 FPGA architecture.
    * AMD's 7-series FPGA architecture.

  * Parmys Logic Synthesis Flow:
    * Better Verilog language coverage
    * More efficient hard block mapping

  * VPR Graphics Visualizations:
    * New interface for improved usability and underlying graphics rewritten using EZGL/GTK to allow more UI widgets.
    * Algorithm breakpoint visualizations for placement and routing algorithm debugging.
    * User-guided (manual) placement optimization features.
    * Enabled a live connection for client graphical application to VTR engines through sockets (server mode).
    * Interactive timing path analysis (IPA) client using server mode.
   
  * Performance Enhancements:
    * Parallel router for faster inter-cluster routing or flat routing.

  * Re-clustering API to modify packing decisions during the flow.
  * Support for floorplanning and placement constraints.
  * Unified intra- and inter-cluster (flat) routing.
  * Comprehensive web-based VTR utilities and API documentation.
  
### Changed
  * The default values of many command line options (e.g. inner_num is 0.5 instead of 1.0)
  * Changes to placement engine
    * Smart centroid initial placement algorithm.
    * Multiple smart placement directed moves.
    * Reinforcement learning-based placement algorithm.
  * Changes to routing engine
    * Faster lookahead creation.
    * More accurate lookahead for large blocks.
    * More efficient heap and pruning strategies.
    * max `pres_fac` capped to avoid possible numeric issues.
    
    
### Fixed
  * Many algorithmic and coding bugs are fixed in this release
   
### Removed
  * Breadth-first (non-timing-driven) router.
  * Non-linear congestion placement cost.

## v8.0.0 - 2020-03-24

### Added
 * Support for arbitrary FPGA device grids/floorplans
 * Support for clustered blocks with width > 1
 * Customizable connection-block and switch-blocks patterns (controlled from FPGA architecture file)
 * Fan-out dependent routing mux delays
 * VPR can generate/load a routing architecture (routing resource graph) in XML format
 * VPR can load routing from a `.route` file
 * VPR can performing analysis (STA/Power/Area) independently from optimization (via `vpr --analysis`)
 * VPR supports netlist primitives with multiple clocks
 * VPR can perform hold-time (minimum delay) timing analysis
 * Minimum delays can be annotated in the FPGA architecture file
 * Flow supports formal verification of circuit implementation against input netlist
 * Support for generating FASM to drive bitstream generators
 * Routing predictor which predicts and aborts impossible routings early (saves significant run-time during minimum channel width search)
 * Support for minimum routable channel width 'hints' (reduces minimum channel width search run-time if accurate)
 * Improved VPR debugging/verbosity controls
 * VPR can perform basic netlist cleaning (e.g. sweeping dangling logic)
 * VPR graphics visualizations:
   * Critical path during placement/routing
   * Cluster pin utilization heatmap
   * Routing utilization heatmap
   * Routing resource cost heatmaps
   * Placement macros
 * VPR can route constant nets
 * VPR can route clock nets
 * VPR can load netlists in extended BLIF (eBLIF) format
 * Support for generating post-placement timing reports
 * Improved router 'map' lookahead which adapts to routing architecture structure
 * Script to upgrade legacy architecture files (`vtr_flow/scripts/upgrade_arch.py`)
 * Support for Fc overrides which depend on both pin and target wire segment type
 * Support for non-configurable switches (shorts, inline-buffers) used to model structures like clock-trees and non-linear wires (e.g. 'L' or 'T' shapes)
 * Various other features since VTR 7

### Changed
 * VPR will exit with code 1 on errors (something went wrong), and code 2 when unable to implement a circuit (e.g. unroutable)
 * VPR now gives more complete help about command-line options (`vpr -h`)
 * Improved a wide variety of error messages
 * Improved STA timing reports (more details, clearer format)
 * VPR now uses Tatum as its STA engine
 * VPR now detects missmatched architecture (.xml) and implementation (.net/.place/.route) files more robustly
 * Improved router run-time and quality through incremental re-routing and improved handling of high-fanout nets
 * The timing edges within each netlist primitive must now be specified in the <models> section of the architecture file
 * All interconnect tags must have unique names in the architecture file
 * Connection block input pin switch must now be specified in <switchlist> section of the architecture file
 * Renamed switch types buffered/pass_trans to more descriptive tristate/pass_gate in architecture file
 * Require longline segment types to have no switchblock/connectionblock specification
 * Improve naming (true/false -> none/full/instance) and give more control over block pin equivalnce specifications
 * VPR will produce a .route file even if the routing is illegal (aids debugging), however analysis results will not be produced unless `vpr --analsysis` is specified
 * VPR long arguments are now always prefixed by two dashes (e.g. `--route`) while short single-letter arguments are prefixed by a single dash (e.g. `-h`)
 * Improved logic optimization through using a recent 2018 version of ABC and new synthesis script
 * Significantly improved implementation quality (~14% smaller minimum routable channel widths, 32-42% reduced wirelength, 7-10% lower critical path delay)
 * Significantly reduced run-time (~5.5-6.3x faster) and memory usage (~3.3-5x lower)
 * Support for non-contiguous track numbers in externally loaded RR graphs
 * Improved placer quality (reduced cost round-off)
 * Various other changes since VTR 7

### Fixed
 * FPGA Architecture file tags can be in arbitary orders
 * SDC command arguments can be in arbitary orders
 * Numerous other fixes since VTR 7

### Removed
 * Classic VPR timing analyzer
 * IO channel distribution section of architecture file

### Deprecated
 * VPR's breadth-first router (use the timing-driven router, which provides supperiour QoR and Run-time)

### Docker Image
 * A docker image is available for VTR 8.0 release on mohamedelgammal/vtr8:latest. You can run it using the following commands:
```
$ sudo docker pull mohamedelgammal/vtr8:latest
$ sudo docker run -it mohamedelgammal/vtr8:latest
```
 
## v8.0.0-rc2 - 2019-08-01

### Changed
 * Support for non-contiguous track numbers in externally loaded RR graphs
 * Improved placer quality (reduced cost round-off)

## v8.0.0-rc1 - 2019-06-13

### Added
 * Support for arbitrary FPGA device grids/floorplans
 * Support for clustered blocks with width > 1
 * Customizable connection-block and switch-blocks patterns (controlled from FPGA architecture file)
 * Fan-out dependent routing mux delays
 * VPR can generate/load a routing architecture (routing resource graph) in XML format
 * VPR can load routing from a `.route` file
 * VPR can performing analysis (STA/Power/Area) independently from optimization (via `vpr --analysis`)
 * VPR supports netlist primitives with multiple clocks
 * VPR can perform hold-time (minimum delay) timing analysis
 * Minimum delays can be annotated in the FPGA architecture file
 * Flow supports formal verification of circuit implementation against input netlist
 * Support for generating FASM to drive bitstream generators
 * Routing predictor which predicts and aborts impossible routings early (saves significant run-time during minimum channel width search)
 * Support for minimum routable channel width 'hints' (reduces minimum channel width search run-time if accurate)
 * Improved VPR debugging/verbosity controls
 * VPR can perform basic netlist cleaning (e.g. sweeping dangling logic)
 * VPR graphics visualizations:
   * Critical path during placement/routing
   * Cluster pin utilization heatmap
   * Routing utilization heatmap
   * Routing resource cost heatmaps
   * Placement macros
 * VPR can route constant nets
 * VPR can route clock nets
 * VPR can load netlists in extended BLIF (eBLIF) format
 * Support for generating post-placement timing reports
 * Improved router 'map' lookahead which adapts to routing architecture structure
 * Script to upgrade legacy architecture files (`vtr_flow/scripts/upgrade_arch.py`)
 * Support for Fc overrides which depend on both pin and target wire segment type
 * Support for non-configurable switches (shorts, inline-buffers) used to model structures like clock-trees and non-linear wires (e.g. 'L' or 'T' shapes)
 * Various other features since VTR 7

### Changed
 * VPR will exit with code 1 on errors (something went wrong), and code 2 when unable to implement a circuit (e.g. unroutable)
 * VPR now gives more complete help about command-line options (`vpr -h`)
 * Improved a wide variety of error messages
 * Improved STA timing reports (more details, clearer format)
 * VPR now uses Tatum as its STA engine
 * VPR now detects missmatched architecture (.xml) and implementation (.net/.place/.route) files more robustly
 * Improved router run-time and quality through incremental re-routing and improved handling of high-fanout nets
 * The timing edges within each netlist primitive must now be specified in the <models> section of the architecture file
 * All interconnect tags must have unique names in the architecture file
 * Connection block input pin switch must now be specified in <switchlist> section of the architecture file
 * Renamed switch types buffered/pass_trans to more descriptive tristate/pass_gate in architecture file
 * Require longline segment types to have no switchblock/connectionblock specification
 * Improve naming (true/false -> none/full/instance) and give more control over block pin equivalnce specifications
 * VPR will produce a .route file even if the routing is illegal (aids debugging), however analysis results will not be produced unless `vpr --analsysis` is specified
 * VPR long arguments are now always prefixed by two dashes (e.g. `--route`) while short single-letter arguments are prefixed by a single dash (e.g. `-h`)
 * Improved logic optimization through using a recent 2018 version of ABC and new synthesis script
 * Significantly improved implementation quality (~14% smaller minimum routable channel widths, 32-42% reduced wirelength, 7-10% lower critical path delay)
 * Significantly reduced run-time (~5.5-6.3x faster) and memory usage (~3.3-5x lower)
 * Various other changes since VTR 7

### Fixed
 * FPGA Architecture file tags can be in arbitary orders
 * SDC command arguments can be in arbitary orders
 * Numerous other fixes since VTR 7

### Deprecated

### Removed
 * Classic VPR timing analyzer
 * IO channel distribution section of architecture file

# Changelog

All notable changes to the VTR Interposer project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-XX-XX

### Added - Interposer Architecture Support

#### Core Interposer Features
- **Wire Cutting Algorithm**: Automatic identification and cutting of CHANY wires at interposer boundaries
- **Interposer Nodes**: Creation of special routing nodes to model inter-die connections
- **Delay Modeling**: Configurable delay penalties for wires crossing interposer boundaries
- **Multi-die Support**: Framework for modeling multi-die FPGA architectures

#### Configuration Options
- `--percent_wires_cut`: Control percentage of wires cut at interposer boundaries (0-100)
- `--num_cuts`: Specify number of horizontal cuts for interposer boundaries
- `--delay_increase`: Set delay increase factor for inter-die communications
- `--pre_pack_only`: Enable pre-packing only mode for rapid architecture exploration
- `--allow_chanx_interposer_connections`: Allow connections between CHANX wires and interposer nodes
- `--transfer_interposer_fanins`: Transfer fanins from cut wires to interposer nodes
- `--allow_additional_interposer_fanins`: Allow additional fanins for interposer nodes
- `--pct_of_interposer_nodes_each_chany_can_drive`: Control CHANY driving percentage
- `--transfer_interposer_fanouts`: Transfer fanouts from cut wires to interposer nodes
- `--allow_additional_interposer_fanouts`: Allow additional fanouts for interposer nodes
- `--pct_of_chany_wires_an_interposer_node_can_drive`: Control interposer node driving capability
- `--allow_bidir_interposer_wires`: Enable bidirectional interposer wires

#### Modified Files
- **Build System**:
  - `CMakeLists.txt`: Added `VTR_ENABLE_INTERPOSER_SUPPORT` compilation option
  
- **Core Implementation**:
  - `vpr/src/route/rr_graph_interposer.h`: New header for interposer functionality
  - `vpr/src/route/rr_graph_interposer.cpp`: Main interposer implementation
  - `vpr/src/route/rr_graph.cpp`: Integration points for interposer support
  
- **VPR Base System**:
  - `vpr/src/base/vpr_types.h`: Added `t_interposer_opts` structure
  - `vpr/src/base/vpr_context.h`: Integrated interposer options into VPR context
  - `vpr/src/base/read_options.h`: Added interposer command-line argument definitions
  - `vpr/src/base/read_options.cpp`: Implemented interposer argument parsing
  - `vpr/src/base/SetupVPR.cpp`: Added `SetupInterposerOpts()` function
  - `vpr/src/base/ShowSetup.cpp`: Added `ShowInterposerOpts()` function
  - `vpr/src/base/vpr_api.h`: Added interposer API declarations
  - `vpr/src/base/vpr_api.cpp`: Implemented interposer setup and cut location logic
  
- **RR Graph Infrastructure**:
  - `libs/librrgraph/src/base/rr_graph_builder.h`: Added VTR 9 API extensions
  - `libs/librrgraph/src/base/rr_graph_builder.cpp`: Implemented API extensions
  - `libs/librrgraph/src/base/check_rr_graph.cpp`: Updated validation for interposer nodes
  
- **Architecture Support**:
  - `libs/libarchfpga/src/physical_types.h`: Added `lcm_of_block_heights` field
  - `libs/libarchfpga/src/read_xml_arch_file.cpp`: Implemented LCM calculation for proper cut placement

#### Technical Enhancements
- **LCM Calculation**: Automatic calculation of least common multiple of block heights to ensure interposer cuts don't intersect physical blocks
- **Wire Identification**: Algorithm to find all CHANY wires that cross interposer cut locations
- **Node Creation**: Safe construction of interposer nodes during RR graph building
- **Connectivity Management**: Flexible fanin/fanout transfer and connection patterns
- **Validation**: Extended RR graph validation to handle interposer nodes correctly

#### Research Features
- **Pre-packing Mode**: Rapid architecture exploration without full placement/routing
- **Configurable Connectivity**: Fine-grained control over inter-die connection patterns
- **Performance Analysis**: Integration with VTR's timing analysis for delay modeling
- **Quality of Results**: Support for evaluating architecture trade-offs

### Technical Details

#### Wire Cutting Algorithm
1. Identifies CHANY wires crossing interposer cut locations
2. Splits wires at cut boundaries
3. Creates interposer nodes at cut locations
4. Establishes configurable connections through interposer nodes
5. Applies delay penalties to inter-die connections

#### Architecture Validation
- Ensures cut locations align with block boundaries using LCM calculations
- Validates interposer node connectivity
- Maintains RR graph consistency after modifications
- Supports both flat and hierarchical routing graph validation

#### Build System Integration
- Compile-time feature flag: `INTERPOSER_BASED_ARCHITECTURE`
- CMake option: `VTR_ENABLE_INTERPOSER_SUPPORT`
- Backward compatibility with standard VTR builds

### Future Work
- Support for vertical interposer cuts
- Advanced delay modeling for different interposer technologies
- Integration with heterogeneous multi-die architectures
- Enhanced visualization tools for interposer analysis

---

## Legacy VTR Changes

This section documents the base VTR changes that this interposer-enabled version is built upon. For the complete VTR changelog, please refer to the [original VTR repository](https://github.com/verilog-to-routing/vtr-verilog-to-routing).

### Base Version
- **VTR 8.0+**: Based on the latest VTR development branch
- **Core Features**: All standard VTR functionality preserved
- **Compatibility**: Maintains compatibility with existing VTR flows and benchmarks
