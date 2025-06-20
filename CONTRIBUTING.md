# Contributing to VTR Interposer

Thank you for your interest in contributing to the VTR Interposer project! This document provides guidelines for contributing to the interposer-enabled VTR CAD tool.

## Getting Started

### Development Environment Setup

1. **Clone the Repository**
   ```bash
   git clone https://github.com/yourusername/vtr-interposer.git
   cd vtr-interposer
   ```

2. **Build with Debug Support**
   ```bash
   mkdir build-debug && cd build-debug
   cmake -DVTR_ENABLE_INTERPOSER_SUPPORT=ON -DCMAKE_BUILD_TYPE=Debug ..
   make -j$(nproc)
   ```

3. **Run Tests**
   ```bash
   # Run basic VTR tests to ensure core functionality
   make test
   
   # Run interposer-specific tests
   ./test_interposer_functionality.sh  # If available
   ```

## How to Contribute

### Reporting Issues

When reporting bugs or requesting features:

1. **Search existing issues** first to avoid duplicates
2. **Use clear, descriptive titles**
3. **Provide detailed information**:
   - VTR version and build configuration
   - Operating system and compiler version
   - Steps to reproduce the issue
   - Expected vs. actual behavior
   - Relevant log files or error messages

### Pull Request Process

1. **Fork the Repository** and create a feature branch
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Follow Coding Standards** (see below)

3. **Write Tests** for new functionality

4. **Update Documentation** as needed

5. **Commit Your Changes**
   ```bash
   git commit -m "Add feature: brief description"
   ```

6. **Push and Create Pull Request**
   ```bash
   git push origin feature/your-feature-name
   ```

## Coding Standards

### C++ Guidelines

- **Follow VTR coding style**: Consistent with existing VTR codebase
- **Use meaningful variable names**: `interposer_cut_location` not `icl`
- **Add comments for complex algorithms**: Especially interposer-specific logic
- **Use const correctness**: Mark variables and methods const where appropriate
- **Prefer RAII**: Proper resource management

### Interposer-Specific Guidelines

- **Preprocessor Guards**: All interposer code should be wrapped in:
  ```cpp
  #ifdef INTERPOSER_BASED_ARCHITECTURE
  // Your interposer code here
  #endif
  ```

- **Naming Conventions**:
  - Interposer functions: `*_interposer_*` or `*_vtr7_*` (for VTR 7 compatibility)
  - Interposer variables: Use clear, descriptive names
  - Interposer types: Follow VTR naming conventions

- **Error Handling**: Use VTR's error handling mechanisms:
  ```cpp
  VPR_FATAL_ERROR(VPR_ERROR_ROUTE, "Interposer error: %s", error_msg);
  ```

### File Organization

```
vpr/src/route/
├── rr_graph_interposer.h          # Main header
├── rr_graph_interposer.cpp        # Implementation
└── rr_graph.cpp                   # Integration points

// New files should follow this pattern:
├── interposer_[functionality].h
├── interposer_[functionality].cpp
```

## Types of Contributions

### 🐛 Bug Fixes
- Fixes to existing interposer functionality
- Validation improvements
- Performance optimizations

### ✨ New Features
- Additional interposer cutting algorithms
- New delay models
- Visualization enhancements
- Support for vertical cuts

### 📚 Documentation
- README improvements
- Code comments
- Usage examples
- Research papers and references

### 🧪 Testing
- Unit tests for interposer functions
- Integration tests
- Benchmark validation
- Performance regression tests

## Development Guidelines

### Testing Your Changes

1. **Build Tests**:
   ```bash
   # Test with interposer support enabled
   cmake -DVTR_ENABLE_INTERPOSER_SUPPORT=ON ..
   make -j$(nproc)
   
   # Test with interposer support disabled
   cmake -DVTR_ENABLE_INTERPOSER_SUPPORT=OFF ..
   make -j$(nproc)
   ```

2. **Functionality Tests**:
   ```bash
   # Basic interposer test
   ./vpr arch.xml design.blif --percent_wires_cut 50 --num_cuts 2
   
   # Regression test (ensure no breaking changes)
   ./vpr arch.xml design.blif  # Standard VTR run
   ```

3. **Validation**:
   - Ensure RR graph validation passes
   - Check for memory leaks with valgrind
   - Verify QoR doesn't degrade unexpectedly

### Code Review Checklist

Before submitting a pull request, ensure:

- [ ] Code compiles with both `-DVTR_ENABLE_INTERPOSER_SUPPORT=ON` and `OFF`
- [ ] All existing tests pass
- [ ] New functionality has appropriate tests
- [ ] Code follows VTR style guidelines
- [ ] Documentation is updated
- [ ] Commit messages are clear and descriptive
- [ ] No memory leaks or undefined behavior
- [ ] Performance impact is acceptable

## Specific Areas for Contribution

### High Priority
- **Vertical Cut Support**: Extend cutting algorithm to support vertical interposer boundaries
- **Advanced Delay Models**: More sophisticated delay modeling for different interposer technologies
- **Visualization Tools**: Tools to visualize interposer cuts and routing patterns
- **Performance Optimization**: Improve efficiency of wire cutting and node creation algorithms

### Medium Priority
- **Additional Connectivity Patterns**: More flexible connection patterns between dies
- **Heterogeneous Die Support**: Support for different die types in multi-die architectures
- **Architecture Generation**: Tools to automatically generate interposer-aware architectures
- **Quality Metrics**: New metrics for evaluating interposer architectures

### Research Contributions
- **Novel Algorithms**: New approaches to wire cutting and interposer routing
- **Architecture Studies**: Validation against real interposer-based systems
- **Benchmarking**: New benchmark designs for interposer evaluation
- **Academic Publications**: Integration of research findings

## Communication

### Channels
- **GitHub Issues**: Bug reports and feature requests
- **GitHub Discussions**: Design discussions and questions
- **Pull Request Reviews**: Code-related discussions
- **Email**: your.email@university.edu for research collaboration

### Meeting Guidelines
- Regular contributor meetings (TBD)
- Research collaboration discussions
- Architecture review sessions

## License

By contributing to this project, you agree that your contributions will be licensed under the same license as the project (MIT License). See [LICENSE](LICENSE) for details.

## Questions?

If you have questions about contributing:
1. Check existing issues and discussions
2. Create a new discussion thread
3. Contact the maintainers directly

## Acknowledgments

- **Base VTR Project**: This work builds on the excellent VTR framework
- **Research Community**: Thanks to the FPGA architecture research community
- **Contributors**: All contributors to this project are acknowledged in the repository

---

*Thank you for helping improve VTR Interposer!*
