# Getting Started with VTR Interposer

This guide will help you get started with using VTR's interposer functionality for modeling multi-die FPGA architectures.

## What is Interposer-Based Architecture?

Interposer-based architectures use a silicon interposer to connect multiple smaller FPGA dies, offering:
- **Scalability**: Overcome reticle limits by using smaller dies
- **Yield**: Improve manufacturing yield with smaller die sizes  
- **Flexibility**: Mix different technologies or die types
- **Cost**: Potentially reduce overall system cost

## Prerequisites

1. **Build VTR with Interposer Support**
   ```bash
   ./build_interposer.sh
   ```

2. **Verify Installation**
   ```bash
   ./build/vpr/vpr --help | grep interposer
   ```

## Basic Concepts

### Wire Cutting
VTR identifies routing wires that cross interposer boundaries and cuts them, creating connection points between dies.

### Interposer Nodes  
Special routing nodes are created at cut locations to model the physical interposer connections.

### Delay Modeling
Wires crossing interposer boundaries incur additional delay penalties to model the electrical characteristics.

## Your First Interposer Run

### Step 1: Prepare Your Design
Use any standard VTR-compatible architecture and netlist:
```bash
# Example files (use your own)
ARCH="arch/timing/k6_frac_N10_mem32K_40nm.xml"
NETLIST="benchmarks/arithmetic/multless_consts/mult_const.blif"
```

### Step 2: Basic Interposer Experiment
```bash
./build/vpr/vpr $ARCH $NETLIST \
  --percent_wires_cut 50 \
  --num_cuts 2 \
  --delay_increase 150
```

**Parameters Explained:**
- `--percent_wires_cut 50`: Cut 50% of wires at interposer boundaries
- `--num_cuts 2`: Create 2 horizontal cut lines (3 dies total)
- `--delay_increase 150`: 50% delay increase for inter-die connections

### Step 3: Analyze Results
VTR will output:
- Standard timing and area reports
- Information about interposer cuts and connections
- Analysis of inter-die routing patterns

## Understanding the Output

### Cut Information
Look for log messages like:
```
Interposer Architecture: LCM of block heights = 4
VTR 7: Finding CHANY wires that cross interposer cuts
Created 127 interposer nodes at cut locations
```

### Timing Impact
Check the critical path timing to see the impact of interposer delays:
```
Critical Path Delay: 15.2 ns (vs 12.1 ns without interposer)
```

## Common Use Cases

### Architecture Exploration
Quick evaluation without full place and route:
```bash
./build/vpr/vpr $ARCH $NETLIST \
  --pre_pack_only on \
  --percent_wires_cut 60 \
  --num_cuts 2
```

### Performance Analysis
Study delay impact with different configurations:
```bash
# Conservative cutting
./build/vpr/vpr $ARCH $NETLIST \
  --percent_wires_cut 25 \
  --num_cuts 1 \
  --delay_increase 100

# Aggressive cutting  
./build/vpr/vpr $ARCH $NETLIST \
  --percent_wires_cut 75 \
  --num_cuts 3 \
  --delay_increase 200
```

### Connectivity Studies
Experiment with different connection patterns:
```bash
./build/vpr/vpr $ARCH $NETLIST \
  --percent_wires_cut 50 \
  --num_cuts 2 \
  --allow_chanx_interposer_connections on \
  --transfer_interposer_fanins on \
  --allow_bidir_interposer_wires on
```

## Next Steps

1. **Read the full documentation**: See `docs/interposer/`
2. **Try the examples**: Check `docs/examples/`
3. **Understand the research**: Review `docs/research/`
4. **Experiment**: Try different parameter combinations
5. **Contribute**: See `CONTRIBUTING.md` for ways to help

## Troubleshooting

### Build Issues
```bash
# Clean rebuild
./build_interposer.sh --clean

# Debug build for troubleshooting
./build_interposer.sh --debug
```

### Runtime Issues
- Ensure your architecture has appropriate block heights for clean cuts
- Check that cut locations don't intersect with fixed blocks
- Verify sufficient routing resources for your cutting percentage

### Getting Help
- Check the [issue tracker](https://github.com/yourusername/vtr-interposer/issues)
- Join the [discussions](https://github.com/yourusername/vtr-interposer/discussions)
- Contact the maintainers

## Advanced Topics

For more advanced usage, see:
- [Configuration Reference](configuration.md)
- [Algorithm Details](algorithms.md)
- [Research Applications](../research/applications.md) 