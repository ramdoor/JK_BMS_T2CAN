# Contributing to JK BMS Master Controller

First off, thank you for considering contributing to this project! Community contributions make open-source projects thrive.

## Table of Contents
1. [Code of Conduct](#code-of-conduct)
2. [How Can I Contribute?](#how-can-i-contribute)
3. [Development Setup](#development-setup)
4. [Coding Standards](#coding-standards)
5. [Commit Guidelines](#commit-guidelines)
6. [Pull Request Process](#pull-request-process)
7. [Testing](#testing)
8. [Documentation](#documentation)

---

## Code of Conduct

This project adheres to a code of conduct that fosters an open and welcoming environment. By participating, you agree to:

- Be respectful and inclusive
- Accept constructive criticism gracefully
- Focus on what's best for the community
- Show empathy towards others

**Unacceptable behavior includes:**
- Harassment, trolling, or insulting comments
- Publishing others' private information
- Any conduct that would be inappropriate in a professional setting

---

## How Can I Contribute?

### Reporting Bugs

**Before submitting a bug report:**
- Check the [troubleshooting guide](docs/TROUBLESHOOTING.md)
- Search existing [GitHub Issues](https://github.com/ramdoor/JK_BMS_Master/issues)
- Verify the bug exists in the latest version

**When submitting a bug report, include:**
- Descriptive title
- Steps to reproduce
- Expected vs. actual behavior
- Serial output (if applicable)
- Hardware configuration
- Firmware version
- Screenshots/photos (if relevant)

**Template:**
```markdown
**Description:**
Brief description of the issue

**Steps to Reproduce:**
1. Step one
2. Step two
3. ...

**Expected Behavior:**
What should happen

**Actual Behavior:**
What actually happens

**Environment:**
- Firmware version: vX.X.X
- Hardware: T-CAN485 / Other
- Battery: 16S LFP / 14S NMC / etc.
- Inverter: BYD / Pylon / None

**Serial Output:**
```
[Paste serial output here]
```

**Additional Context:**
Any other relevant information
```

### Suggesting Features

We welcome feature suggestions! Before submitting:
- Check if the feature is already in the [roadmap](docs/ROADMAP.md)
- Search existing feature requests

**Feature request template:**
```markdown
**Feature Description:**
Clear description of the proposed feature

**Use Case:**
Why is this feature needed? What problem does it solve?

**Proposed Implementation:**
(Optional) How might this be implemented?

**Alternatives Considered:**
(Optional) Other solutions you've thought about

**Additional Context:**
Any other relevant information
```

### Code Contributions

We appreciate code contributions! Types of contributions we're looking for:
- Bug fixes
- New inverter protocol support
- Performance improvements
- Documentation improvements
- Test coverage expansion
- UI/UX enhancements

---

## Development Setup

### Prerequisites
- **PlatformIO IDE** or **PlatformIO CLI**
- **Git**
- **Python 3.7+** (for build scripts)
- **ESP32 board** (T-CAN485 recommended)

### Initial Setup

1. **Fork the repository**
   - Click "Fork" on GitHub
   - Clone your fork:
     ```bash
     git clone https://github.com/YOUR_USERNAME/JK_BMS_Master.git
     cd JK_BMS_Master
     ```

2. **Add upstream remote**
   ```bash
   git remote add upstream https://github.com/ORIGINAL_OWNER/JK_BMS_Master.git
   ```

3. **Install dependencies**
   ```bash
   # PlatformIO will auto-install on first build
   pio run -e esp32_debug
   ```

4. **Create a branch**
   ```bash
   git checkout -b feature/your-feature-name
   ```

### Development Workflow

1. **Keep your fork synced**
   ```bash
   git fetch upstream
   git checkout main
   git merge upstream/main
   ```

2. **Make changes** in your feature branch

3. **Test locally**
   ```bash
   pio run -e esp32_debug
   pio test
   ```

4. **Commit and push**
   ```bash
   git add .
   git commit -m "feat: add awesome feature"
   git push origin feature/your-feature-name
   ```

5. **Open a Pull Request** on GitHub

---

## Coding Standards

### General Principles
- **Readability over cleverness** – code should be easy to understand
- **DRY (Don't Repeat Yourself)** – avoid code duplication
- **KISS (Keep It Simple, Stupid)** – simple solutions are usually best
- **YAGNI (You Aren't Gonna Need It)** – don't add unused features

### C++ Style Guide

**Naming Conventions:**
```cpp
// Variables: camelCase
int cellCount = 0;
float packVoltage = 0.0f;

// Constants: UPPER_SNAKE_CASE
#define MAX_MODULES 10
const uint16_t AHBC_ID1 = 0x3C2;

// Functions: camelCase
void updateCellVoltages() { ... }
float calculateSoC() { ... }

// Classes/Structs: PascalCase
struct Module { ... };
class BatteryManager { ... };

// Enums: PascalCase with UPPER values
enum SysState : uint8_t {
  ST_OPEN = 0,
  ST_PRECHARGING = 1,
  ST_CLOSED = 2
};
```

**Formatting:**
```cpp
// Indentation: 2 spaces (not tabs)
void function() {
  if (condition) {
    doSomething();
  } else {
    doSomethingElse();
  }
}

// Braces: K&R style (opening brace on same line)
if (armed) {
  closeContactors();
}

// Line length: Max 120 characters (soft limit)

// Comments: Use // for single-line, /* */ for multi-line
// This is a single-line comment

/*
 * Multi-line comment
 * for complex explanations
 */
```

**Best Practices:**
```cpp
// Always initialize variables
float voltage = 0.0f;  // ✅ Good
float voltage;         // ❌ Bad (uninitialized)

// Use const where applicable
const float MAX_VOLTAGE = 3.65f;  // ✅ Good
float MAX_VOLTAGE = 3.65f;        // ❌ Bad

// Prefer early returns over deep nesting
if (!armed) return;     // ✅ Good
if (armed) { ... }      // ❌ Bad (if entire function is inside)

// Use enum classes for type safety (when available)
enum class State { OPEN, CLOSED };  // ✅ Good
enum State { OPEN, CLOSED };        // ❌ Less safe

// Avoid magic numbers
if (voltage > 3.65f) { ... }           // ❌ Bad
if (voltage > limits.cellOv) { ... }   // ✅ Good
```

### File Organization

```
src/
  main.cpp              # Main entry point (setup, loop)
  bms_manager.cpp       # BMS module handling (future refactor)
  contactor_control.cpp # Contactor logic (future refactor)
  protection.cpp        # Protection logic (future refactor)
  ...

include/
  pin_config.h          # Hardware pin definitions
  version.h             # Auto-generated version info
  bms_manager.h         # Header files (future refactor)
  ...
```

### Documentation

**Function documentation:**
```cpp
/**
 * @brief Calculate State of Charge using coulomb counting
 * 
 * @param current_mA Current in milliamps (positive = charging)
 * @param dt_s Time delta in seconds
 * @return Updated SoC percentage (0-100)
 * 
 * @note This function uses the configured coulombic efficiency
 * @warning Ensure current sensor is calibrated before use
 */
float calculateSoC(int32_t current_mA, float dt_s) {
  // Implementation
}
```

**Inline comments:**
```cpp
// Good: Explain WHY, not WHAT
// Prevent oscillation by requiring hysteresis margin before re-enabling
if (minCellV > limits.cellUv + CELL_UV_HYST) {
  allowDischarge = true;
}

// Bad: Obvious comment (don't do this)
// Set allowDischarge to true
allowDischarge = true;
```

---

## Commit Guidelines

### Commit Message Format

We follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, no logic change)
- `refactor`: Code refactoring (no behavior change)
- `perf`: Performance improvement
- `test`: Adding or updating tests
- `chore`: Maintenance tasks (build, dependencies)

**Examples:**
```bash
feat(inverter): add Sofar protocol support

Implement Sofar inverter CAN protocol including:
- Message frame definitions
- Periodic transmission logic
- Integration with main inverter task

Closes #42

---

fix(contactor): prevent rapid toggle on fault oscillation

Added minimum toggle delay enforcement to prevent
relay damage when fault conditions oscillate.

Fixes #38

---

docs(readme): update hardware requirements section

Added missing current sensor specifications
and clarified contactor rating requirements.

---

refactor: extract protection logic to separate module

Moved voltage/temperature protection functions to
protection.cpp for better code organization.
No behavioral changes.
```

### Commit Best Practices

- **One logical change per commit** (small, focused commits)
- **Write descriptive commit messages** (future you will thank you)
- **Reference issues** (use "Closes #123", "Fixes #456")
- **Test before committing** (ensure code compiles and works)

---

## Pull Request Process

### Before Submitting

1. **Ensure code compiles** across all environments:
   ```bash
   pio run -e esp32_production
   pio run -e esp32_debug
   ```

2. **Run tests** (if available):
   ```bash
   pio test
   ```

3. **Update documentation** (if applicable):
   - README.md (if adding features)
   - FSD.md (if changing functionality)
   - HARDWARE.md (if changing wiring)

4. **Follow coding standards** (see above)

5. **Rebase on latest main**:
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

### PR Template

When opening a PR, include:

```markdown
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Documentation update
- [ ] Refactor
- [ ] Performance improvement

## Related Issue
Closes #(issue number)

## Testing
- [ ] Tested on hardware (describe setup)
- [ ] Unit tests added/updated
- [ ] Compiles without warnings

## Checklist
- [ ] Code follows project style guidelines
- [ ] Self-reviewed code
- [ ] Commented complex sections
- [ ] Updated documentation
- [ ] No breaking changes (or documented if yes)

## Screenshots (if applicable)
[Attach screenshots of UI changes, etc.]

## Additional Notes
Any other relevant information
```

### PR Review Process

1. **Automated checks** must pass (CI/CD build)
2. **Code review** by maintainer(s)
3. **Address feedback** (push new commits to same branch)
4. **Approval** by at least one maintainer
5. **Merge** (squash and merge preferred for clean history)

---

## Testing

### Manual Testing Checklist

Before submitting changes, verify:

- [ ] Code compiles without errors/warnings
- [ ] Firmware uploads successfully
- [ ] Basic functionality works (arm/disarm, web UI)
- [ ] No regression (old features still work)
- [ ] Edge cases considered

### Automated Testing (Future)

We're working on automated tests. If adding testable code:
- Write unit tests for new functions
- Update integration tests if changing behavior
- Aim for >70% code coverage

---

## Documentation

**Always update docs when:**
- Adding new features
- Changing behavior
- Adding configuration options
- Modifying wiring/hardware requirements

**Documentation files:**
- `README.md` – Overview and quick start
- `docs/FSD.md` – Functional specification
- `docs/HARDWARE.md` – Wiring and hardware setup
- `docs/TROUBLESHOOTING.md` – Common issues
- `docs/ROADMAP.md` – Development plan

---

## Questions?

- **GitHub Discussions** – For general questions and ideas
- **GitHub Issues** – For bug reports and feature requests
- **Discord/Forum** – (Link if available)

---

## License

By contributing, you agree that your contributions will be licensed under the **GNU General Public License v3.0** (same as the project).

---

**Thank you for contributing! 🎉**
