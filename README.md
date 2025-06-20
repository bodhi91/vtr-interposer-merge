# VTR with Interposer Architecture Support

This repository contains modifications to the VTR (Verilog-to-Routing) CAD tool to support **interposer-based FPGA architectures**. This work enables modeling and evaluation of multi-die FPGA systems connected through silicon interposers.

## Overview

Interposer-based architectures represent a promising approach for scaling FPGA capacity by connecting multiple smaller dies through a silicon interposer. This modified VTR enables researchers and engineers to:

- Model multi-die FPGA architectures with interposer connections
- Evaluate the impact of interposer routing on performance and routability
- Experiment with different interposer cutting strategies and wire distributions
- Analyze delay penalties associated with inter-die communications

## Key Features

### 🔬 **Interposer Modeling**
- Automatic identification and cutting of routing wires at interposer boundaries
- Creation of interposer nodes to model inter-die connections
- Configurable wire cutting percentages and strategies

### ⚡ **Performance Analysis**
- Delay modeling for wires crossing interposer boundaries
- Configurable delay increase factors for inter-die communications
- Integration with VTR's timing analysis framework

### 🎛️ **Flexible Configuration**
- Support for horizontal and vertical interposer cuts
- Configurable connection patterns between different channel types
- Fine-grained control over fanin/fanout distributions

### 📊 **Research-Friendly**
- Pre-packing only mode for rapid architecture exploration
- Detailed logging and analysis capabilities
- Integration with existing VTR analysis flows

## Quick Start

### Prerequisites
- CMake 3.16 or later
- GCC 9+ or Clang 10+
- Python 3.6+ (for build scripts)

### Building with Interposer Support

```bash
# Clone the repository
git clone https://github.com/yourusername/vtr-interposer.git
cd vtr-interposer

# Build with interposer support enabled
mkdir build && cd build
cmake -DVTR_ENABLE_INTERPOSER_SUPPORT=ON ..
make -j$(nproc)
```

### Basic Usage

```bash
# Run VPR with interposer support
./vpr architecture.xml netlist.blif \
  --percent_wires_cut 50 \
  --num_cuts 2 \
  --delay_increase 150 \
  --allow_chanx_interposer_connections on \
  --transfer_interposer_fanins on
```

## Architecture Requirements

For proper interposer modeling, your architecture file should account for the LCM (Least Common Multiple) of block heights to ensure clean cut lines. The tool automatically calculates this during architecture parsing.

## Configuration Options

### Core Interposer Options
| Option | Description | Default |
|--------|-------------|---------|
| `--percent_wires_cut` | Percentage of wires to cut at boundaries (0-100) | 0 |
| `--num_cuts` | Number of horizontal cuts for interposer boundaries | 0 |
| `--delay_increase` | Delay increase factor for inter-die wires (%) | 0 |
| `--pre_pack_only` | Run only pre-packing for rapid exploration | false |

### Connection Control
| Option | Description | Default |
|--------|-------------|---------|
| `--allow_chanx_interposer_connections` | Allow CHANX-interposer connections | false |
| `--transfer_interposer_fanins` | Transfer fanins to interposer nodes | false |
| `--allow_additional_interposer_fanins` | Allow additional interposer fanins | false |
| `--pct_of_interposer_nodes_each_chany_can_drive` | CHANY driving percentage | 100 |
| `--transfer_interposer_fanouts` | Transfer fanouts to interposer nodes | false |
| `--allow_bidir_interposer_wires` | Enable bidirectional interposer wires | false |

## File Structure

### Modified Core Files
```
vpr/src/route/
├── rr_graph_interposer.h          # Main interposer functionality header
├── rr_graph_interposer.cpp        # Core interposer implementation
└── rr_graph.cpp                   # Integration points

vpr/src/base/
├── vpr_types.h                    # Interposer data structures
├── vpr_context.h                  # Context integration
├── read_options.{h,cpp}           # Command-line interface
├── SetupVPR.cpp                   # Configuration setup
├── ShowSetup.cpp                  # Configuration display
└── vpr_api.{h,cpp}               # API extensions

libs/libarchfpga/src/
├── physical_types.h               # Architecture extensions
└── read_xml_arch_file.cpp         # LCM calculations

libs/librrgraph/src/base/
├── rr_graph_builder.{h,cpp}       # Builder API extensions
└── check_rr_graph.cpp             # Validation updates
```

## Research Applications

This tool has been used for research in:

- **Multi-die FPGA architectures**: Evaluating the trade-offs between die size and interposer overhead
- **3D FPGA design**: Modeling vertical connections through interposer layers
- **Chiplet-based systems**: Analyzing heterogeneous multi-die configurations
- **Advanced packaging**: Studying the impact of different interposer technologies

## Examples

### Basic Interposer Experiment
```bash
# 50% wire cutting with 2 horizontal cuts, 50% delay increase
./vpr arch.xml design.blif \
  --percent_wires_cut 50 \
  --num_cuts 2 \
  --delay_increase 150
```

### Advanced Configuration
```bash
# Full-featured interposer run with bidirectional wires
./vpr arch.xml design.blif \
  --percent_wires_cut 75 \
  --num_cuts 3 \
  --delay_increase 200 \
  --allow_chanx_interposer_connections on \
  --transfer_interposer_fanins on \
  --allow_additional_interposer_fanins on \
  --allow_bidir_interposer_wires on \
  --pct_of_interposer_nodes_each_chany_can_drive 80
```

### Pre-packing Only (Fast Architecture Exploration)
```bash
# Quick analysis without placement/routing
./vpr arch.xml design.blif \
  --pre_pack_only on \
  --percent_wires_cut 60 \
  --num_cuts 2
```

## Technical Details

### Wire Cutting Algorithm
The tool identifies CHANY wires that cross interposer cut locations and:
1. Splits the wire at the cut boundary
2. Creates interposer nodes at cut locations
3. Establishes configurable connections through interposer nodes
4. Applies delay penalties to inter-die connections

### Delay Modeling
Inter-die connections incur additional delay based on:
- Base wire delay + (delay_increase_factor × base_delay)
- Configurable per-connection basis
- Integration with VTR's timing analysis

### Architecture Validation
The tool ensures:
- Cut locations align with block boundaries (LCM-based)
- Interposer nodes have valid connectivity
- RR graph consistency after modifications

## Contributing

We welcome contributions! Please see our contributing guidelines:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Development Setup
```bash
# Enable debug builds for development
cmake -DVTR_ENABLE_INTERPOSER_SUPPORT=ON -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

## Citation

If you use this work in your research, please cite:

```bibtex
@inproceedings{vtr_interposer_2024,
  title={Interposer-Based FPGA Architecture Modeling in VTR},
  author={Your Name},
  booktitle={Proceedings of the International Conference on FPGA},
  year={2024},
  organization={ACM}
}
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Built on the excellent [VTR Project](https://verilogtorouting.org/)
- Inspired by research in multi-die FPGA architectures
- Thanks to the FPGA architecture research community

## Support

- 📖 [Documentation](docs/)
- 🐛 [Issue Tracker](https://github.com/yourusername/vtr-interposer/issues)
- 💬 [Discussions](https://github.com/yourusername/vtr-interposer/discussions)
- 📧 Contact: your.email@university.edu

---

**Keywords**: FPGA, Interposer, Multi-die, CAD Tools, VTR, Architecture Modeling, Silicon Interposer, Chiplet


